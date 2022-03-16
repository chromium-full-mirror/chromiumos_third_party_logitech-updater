// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COMPOSITE_DEVICE_H_
#define SRC_COMPOSITE_DEVICE_H_

#include <stdio.h>
#include <map>
#include <memory>
#include "usb_device.h"
#include "audio_device.h"
#include "tablehub_device.h"

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

  // Stores the image buffers in this map as follow:
  // {
  //   kLogiDeviceVideo   : video-image-buffer,
  //   kLogiDeviceEeprom  : eeprom-image-buffer,
  //   kLogiDeviceMcu2    : mcu2-image-buffer,
  // }
  std::map<int, std::vector<uint8_t>> image_buffers_;
  std::map<int, std::vector<uint8_t>> secure_headers_;

  // Device with long audio cable sometimes fails to be re-mounted after being
  // put into dfu mode. Set hub vid and pid and reset flag to reset hub on dfu.
  std::string hub_vid_;
  std::string hub_pid_;
  bool should_reset_hub_;

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
   * @brief Opens all component devices.
   * @return kLogiErrorNoError if successfully opened or error code if one of
   * the components fails to open.
   */
  int OpenDevices();

  /**
   * @brief Closes all component devices.
   */
  void CloseDevices();

  /**
   * @brief Gets version number of each component in the composite device.
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
  int GetDevicesVersion(std::map<int, std::string>& version_map);

  /**
   * @brief Queries the HID on the audio device to get the topology for the
   * system, which consists of the firmware versions for all components.
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
  int GetDevicesVersionFromAudio(std::map<int, std::string>& version_map);

  /**
   * @brief Gets name of each component in the composite device.
   * @param name Output device name.
   * @return kLogiErrorNoError if succeeded or error code if failed to get one
   * of the component versions.
   */
  int GetDeviceName(std::string& name);

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
  int GetImagesVersion(std::map<int, std::string>& version_map);

  /**
   * @brief Sets image buffer. It does not retain or copy the buffer. It
   * @brief Set image buffer. It does not retain or copy the buffer. It
   * @brief Grabs the firmware image's component firmware versions.
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
  int GetImagesVersionFromFile(std::map<int, std::string>& version_map);

  /**
   * @brief Sets image buffer. It does not retain or copy the buffer. It
   * replaces the old buffer if it's already existed in the map.
   * @param device_type The device type of the image. Use kLogiDeviceVideo,
   * kLogiDeviceEeprom... for this type.
   * @param buffer The image buffer to be set.
   */
  void SetImageBuffer(int device_type, std::vector<uint8_t> buffer);

  /**
   * @brief Set version text file for firmware. In case some firmware binary
   * does not have embedded version, the version file helps to get the version
   * of the bin file.
   * @param device_type The usb device type. Use kLogiDeviceVideo,
   * kLogiDeviceEeprom... for this type.
   * @param version_file The path to the version file.
   */
  void SetVersionFile(int device_type, const std::string& version_file);

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
   * @param updated True if did perform update on component, false otherwise.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  int PerformComponentUpdate(bool* updated);

  /**
   * @brief Adds a device to the composite device.
   * @param device_type Device type to add. Use kLogiDeviceAudio,
   * kLogiDeviceAudio, kLogiDeviceEeprom...
   * @param dfu_pid If device type is audio, provide DFU pid. By default, dfu
   * pid is not used for other devices.
   * @param pid Device product ID.
   */
  void AddDevice(int device_type, std::string pid, std::string dfu_pid = "");

  /**
   * @brief Sets secure header image buffer. It does not retain or copy the
   * buffer. It replaces the old buffer if existed already in the map.
   * @param device_type The type of the image buffer. Use kLogiDeviceAudio,
   * kLogiDeviceCodec...
   * @param enable If true, enable secure boot update.
   * @param secure_header The secure header image to set.
   */
  void SetSecureHeader(int device_type,
                       std::vector<uint8_t> secure_header,
                       bool enable);

  /**
   * @brief Sets support BLE to read and write BLE device during firmware
   * update.
   * @param support_ble If true, the device supports BLE.
   */
  void SetSupportBle(bool support_ble);

  /**
   * @brief Checks if the composite device has Logitech or Logicool firmware.
   * @param check_video If true, check Logicool firmware on video device.
   * @param check_audio If true, check Logicool firmware on audio device.
   * @param logicool True firmware is Logicool.
   * @return kLogiErrorNoError if checked ok, error code otherwise.
   */
  int IsLogicool(bool check_video, bool check_audio, bool& logicool);

  /**
   * @brief Writes Logicool/tech info to EEPROM.
   * IMPORTANT: This should apply to LogiGroup only since the EEPROM address is
   * safe for Group.
   * @param logicool Write Logicool brand info if true. Logitech otherwise.
   * @return kLogiErrorNoError if wrote ok, error code otherwise.
   */
  int WriteBrandInfoToEeprom(bool logicool);

  /**
   * @brief Reads Logicool/tech info from EEPROM.
   * IMPORTANT: This should apply to LogiGroup only since the EEPROM address is
   * safe for Group.
   * @param logicool True if Logicool brand.
   * @return kLogiErrorNoError if read ok, error code otherwise.
   */
  int ReadBrandInfoFromEeprom(bool& logicool);

  /**
   * @brief Performs audio firmware update when the device is in the DFU mode.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  int PerformDfuAudioUpdate();

  /**
   * @brief Resets the usb hub (Logi Group long cable). Re-power the usb hub
   * and re-insert it.
   * @param vid Usb hub vendor id.
   * @param pid Usb hub product id.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int ResetUsbHub(std::string vid, std::string pid);

  /**
   * @brief Resets the device using TDE mode.
   * @param pid Logitech usb product id.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int PowerCycleTDEMode(std::string pid);

  /**
   * @brief Updates the firmware for the entire system.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int PerformSystemUpdate();

  /**
   * @brief Tracks the indivdual update progress of components in the system.
   * @param audio The audio device.
   * @param tablehub The tablehub device.
   * @param topology System components' information.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int CheckComponentsUpdated(const std::shared_ptr<AudioDevice>& audio,
                             const std::shared_ptr<TableHubDevice>& tablehub,
                             SystemTopology& topology);

  /**
   * @brief Grabs the number of components that are comprised in the system.
   * @param count Number of components.
   * @param topology System components' information.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int GetComponentCount(int& count, const SystemTopology& topology);

  /**
   * @brief Check to see if MCU needs update or not.
   * New Video firmware includes MCU image and update/reset MCU automatically
   * @param skip_update True if update needed, False otherwise.
   * @param device_name Name of the composite device.
   * @return kLogiErrorNoError if no error, error code otherwise.
   */
  int CheckMCUUpdate(bool* skip_update, std::string device_name);
};
#endif /* SRC_COMPOSITE_DEVICE_H_ */
