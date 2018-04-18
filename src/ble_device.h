// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BLE_DEVICE_H_
#define SRC_BLE_DEVICE_H_

#include <stdio.h>
#include "audio_device.h"

/**
 * Logitech Bluetooth Low Energy (BLE) device class to handle BLE firmware
 * update. BLE firmware is on the audio chip. Update the firmware from
 * HID mode.
 */

class BleDevice : public AudioDevice {
 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  BleDevice(std::string pid);
  virtual ~BleDevice();

  virtual bool IsPresent();
  virtual int OpenDevice();
  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);

 private:
  /**
   * @brief Computes CRC checksum for the data block.
   * @param data Block data to compute the CRC.
   * @param size Block data size.
   * @param pre_crc Value to add to the computed crc.
   * @return block crc checksum.
   */
  uint16_t ComputeBlockCRC(const uint8_t* data, int size, uint16_t pre_crc);

  /**
   * @brief Computes the checksum of the buffer. This uses the block CRC method
   * to compute the checksum for each block and accumulates the checksum.
   * @brief buffer Image buffer to compute checksum.
   * @return image buffer crc checksum.
   */
  uint16_t ComputeChecksum(std::vector<uint8_t> buffer);

  /**
   * @brief Gets the report status after sending the packet to the device.
   * @param report_size The report data size in bytes.
   * @return kLogiErrorNoError if packet sent ok, error code otherwise.
   */
  int GetPacketReportStatus(int report_size);

  /**
   * @brief Sends image to the device.
   * @param buffer The image buffer to send.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int SendBleImage(std::vector<uint8_t> buffer);

  /**
   * @brief Send initial BLE packet to the device.
   * @param buffer_size Size of the binary image buffer.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int SendInitialPacket(int buffer_size);

  /**
   * @brief Compute checksum packet with given subcommand.
   * @param buffer The image buffer to compute checksum.
   * @param subcommand Subcommand to send with checksum packet.
   * @return Checksum packet
   */
  std::vector<uint8_t> ComputeChecksumPacket(std::vector<uint8_t> buffer,
                                             uint8_t subcommand);

  /**
   * @brief Send binary image checksum to the device.
   * @param buffer Binary image buffer to compute and send the checksum.
   * @return kLogiErrorNoError if computed and sent ok, error code otherwise.
   */
  int SendChecksum(std::vector<uint8_t> buffer);

  /**
   * @brief Send stop-packet to signal the end of update process.
   * @param buffer Binary image buffer to compute checksum for the stop-packet.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int SendStopPacket(std::vector<uint8_t> buffer);
};
#endif /* SRC_BLE_DEVICE_H_ */
