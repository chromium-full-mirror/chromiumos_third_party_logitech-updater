// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_EEPROM_DEVICE_H_
#define SRC_EEPROM_DEVICE_H_

#include <stdio.h>
#include "video_device.h"

// Structure represents S19 binary format.
struct S19Block {
  // Header block. Header block has address and data.
  std::pair<uint16_t, std::vector<uint8_t>> Header;
  // Data blocks. Each data block has an address and data.
  std::vector<std::pair<uint16_t, std::vector<uint8_t>>> Data;
};

/**
 * Logitech eeprom device class to handle eeprom firmware update.
 */
class EepromDevice : public VideoDevice {
 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  EepromDevice(std::string pid);
  virtual ~EepromDevice();

  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);

  /**
   * @brief Writes Logicool brand info to eeprom. This is to save the brand info
   * to faciliate the firmware process. Before firmware update process on audio
   * device, brand info should be query from HID mode and store into the eeprom.
   * When audio device is rebooted, query this brand info and continue the
   * dfu update process
   * @param logicool Input flag for brand info Logicool.
   * @return kLogiErrorNoError if write info succeeded or error code otherwise
   */
  int WriteBrandInfo(bool logicool);

  /**
   * @brief Reads the brand info from eeprom address. The info can only be read
   * if it was written to eeprom by WriteBrandInfo method.
   * @param logicool Output brand info. True if brand is Logicool.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  int ReadBrandInfo(bool* logicool);

 private:
  /**
   * @brief Parses S19 binary file.
   * @param buffer The binary buffer.
   * @param image_blocks Parsed image blocks.
   * @return kLogiErrorNoError if parsed ok, error code otherwise.
   */
  int ParseS19(std::vector<uint8_t> buffer,
               std::vector<S19Block>* image_blocks);

  /**
   * @brief Sends image blocks to eeprom device.
   * @param image_blocks Image blocks to send to the device.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int SendImage(std::vector<S19Block> image_blocks);

  /**
   * @brief Splits the S19 binary file into lines.
   * @param buffer The binary buffer.
   * @return vector containing one string for each line.
   */
  std::vector<std::string> SplitBinaryIntoLines(std::vector<uint8_t> buffer);

  /**
   * @brief Reads the image byte at address.
   * @param image_blocks The S19 image block to read byte from.
   * @param address Address to read from.
   * @param value Output read byte value.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  int ReadImageByte(std::vector<S19Block> image_blocks,
                    uint16_t address,
                    uint8_t* value);

  /**
   * @brief Counts the image bytes from S19 image blocks.
   * @param image_blocks The image blocks to count number of bytes.
   * @return image blocks size or 0 if error.
   */
  int CountImageBytes(std::vector<S19Block> image_blocks);

  /**
   * @brief Writes eeprom byte to the device.
   * @param address Address to write to.
   * @param byte Data byte to write.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int WriteEepromByte(uint16_t address, uint8_t byte);

  /**
   * @brief Reads eeprom byte from the device.
   * @param address Address to read the byte data from.
   * @param bytes Output data byte vector.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int ReadEepromBytes(uint16_t address, std::vector<unsigned char>* bytes);
};
#endif /* SRC_EEPROM_DEVICE_H_ */
