// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CODEC_DEVICE_H_
#define SRC_CODEC_DEVICE_H_

#include <stdio.h>
#include "audio_device.h"

/**
 * Logitech codec device class to handle codec firmware update. Codec firmware
 * is on the audio chip. Therefore, most methods are directly from Audio Device.
 * When updating codec firmware, Device Firmware Update mode is not used and
 * the product ID does not change.
 */
class CodecDevice : public AudioDevice {
 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  CodecDevice(std::string pid);
  virtual ~CodecDevice();

  virtual bool IsPresent();
  virtual int OpenDevice();
  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);
  virtual int CheckDeviceStatusAfterSendingData();

 private:
  /**
   * @brief Verifies the updated image after sending it to the device. This
   * computes the checksum, sends it to the device and queries the result back.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int VerifyUpdateImage(std::vector<uint8_t> buffer);

  /**
   * @brief Resets the devices after firmware update. This waits and reopens
   * device after resetting.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int Reset();
};
#endif /* SRC_CODEC_DEVICE_H_ */
