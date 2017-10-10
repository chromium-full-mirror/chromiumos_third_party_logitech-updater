// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIDEO_DEVICE_H_
#define SRC_VIDEO_DEVICE_H_

#include <stdio.h>
#include "usb_device.h"

constexpr unsigned char kLogiAitSetMmpCmdFwBurning = 0x01;

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

  /**
   * @brief Finds the device with usbPid & usbVid.
   * @return list of device paths. If list count > 0, there are multiple
   * devices.
   */
  virtual std::vector<std::string> findDevices();

  /**
   * @brief Opens the device.
   * @return kLogiErrorNoError if opened ok, error code otherwise.
   */
  virtual int openDevice();

  /**
   * @brief Closes the device.
   */
  virtual void closeDevice();

  /**
   * @brief Gets the device name.
   * @param deviceName Output device name.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int getDeviceName(std::string* deviceName);

  /**
   * @brief Gets the binary image version.
   * @param buffer The image buffer to read from.
   * @param imageVersion Output device version string.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int getImageVersion(std::vector<char> buffer,
                              std::string* imageVersion);

  /**
   * @brief Verifies the image to make sure it has the secure signature.
   * @param buffer The image buffer to verify.
   * @return kLogiErrorNoError if verified ok, error code otherwise.
   */
  virtual int verifyImage(std::vector<char> buffer);

  /**
   * @brief Reads the device version.
   * @param deviceVersion Output device version string.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  virtual int readDeviceVersion(std::string* deviceVersion);

  /**
   * @brief Performs firmware update.
   * @param buffer Firmware image buffer.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  virtual int performUpdate(std::vector<char> buffer, bool force);

  /**
   * @brief Reboots the device.
   * @return kLogiErrorNoError if rebooted ok, error code otherwise.
   */
  virtual int rebootDevice();

 protected:
  /**
   * @brief Initiates the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if initiated ok, error code otherwise.
   */
  virtual int aitInitiateUpdate();

  /**
   * @brief Sends image buffer to the AIT chip (video device chip).
   * @param buffer The image buffer
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  virtual int aitSendImage(std::vector<char> buffer);

  /**
   * @brief Finalizes the update process for the AIT chip (video device chip).
   * @return kLogiErrorNoError if finalized ok, error code otherwise.
   */
  virtual int aitFinalizeUpdate();

  /**
   * @brief Initiates the update process for the AIT chip (video device chip).
   * @param mmpData The data for setting initiation.
   * @return kLogiErrorNoError if initiated ok, error code otherwise.
   */
  int aitInitiateUpdateWithData(std::vector<unsigned char> mmpData);

  /**
   * @brief Sends image buffer to the AIT chip (video device chip).
   * @param buffer The image buffer.
   * @param offset The initial offset when send image.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int aitSendImageWithOffset(std::vector<char> buffer, unsigned int offset);

  /**
   * @brief Finalizes the update process for the AIT chip (video device chip).
   * @param mmpData The data for setting finalization.
   * @return kLogiErrorNoError if finalized ok, error code otherwise.
   */
  int aitFinalizeUpdateWithData(std::vector<unsigned char> mmpData);

 public:
  /**
   * @brief Sets data to the extension control unit (XU).
   * @param unitId XU unit id.
   * @param controlSelector XU control selector.
   * @param data XU data to be set.
   * @return kLogiErrorNoError if succeeded, error otherwise.
   */
  int setXuControl(unsigned char unitId,
                   unsigned char controlSelector,
                   std::vector<unsigned char> data);

  /**
   * @brief Gets data from the extension control unit (XU).
   * @param unitId XU unit id.
   * @param controlSelector XU control selector.
   * @param data XU data output.
   * @return kLogiErrorNoError if succeeded, error otherwise.
   */
  int getXuControl(unsigned char unitId,
                   unsigned char controlSelector,
                   std::vector<unsigned char>* data);

 private:
  /**
   * @brief Control query data size needs to be known before querying.
   * Therefore, UVC_GET_LEN needs to be queried for the size first.
   * UVC_GET_LEN query returns 2 bytes of data in little endian. Refers to
   * https://linuxtv.org/downloads/v4l-dvb-api/v4l-drviers/uvcvideo.html for
   * more info.
   * @param unitID XU unit id.
   * @param controlSelector XU control selector.
   * @param dataSize Data size output.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int queryDataSize(unsigned char unitId,
                    unsigned char controlSelector,
                    int* dataSize);
};
#endif /* SRC_VIDEO_DEVICE_H_ */
