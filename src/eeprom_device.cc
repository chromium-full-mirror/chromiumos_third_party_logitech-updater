// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "eeprom_device.h"
#include "utilities.h"
#include <base/logging.h>
#include <brillo/syslog_logging.h>

constexpr unsigned int kLogiEepromDeviceVersionDataSize = 2;
constexpr uint16_t kLogiEepromAddressSignatureByte0 = 0x00;
constexpr uint16_t kLogiEepromAddressSignatureByte1 = 0x01;
constexpr uint16_t kLogiEepromAdressMajorVersion = 0x10;
constexpr uint16_t kLogiEepromAddressMinorVersion = 0x0f;
constexpr unsigned int kLogiEepromMaxAttemptSendBlockData = 3;
constexpr unsigned char kLogiUvcXuDevInfoCsEepromVersion = 3;
constexpr unsigned char kLogiEepromUvcXuTestDbgCsEepromAddress = 3;
constexpr unsigned char kLogiEepromUvcXuTestDbgCsEepromAccess = 4;
constexpr unsigned int kLogiEepromImageVerificationMinSize = 6;

EepromDevice::EepromDevice(std::string pid)
    : VideoDevice(pid, kLogiDeviceEeprom) {}

EepromDevice::~EepromDevice() {
  closeDevice();
}

int EepromDevice::readDeviceVersion(std::string* deviceVersion) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  std::vector<unsigned char> outputData;
  int unitId = GetUnitID(kLogiGuidDeviceInfo);
  int error =
      getXuControl(unitId, kLogiUvcXuDevInfoCsEepromVersion, &outputData);
  if (error)
    return error;
  if (outputData.size() < kLogiEepromDeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;

  // device version is valid
  int major = (int)outputData[1];
  int minor = (int)outputData[0];
  *deviceVersion = GetDeviceStringVersion(major, minor);
  return kLogiErrorNoError;
}

int EepromDevice::getImageVersion(std::vector<char> buffer,
                                  std::string* imageVersion) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  uint8_t major = 0;
  uint8_t minor = 0;
  std::vector<S19Block> imageBlocks;
  int error = parseS19(buffer, &imageBlocks);
  if (error)
    return error;
  error = readImageByte(imageBlocks, kLogiEepromAdressMajorVersion, &major);
  if (error)
    return error;
  error = readImageByte(imageBlocks, kLogiEepromAddressMinorVersion, &minor);
  if (error)
    return error;

  std::string version = GetDeviceStringVersion(major, minor);
  *imageVersion = version;
  return kLogiErrorNoError;
}

int EepromDevice::verifyImage(std::vector<char> buffer) {
  if (buffer.empty() || buffer.size() < kLogiEepromImageVerificationMinSize)
    return kLogiErrorImageBufferReadFailed;

  std::vector<S19Block> imageBlocks;
  int error = parseS19(buffer, &imageBlocks);
  if (error)
    return error;

  // perform some sanity checks
  uint8_t signatureAA = 0x00;
  uint8_t signature55 = 0x00;
  error = readImageByte(imageBlocks, kLogiEepromAddressSignatureByte0,
                        &signatureAA);
  if (error)
    return error;
  error = readImageByte(imageBlocks, kLogiEepromAddressSignatureByte1,
                        &signature55);
  if (error)
    return error;

  if (signatureAA != 0xAA || signature55 != 0x55)
    return kLogiErrorImageVerificationFailed;

  return kLogiErrorNoError;
}

int EepromDevice::parseS19(std::vector<char> buffer,
                           std::vector<S19Block>* imageBlocks) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Split the binary file into lines
  std::vector<std::string> lines = splitBinaryIntoLines(buffer);
  if (lines.empty())
    return kLogiErrorParseS19BinaryFailed;

  std::vector<S19Block> resultBlocks;
  S19Block block;

  for (const auto& line : lines) {
    if (line[0] != 'S')
      return kLogiErrorParseS19BinaryFailed;

    int addressBytes = 0;
    const auto commandString = line[1];
    switch (commandString) {
      case '0':  // Block header
        block = S19Block();
        addressBytes = 2;
        break;
      case '1':  // Data sequence (2-byte address)
      case '9':  // End of block (2-byte address)
        addressBytes = 2;
        break;
      default:  // Others are not supported by this parser
        return kLogiErrorParseS19BinaryFailed;
    }

    const unsigned int byteCount = stoi(line.substr(2, 2), NULL, 16);
    const uint16_t address =
        (uint16_t)stoi(line.substr(4, addressBytes * 2), NULL, 16);
    uint8_t runningCheckSum =
        (uint8_t)(byteCount + (address >> 8) + (address & 0x00ff));

    const auto payloadString = line.substr(1 + 1 + 2 + addressBytes * 2);
    if (payloadString.size() != 2 * (byteCount - addressBytes))
      return kLogiErrorParseS19BinaryFailed;
    std::vector<uint8_t> data;
    auto it = begin(payloadString);
    uint8_t expectedCheckSum = 0;
    for (;;) {
      uint8_t byte;
      // Most-significant nibble
      char c = (char)(*it++);
      if (!ConvertHexCharToUnsignedInt(c, &byte))
        return kLogiErrorParseS19BinaryFailed;
      byte <<= 4;

      // Least-significant nibble
      c = (char)(*it++);
      uint8_t leastSignificantByte;
      if (!ConvertHexCharToUnsignedInt(c, &leastSignificantByte))
        return kLogiErrorParseS19BinaryFailed;
      byte |= leastSignificantByte;

      if (it != end(payloadString)) {
        // Data byte
        data.push_back(byte);
        runningCheckSum += byte;
      } else {
        // Check sum (last byte)
        expectedCheckSum = byte;
        break;
      }
    }
    if ((uint8_t)~runningCheckSum != expectedCheckSum)
      return kLogiErrorParseS19BinaryFailed;

    switch (commandString) {
      case '0':  // Block header
        block.Header = make_pair(address, data);
        break;
      case '1':  // Data sequence (2-byte address)
        block.Data.push_back(make_pair(address, data));
        break;
      case '9':  // End of block (2-byte address)
        resultBlocks.push_back(block);
        break;
      default:  // Others are not supported by this parser
        return kLogiErrorParseS19BinaryFailed;
    }
  }

  *imageBlocks = resultBlocks;
  return kLogiErrorNoError;
}

