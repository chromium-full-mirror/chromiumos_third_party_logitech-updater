// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MCU2_DEVICE_H_
#define SRC_MCU2_DEVICE_H_

#include <stdio.h>
#include "video_device.h"

/**
 * Logitech MCU2 device class to handle motor control unit firmware update
 */
class Mcu2Device : public VideoDevice {
 public:
  /**
   * @brief Constructor with product id
   * @param pid Product id string
   */
  Mcu2Device(std::string pid);
  virtual ~Mcu2Device();

  /**
   * @brief Reads the device version.
   * @param deviceVersion Output device version string.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  virtual int readDeviceVersion(std::string* deviceVersion);

  /**
   * @brief Gets the binary image version.
   * @param buffer The image buffer to read from.
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
   * @brief Initiates the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if initiated ok, error code otherwise.
   */
  virtual int aitInitiateUpdate();

  /**
   * @brief Sends image buffer to the AIT chip (video device chip).
   * @param buffer The image buffer.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  virtual int aitSendImage(std::vector<char> buffer);

  /**
   * @brief Finalizes the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if finalized ok, error code otherwise.
   */
  virtual int aitFinalizeUpdate();
};
#endif /* SRC_MCU2_DEVICE_H_ */
