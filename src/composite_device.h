// Copyright 2018 The Chromium OS Authors. All rights reserved.
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
 private:
  std::vector<std::shared_ptr<USBDevice>> devices_;

  // Store the image buffers in this map as follow:
  // {
  //   kLogiDeviceVideo   : video-image-buffer,
  //   kLogiDeviceEeprom  : eeprom-image-buffer,
  //   kLogiDeviceMcu2    : mcu2-image-buffer,
  // }
  std::map<int, std::vector<uint8_t>> image_buffers_;

 public:
  CompositeDevice();

  /**
   * @brief Constructor with video pid, eeprom pid and mcu2 pid.
   * @param video_pid Video product id.
   * @param eeprom_pid Eeprom product id.
   * @param mcu2_pid Motor control unit product id.
   */
  CompositeDevice(std::string video_pid,
                  std::string eeprom_pid,
                  std::string mcu2_pid);

  virtual ~CompositeDevice();

  /**
   * @brief Open all component devices.
   * @return kLogiErrorNoError if successfully opened or error code if one of
   * the components fails to open.
   */
  int OpenDevices();

  /**
   * @brief Close all component devices.
   */
  void CloseDevices();

  /**
   * @brief Get version number of each component in the composite device.
   * @param version_map Output version map containing all component versions of
   * the composite device. Version map is formatted as follow:
   * {
   *    kLogiDeviceVideo  : "video-version",
   *    kLogiDeviceEeprom : "eeprom-version",
   *    kLogiDeviceMcu2   : "mcu2-version",
   * }
   * @return kLogiErrorNoError if succeeded or error code if fails to get
   * one of the component versions.
   */
  int GetDevicesVersion(std::map<int, std::string>* version_map);

  /**
   * @brief Get name of each component in the composite device.
   * @param name Output device name.
   * @return kLogiErrorNoError if succeeded or error code if failed to get one
   * of the component versions.
   */
  int GetDeviceName(std::string* name);

  /**
   * @brief Verify and get version number of each image buffers.
   * @param version_map Output version map containing all images' version
   * Version map is formatted as follow:
   * {
   *    kLogiDeviceVideo  : "video-image-version",
   *    kLogiDeviceEeprom : "eeprom-image-version",
   *    kLogiDeviceMcu2   : "mcu2-image-version",
   * }
   * @return kLogiErrorNoError if succeeded or error code if fails to verify or
   * get version.
   */
  int GetImagesVersion(std::map<int, std::string>* version_map);

  /**
   * @brief Set image buffer. It does not retain or copy the buffer. It
   * replaces the old buffer if it's already existed in the map.
   * @param device_type The device type of the image. Use kLogiDeviceVideo,
   * kLogiDeviceEeprom... for this type.
   * @param buffer The image buffer to be set.
   */
  void SetImageBuffer(int device_type, std::vector<uint8_t> buffer);

  /**
   * @brief Set force update to ignore firmware version checking.
   * @param force If true, ignore version checking and perform update.
   */
  void SetForceUpdate(bool force);

  /**
   * @brief Check if device firmware is up to date.
   * @return true if device is up to date, false otherwise.
   */
  bool IsDeviceUpToDate();

  /**
   * @brief Check if device is present.
   * @return true if one of the device components is present, false otherwise.
   */
  bool IsDevicePresent();

  /**
   * @brief Check if the device is present based on device type.
   * @param device_type The device to check.
   * @return true if the device is present, false otherwise.
   */
  bool IsDevicePresent(int device_type);

  /**
   * @brief Check if images are present. This will return false if one of the
   * images is not present.
   */
  bool AreImagesPresent();

  /**
   * @brief Get device from device type.
   * @param device_type Type of the device to get.
   */
  std::shared_ptr<USBDevice> GetDeviceFromType(int device_type);

  /**
   * @brief Perform composite device components one at a time.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  int PerformComponentUpdate();
};
#endif /* SRC_COMPOSITE_DEVICE_H_ */
