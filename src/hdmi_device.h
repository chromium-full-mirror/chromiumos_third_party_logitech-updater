// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_HDMI_DEVICE_H_
#define SRC_HDMI_DEVICE_H_

#include <stdio.h>
#include "video_device.h"

/**
 * Logitech HDMI device class to handle HDMI firmware update.
 */
class HdmiDevice : public VideoDevice {
 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  HdmiDevice(std::string pid);
  virtual ~HdmiDevice();

  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
};
#endif /* SRC_HDMI_DEVICE_H_ */
