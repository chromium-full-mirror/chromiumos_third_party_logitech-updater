// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MCU2_DEVICE_H_
#define SRC_MCU2_DEVICE_H_

#include <stdio.h>
#include "video_device.h"

/**
 * Logitech MCU2 device class to handle motor control unit firmware update.
 */
class Mcu2Device : public VideoDevice {
 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  Mcu2Device(std::string pid);
  virtual ~Mcu2Device();

  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int AitInitiateUpdate();
  virtual int AitSendImage(std::vector<uint8_t> buffer);
  virtual int AitFinalizeUpdate();
};
#endif /* SRC_MCU2_DEVICE_H_ */