int EepromDevice::sendImage(std::vector<S19Block> imageBlocks) {
  if (imageBlocks.empty())
    return kLogiErrorReadS19ImageByteFailed;

  int bytesWritten = 0;
  for (const auto& block : imageBlocks) {
    for (const auto& dataPointer : block.Data) {
      const auto baseAddress = dataPointer.first;
      const auto& data = dataPointer.second;
      for (uint16_t offset = 0; offset < data.size(); offset++) {
        int attempts = kLogiEepromMaxAttemptSendBlockData;
        while (attempts-- > 0) {
          int error = writeEepromByte(baseAddress + offset, data[offset]);
          if (error)
            continue;  // Failed this attempt, retry to send image.
          break;
        }

        if (attempts <= 0)
          return kLogiErrorSendImageFailed;
      }
      bytesWritten += data.size();
    }
  }
  return kLogiErrorNoError;
}

int EepromDevice::performUpdate(std::vector<char> buffer, bool force) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  LOG(INFO) << "Checking EEPROM firmware...";
  std::string deviceVersion;
  std::string imageVersion;
  int error = getDeviceVersion(&deviceVersion);
  if (error) {
    LOG(ERROR) << "Failed to get device version. Error code: " << error;
    return error;
  }

  error = verifyImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to verify image. Error code: " << error;
    return error;
  }

  error = getImageVersion(buffer, &imageVersion);
  if (error) {
    LOG(ERROR) << "Failed to get image version. Error code: " << error;
    return error;
  }

  // check if update available
  int compareVersion = CompareVersions(deviceVersion, imageVersion);
  if (compareVersion < 0 || force) {
    LOG(INFO) << "Updating EEPROM firmware...";
    std::vector<S19Block> imageBlocks;
    error = parseS19(buffer, &imageBlocks);
    if (error) {
      LOG(ERROR) << "Failed to parse image blocks. Error code: " << error;
      return error;
    }
    error = sendImage(imageBlocks);
    if (error) {
      LOG(ERROR) << "Failed to send image. Error code: " << error;
    } else {
      LOG(INFO) << "Successfully updated EEPROM firmware.";
    }
  } else {
    LOG(INFO) << "EEPROM firmware is up to date.";
  }
  return error;
}

std::vector<std::string> EepromDevice::splitBinaryIntoLines(
    std::vector<char> buffer) {
  std::vector<std::string> lines;
  std::string line;
  for (const auto byte : buffer) {
    if (isalnum(byte)) {
      line.push_back(byte);
    } else if (byte == '\r' || byte == '\n') {
      if (!line.empty()) {
        lines.push_back(line);
        line.clear();
      }
    } else {
      return std::vector<std::string>();
    }
  }
  return lines;
}

int EepromDevice::readImageByte(std::vector<S19Block> imageBlocks,
                                uint16_t address,
                                uint8_t* value) {
  if (imageBlocks.empty())
    return kLogiErrorReadS19ImageByteFailed;
  for (const auto& block : imageBlocks) {
    for (const auto& p : block.Data) {
      const auto baseAddress = p.first;
      const auto& data = p.second;

      const auto blockSize = data.size();
      if (address >= baseAddress && address < (baseAddress + blockSize)) {
        const auto offset = address - baseAddress;  // Offset within this block

        *value = data[offset];
        return kLogiErrorNoError;
      }
    }
  }
  return kLogiErrorReadS19ImageByteFailed;
}

int EepromDevice::countImageBytes(std::vector<S19Block> imageBlocks) {
  if (imageBlocks.empty())
    return kLogiErrorReadS19ImageByteFailed;
  int byteCount = 0;
  for (const auto& block : imageBlocks) {
    for (const auto& data : block.Data)
      byteCount += data.second.size();
  }
  return byteCount;
}

int EepromDevice::writeEepromByte(uint16_t address, uint8_t byte) {
  unsigned char firstByte = uint8_t(address & 0xff);
  unsigned char secondByte = uint8_t(address >> 8);
  std::vector<unsigned char> addressData;
  addressData.push_back(firstByte);
  addressData.push_back(secondByte);
  int unitID = GetUnitID(kLogiGuidTestDebug);
  int error =
      setXuControl(unitID, kLogiEepromUvcXuTestDbgCsEepromAddress, addressData);
  if (error)
    return error;

  std::vector<unsigned char> byteData;
  byteData.push_back(byte);
  return setXuControl(unitID, kLogiEepromUvcXuTestDbgCsEepromAccess, byteData);
}
