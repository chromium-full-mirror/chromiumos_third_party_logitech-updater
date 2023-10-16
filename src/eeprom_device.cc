// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "eeprom_device.h"

#include <memory>

#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>

#include "utilities.h"

constexpr unsigned int kLogiEepromDeviceVersionDataSize = 2;
constexpr uint16_t kLogiEepromAddressSignatureByte0 = 0x00;
constexpr uint16_t kLogiEepromAddressSignatureByte1 = 0x01;
constexpr uint16_t kLogiEepromAdressMajorVersion = 0x10;
constexpr uint16_t kLogiEepromAddressMinorVersion = 0x0f;
constexpr unsigned int kLogiEepromMaxAttemptSendBlockData = 3;
constexpr unsigned char kLogiUvcXuDevInfoCsEepromVersion = 0x03;
constexpr unsigned char kLogiEepromUvcXuTestDbgCsEepromAddress = 0x03;
constexpr unsigned char kLogiEepromUvcXuTestDbgCsEepromAccess = 0x04;
constexpr unsigned int kLogiEepromImageVerificationMinSize = 6;
constexpr uint16_t kLogiEepromBrandInfoAddress = 0x0121;
constexpr uint8_t kLogiEepromBrandLogicoolByte = 0xAA;
constexpr uint8_t kLogiEepromBrandLogitechByte = 0xBB;

EepromDevice::EepromDevice(std::string pid)
    : VideoDevice(pid, kLogiDeviceEeprom) {}

EepromDevice::~EepromDevice() {}

int EepromDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  std::vector<unsigned char> output_data;
  int unit_id = GetUnitID(kLogiGuidDeviceInfo);
  int error =
      GetXuControl(unit_id, kLogiUvcXuDevInfoCsEepromVersion, &output_data);
  if (error)
    return error;
  if (output_data.size() < kLogiEepromDeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;

  // Device version is valid.
  int major = static_cast<int>(output_data[1]);
  int minor = static_cast<int>(output_data[0]);
  *device_version = GetDeviceStringVersion(major, minor);
  return kLogiErrorNoError;
}

int EepromDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                  std::string* image_version) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  uint8_t major = 0;
  uint8_t minor = 0;
  std::vector<S19Block> image_blocks;
  int error = ParseS19(buffer, &image_blocks);
  if (error)
    return error;

  error = ReadImageByte(image_blocks, kLogiEepromAdressMajorVersion, &major);
  if (error)
    return error;

  error = ReadImageByte(image_blocks, kLogiEepromAddressMinorVersion, &minor);
  if (error)
    return error;

  std::string version = GetDeviceStringVersion(major, minor);
  *image_version = version;
  return kLogiErrorNoError;
}

int EepromDevice::VerifyImage(std::vector<uint8_t> buffer) {
  if (buffer.empty() || buffer.size() < kLogiEepromImageVerificationMinSize)
    return kLogiErrorImageBufferReadFailed;

  std::vector<S19Block> image_blocks;
  int error = ParseS19(buffer, &image_blocks);
  if (error)
    return error;

  // Perform some sanity checks. S19 binary image should contain 0xAA at address
  // 0 and 0x55 at address 1.
  uint8_t signature_aa = 0x00;
  uint8_t signature_55 = 0x00;
  error = ReadImageByte(image_blocks, kLogiEepromAddressSignatureByte0,
                        &signature_aa);
  if (error)
    return error;
  error = ReadImageByte(image_blocks, kLogiEepromAddressSignatureByte1,
                        &signature_55);
  if (error)
    return error;

  if (signature_aa != 0xAA || signature_55 != 0x55)
    return kLogiErrorImageVerificationFailed;

  return kLogiErrorNoError;
}

