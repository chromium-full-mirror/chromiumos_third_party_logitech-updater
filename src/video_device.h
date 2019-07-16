// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIDEO_DEVICE_H_
#define SRC_VIDEO_DEVICE_H_

#include <stdio.h>
#include "usb_device.h"

constexpr unsigned char kLogiAitSetMmpCmdFwBurning = 0x01;
const std::string kLogiDefaultRallyVideoVersion = "9.0.0";
const std::string kLogiDefaultRallyEepromVersion = "9.0";
const std::string kLogiDefaultRallyVideoBleVersion = "9.0";

/**
 * Logitech video device class to handle video firmware update.
 */
class VideoDevice : public USBDevice {
 protected:
  /**
   * @brief Constructor with product id and device type.
   * @param pid Product id string.
   * @param type Device type.
   */
  VideoDevice(std::string pid, int type);

 public:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   */
  VideoDevice(std::string pid);

  virtual ~VideoDevice();

  virtual bool IsPresent();
  virtual int OpenDevice();
  virtual int GetDeviceName(std::string* device_name);
  virtual int IsLogicool(bool* is_logicool);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);
  virtual int RebootDevice();
  virtual int ReadDeviceVersion(std::string* device_version);

 protected:
  /**
   * @brief Initiates the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if initiated ok, error code otherwise.
   */
  virtual int AitInitiateUpdate();

  /**
   * @brief Sends image buffer to the AIT chip (video device chip).
   * @param buffer The image buffer.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  virtual int AitSendImage(std::vector<uint8_t> buffer);

  /**
   * @brief Finalizes the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if finalized ok, error code otherwise.
   */
  virtual int AitFinalizeUpdate();

  /**
   * @brief Initiates the update process for the AIT chip (video device chip).
   * @param mmp_data The data for setting initiation.
   * @return kLogiErrorNoError if initiated ok, error code otherwise.
   */
  int AitInitiateUpdateWithData(std::vector<unsigned char> mmp_data);

  /**
   * @brief Sends image buffer to the AIT chip (video device chip).
   * @param buffer The image buffer.
   * @param offset The initial offset when send image.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int AitSendImageWithOffset(std::vector<uint8_t> buffer, unsigned int offset);

  /**
   * @brief Finalizes the update process for the AIT chip (video device chip).
   * @param mmp_data The data for setting finalization.
   * @return kLogiErrorNoError if finalized ok, error code otherwise.
   */
  int AitFinalizeUpdateWithData(std::vector<unsigned char> mmp_data);

 public:
  /**
   * @brief Sets data to the extension control unit (xu).
   * @param unit_id XU unit id.
   * @param control_selector XU control selector.
   * @param data XU data to be set.
   * @return kLogiErrorNoError if set ok, error otherwise.
   */
  int SetXuControl(unsigned char unit_id,
                   unsigned char control_selector,
                   std::vector<unsigned char> data);

  /**
   * @brief Gets data from the extension control unit (xu).
   * @param unit_id XU unit id.
   * @param control_selector XU control selector.
   * @param data XU data output.
   * @return kLogiErrorNoError if succeeded, error otherwise.
   */
  int GetXuControl(unsigned char unit_id,
                   unsigned char control_selector,
                   std::vector<unsigned char>* data);

 private:
  /**
   * @brief  Controls query data size needs to be known before sending queries
   * to the device by sending UVC_GET_LEN query first. UVC_GET_LEN query
   * returns 2 bytes of data in little endian. Refers to
   * https://linuxtv.org/downloads/v4l-dvb-api/v4l-drviers/uvcvideo.html for
   * more info.
   * @param unit_id XU unit id.
   * @param control_selector XU control selector.
   * @param data_size Data size output.
   * @return kLogiErrorNoError if succeeded, error otherwise
   */
  int QueryDataSize(unsigned char unit_id,
                    unsigned char control_selector,
                    int* data_size);
};
#endif /* SRC_VIDEO_DEVICE_H_ */
