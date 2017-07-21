// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COMPOSITE_DEVICE_H_
#define SRC_COMPOSITE_DEVICE_H_

#include <stdio.h>
#include <map>
#include <memory>
#include "usb_device.h"

/**
 * Composite device class to hold and handle firmware update of component usb
 * devices. For example: ptzpro2 device has 3 components: eeprom device
 * (LogiEepromDevice) motor control unit device mcu2 (LogiMCU2Device) and video
 * device (LogiVideoDevice). Some other Logitech products might have
 * audio/codec/bluetooth ble components.
 */
class CompositeDevice {
 public:
  std::vector<char> videoImageBuffer;
  std::vector<char> eepromImageBuffer;
  std::vector<char> mcu2ImageBuffer;

 private:
  std::vector<std::shared_ptr<USBDevice>> devices;  // component devices
 public:
  /**
   * @brief Constructor with video pid, eeprom pid and mcu2 pid.
   * @param videoPid Video product id.
   * @param eepromPid Eeprom product id.
   * @param mcu2Pid Motor control unit product id.
   */
  CompositeDevice(std::string videoPid,
                  std::string eepromPid,
                  std::string mcu2Pid);

  virtual ~CompositeDevice();

  /**
   * @brief Opens all component devices.
   * @return kLogiErrorNoError if successfully opened or error code if one of
   * the components fails to open.
   */
  int openDevices();

  /**
   * @brief Closes all component devices.
   */
  void closeDevices();

  /**
   * @brief Gets version number of each component in the composite device.
   * @param versionMap Output version map containing all component versions of
   * the composite device. Version map is formatted as follow
   * {
   *  kLogiDeviceVideo  : "video-version",
   *  kLogiDeviceEeprom : "eeprom-version",
   *  kLogiDeviceMcu2   : "mcu2-version",
   * }
   * @return kLogiErrorNoError if succeeded or error code if failed to get one
   * of the component versions.
   */
  int getDevicesVersion(std::map<int, std::string>* versionMap);

  /**
   * @brief Gets name of each component in the composite device.
   * @param nameMap Output name map containing all component names of the
   * composite device. name map is formatted as follow
   * {
   *  kLogiDeviceVideo  : "video-device-name",
   *  kLogiDeviceEeprom  : "eeprom-device-name",
   *  kLogiDeviceMcu2   : "mcu2-device-name",
   * }
   * @return kLogiErrorNoError if succeeded or error code if failed to get one
   * of the component version.
   */
  int getDevicesName(std::map<int, std::string>* nameMap);

  /**
   * @brief Verifies and gets version number of each image buffers assuming
   * they've been set.
   * @param versionMap Output version map containing all images' version
   * Version map is formatted as follow
   * {
   *  kLogiDeviceVideo  : "video-image-version",
   *  kLogiDeviceEeprom : "eeprom-image-version",
   *  kLogiDeviceMcu2   : "mcu2-image-version",
   * }
   * @return kLogiErrorNoError if succeeded or error code if failed to verify or
   * get version.
   */
  int getImagesVersion(std::map<int, std::string>* versionMap);

  /**
   * @brief Checks if device firmware is up to date.
   */
  bool isDeviceUpToDate();

  /**
   * @brief Checks if device is present.
   */
  bool isDevicePresent();

  /**
   * @brief Performs the firmware update on all component devices.
   * @return kLogiErrorNoError if successfully updated all components, error
   * code otherwise.
   */
  int performUpdate();

 private:
  /**
   * @brief Gets image buffer from type.
   * @param deviceType The device type: kLogiDeviceVideo, kLogiDeviceEeprom, or
   * kLogiDeviceMcu2 ...
   * @return image buffer based on device type or empty buffer if device type is
   * unknown.
   */
  std::vector<char> getImageBuffer(int deviceType);
};
#endif /* SRC_COMPOSITE_DEVICE_H_ */