int EepromDevice::ParseS19(std::vector<uint8_t> buffer,
                           std::vector<S19Block>* image_blocks) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Split the binary file into lines.
  std::vector<std::string> lines = SplitBinaryIntoLines(buffer);
  if (lines.empty())
    return kLogiErrorParseS19BinaryFailed;

  std::vector<S19Block> result_blocks;
  S19Block block;

  for (const auto& line : lines) {
    if (line[0] != 'S')
      return kLogiErrorParseS19BinaryFailed;

    int address_bytes = 0;
    const auto command_string = line[1];
    switch (command_string) {
      case '0':  // Block header.
        block = S19Block();
        address_bytes = 2;
        break;
      case '1':  // Data sequence (2-byte address).
      case '9':  // End of block (2-byte address).
        address_bytes = 2;
        break;
      default:  // Others are not supported by this parser.
        return kLogiErrorParseS19BinaryFailed;
    }

    const unsigned int byte_count = stoi(line.substr(2, 2), NULL, 16);
    const uint16_t address = static_cast<uint16_t>(
        stoi(line.substr(4, address_bytes * 2), NULL, 16));
    uint8_t checksum = static_cast<uint8_t>(
        (byte_count + (address >> 8) + (address & 0x00ff)));

    const auto payload_string = line.substr(1 + 1 + 2 + address_bytes * 2);
    if (payload_string.size() != 2 * (byte_count - address_bytes))
      return kLogiErrorParseS19BinaryFailed;
    std::vector<uint8_t> data;
    auto it = begin(payload_string);
    uint8_t expected_checksum = 0;
    for (;;) {
      uint8_t byte;
      // Most-significant nibble.
      char c = (char)(*it++);
      if (!ConvertHexCharToUnsignedInt(c, &byte))
        return kLogiErrorParseS19BinaryFailed;
      byte <<= 4;

      // Least-significant nibble.
      c = (char)(*it++);
      uint8_t least_significant_byte;
      if (!ConvertHexCharToUnsignedInt(c, &least_significant_byte))
        return kLogiErrorParseS19BinaryFailed;
      byte |= least_significant_byte;

      if (it != end(payload_string)) {
        // Data byte.
        data.push_back(byte);
        checksum += byte;
      } else {
        // Check sum (last byte).
        expected_checksum = byte;
        break;
      }
    }
    if (expected_checksum != static_cast<uint8_t>(~checksum))
      return kLogiErrorParseS19BinaryFailed;

    switch (command_string) {
      case '0':  // Block header.
        block.Header = make_pair(address, data);
        break;
      case '1':  // Data sequence (2-byte address).
        block.Data.push_back(make_pair(address, data));
        break;
      case '9':  // End of block (2-byte address).
        result_blocks.push_back(block);
        break;
      default:  // Others are not supported by this parser.
        return kLogiErrorParseS19BinaryFailed;
    }
  }

  *image_blocks = result_blocks;
  return kLogiErrorNoError;
}

int EepromDevice::SendImage(std::vector<S19Block> image_blocks) {
  if (image_blocks.empty())
    return kLogiErrorReadS19ImageByteFailed;

  for (const auto& block : image_blocks) {
    for (const auto& data_pointer : block.Data) {
      const auto base_address = data_pointer.first;
      const auto& data = data_pointer.second;
      for (uint16_t offset = 0; offset < data.size(); offset++) {
        int attempts = kLogiEepromMaxAttemptSendBlockData;
        while (attempts-- > 0) {
          int error = WriteEepromByte(base_address + offset, data[offset]);
          if (error)
            continue;  // Failed this attempt, try to resend image
          break;
        }

        if (attempts <= 0)
          return kLogiErrorSendImageFailed;
      }
    }
  }
  return kLogiErrorNoError;
}

