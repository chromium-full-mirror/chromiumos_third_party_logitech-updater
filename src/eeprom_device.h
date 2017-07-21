// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_EEPROM_DEVICE_H_
#define SRC_EEPROM_DEVICE_H_

#include <stdio.h>
#include "video_device.h"

// structure represents S19 binary format
struct S19Block {
  // header block. Header block has address and data
  std::pair<uint16_t, std::vector<uint8_t>> Header;
  // data blocks. Each data block has the address and data
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> Data;
};

/**
 * Logitech eeprom device class to handle eeprom firmware update.
 */
class EepromDevice : public VideoDevice {
 public:
  /**
   * @brief Constructor with product id
   * @param pid Product id string
   */
  EepromDevice(std::string pid);
  virtual ~EepromDevice();

  /**
   * @brief Reads the device version.
   * @param deviceVersion Output device version string.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  virtual int readDeviceVersion(std::string* deviceVersion);

  /**
   * @brief Gets the binary image version.
   * @param buffer The image buffer to read from
   * @param imageVersion Output device version string.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int getImageVersion(std::vector<char> buffer,
                              std::string* imageVersion);

  /**
   * @brief Verifies the image to make sure it's secure.
   * @param buffer The image buffer to verify.
   * @return kLogiErrorNoError if verified ok, error code otherwise.
   */
  virtual int verifyImage(std::vector<char> buffer);

  /**
   * @brief Performs firmware update.
   * @param buffer Firmware image buffer.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  virtual int performUpdate(std::vector<char> buffer);

 private:
  /**
   * @brief Parses S19 binary file.
   * @param buffer The binary buffer.
   * @param imageBlocks Parsed image block.
   * @return kLogiErrorNoError if parsed ok, error code otherwise.
   */
  int parseS19(std::vector<char> buffer, std::vector<S19Block>* imageBlocks);

  /**
   * @brief Sends image blocks to eeprom device.
   * @param imageBlocks Parsed image block.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int sendImage(std::vector<S19Block> imageBlocks);

  /**
   * @brief Splits the S19 binary file into lines.
   * @param buffer The binary buffer.
   * @return vector containing one string for each line.
   */
  std::vector<std::string> splitBinaryIntoLines(std::vector<char> buffer);

  /**
   * @brief Reads the image byte at address.
   * @param imageBlocks The parsed S19 image blocks.
   * @param address Address to read from.
   * @param value Output value.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  int readImageByte(std::vector<S19Block> imageBlocks,
                    uint16_t address,
                    uint8_t* value);

  /**
   * @brief Counts the image bytes.
   * @param imageBlocks The parsed S19 image blocks.
   * @return image blocks size or 0 if error.
   */
  int countImageBytes(std::vector<S19Block> imageBlocks);

  /**
   * @brief Writes eeprom byte to the device.
   * @param address Address to write to.
   * @param byte Byte to write.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int writeEepromByte(uint16_t address, uint8_t byte);
};
#endif /* SRC_EEPROM_DEVICE_H_ */