int EepromDevice::PerformUpdate(std::vector<uint8_t> buffer,
                                std::vector<uint8_t> secure_header,
                                bool* did_update) {
  *did_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  LOG(INFO) << "Checking eeprom firmware...";
  bool should_update;
  int error = CheckForUpdate(buffer, &should_update);
  if (error || !should_update)
    return error;

  LOG(INFO) << "Eeprom firmware is not up to date. Updating firmware...";

  std::string device_name;
  GetDeviceName(&device_name);
  device_name += " EEPROM";
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(device_name);
  dfu_notification->NotifyStartUpdate();
  // Stores the eeprom brand info before updating.
  bool logicool;
  int brand_info_error = ReadBrandInfo(&logicool);

  std::vector<S19Block> image_blocks;
  error = ParseS19(buffer, &image_blocks);
  if (error) {
    LOG(ERROR) << "Failed to parse image buffer. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  error = SendImage(image_blocks);

  // Saves the eeprom brand info.
  if (!brand_info_error)
    WriteBrandInfo(logicool);

  if (error) {
    LOG(ERROR) << "Failed to send image. Error: " << error;
  } else {
    LOG(INFO) << "Successfully updated eeprom firmware.";
    *did_update = true;
  }
  dfu_notification->NotifyEndUpdate(!error);
  return error;
}

std::vector<std::string> EepromDevice::SplitBinaryIntoLines(
    std::vector<uint8_t> buffer) {
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

int EepromDevice::ReadImageByte(std::vector<S19Block> image_blocks,
                                uint16_t address,
                                uint8_t* value) {
  if (image_blocks.empty())
    return kLogiErrorReadS19ImageByteFailed;
  for (const auto& block : image_blocks) {
    for (const auto& p : block.Data) {
      const auto base_address = p.first;
      const auto& data = p.second;

      const auto block_size = data.size();
      if (address >= base_address && address < (base_address + block_size)) {
        const auto offset = address - base_address;

        *value = data[offset];
        return kLogiErrorNoError;
      }
    }
  }
  return kLogiErrorReadS19ImageByteFailed;
}

int EepromDevice::CountImageBytes(std::vector<S19Block> image_blocks) {
  if (image_blocks.empty())
    return kLogiErrorReadS19ImageByteFailed;
  int byte_count = 0;
  for (const auto& block : image_blocks) {
    for (const auto& data : block.Data)
      byte_count += data.second.size();
  }
  return byte_count;
}

int EepromDevice::WriteEepromByte(uint16_t address, uint8_t byte) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  unsigned char first_byte = static_cast<uint8_t>(address & 0xff);
  unsigned char second_byte = static_cast<uint8_t>((address >> 8) & 0xFF);
  std::vector<unsigned char> address_data;
  address_data.push_back(first_byte);
  address_data.push_back(second_byte);
  int unit_id = GetUnitID(kLogiGuidTestDebug);
  int error = SetXuControl(unit_id, kLogiEepromUvcXuTestDbgCsEepromAddress,
                           address_data);
  if (error) {
    LOG(ERROR) << "Failed to set eeprom address to access: " << address
               << ". Error: " << error;
    return error;
  }

  std::vector<unsigned char> byte_data;
  byte_data.push_back(byte);
  error =
      SetXuControl(unit_id, kLogiEepromUvcXuTestDbgCsEepromAccess, byte_data);
  if (error) {
    LOG(ERROR) << "Failed to write byte: " << byte
               << " to eeprom address: " << address << ". Error: " << error;
  }
  return error;
}

int EepromDevice::ReadEepromBytes(uint16_t address,
                                  std::vector<unsigned char>* bytes) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<unsigned char> data = {
      static_cast<uint8_t>(address & 0xFF),
      static_cast<uint8_t>((address >> 8) & 0xFF)};
  int unit_id = GetUnitID(kLogiGuidTestDebug);

  int error =
      SetXuControl(unit_id, kLogiEepromUvcXuTestDbgCsEepromAddress, data);
  if (error) {
    LOG(ERROR) << "Failed to set eeprom address to access: " << address
               << ". Error: " << error;
    return error;
  }

  error = GetXuControl(unit_id, kLogiEepromUvcXuTestDbgCsEepromAccess, bytes);
  if (error) {
    LOG(ERROR) << "Failed to read bytes from eeprom address: " << address
               << ". Error: " << error;
  }
  return error;
}

int EepromDevice::WriteBrandInfo(bool logicool) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  // Writes 1 if logicool, 0 if logitech.
  uint8_t byte =
      (logicool) ? kLogiEepromBrandLogicoolByte : kLogiEepromBrandLogitechByte;
  int error = WriteEepromByte(kLogiEepromBrandInfoAddress, byte);
  if (error)
    LOG(ERROR) << "Failed to write brand info to eeprom. Error: " << error;
  return error;
}

int EepromDevice::ReadBrandInfo(bool* logicool) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<unsigned char> output;
  int error = ReadEepromBytes(kLogiEepromBrandInfoAddress, &output);
  if (error) {
    LOG(ERROR) << "Failed to read brand info from eeprom. Error: " << error;
    return error;
  }
  if (output.empty()) {
    LOG(ERROR) << "Failed to read brand info from eeprom. Empty output";
    return kLogiErrorBrandCheckFailed;
  }
  if (output[0] == kLogiEepromBrandLogicoolByte) {
    *logicool = true;
    return kLogiErrorNoError;
  }
  if (output[0] == kLogiEepromBrandLogitechByte) {
    *logicool = false;
    return kLogiErrorNoError;
  }
  return kLogiErrorBrandCheckFailed;
}
