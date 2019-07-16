// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TABLEHUB_DEVICE_H_
#define SRC_TABLEHUB_DEVICE_H_

#include <stdio.h>
#include <libusb.h>
#include <map>
#include "usb_device.h"

// filepath to image's component firmware versions.
const char kTableHubVersionFilePath[] =
    "/lib/firmware/logitech/rally/versions.bin";
// max length of version string.
constexpr int kVersionLength = 16;
// default version string for tablehub.
const char kDefaultVersionString[] = "7.1.002";
// search strings for versions file.
const char kTableHubSearchString[] = "Bolide_TableHub";
const char kBleSearchString[] = "Bolide_BLE";
const char kAudioBleSearchString[] = "Bolide_Audio_BLE";
const char kVideoBleSearchString[] = "Bolide_Video_BLE";
const char kMicPodSearchString[] = "Bolide_MicPod";
const char kSplitterSearchString[] = "Bolide_Splitter";
const char kAudioSearchString[] = "Bolide_TVHub";
const char kVideoSearchString[] = "Bolide_VideoBuild";
const char kEepromSearchString[] = "Bolide_Eeprom";

enum ImageState { img_new, img_valid, img_invalid };

struct LogiBulkCmd {
  uint16_t id;
  uint16_t status;
  uint32_t len;
};

struct DownloadPacket {
  LogiBulkCmd cmd;
  char fw_version[kVersionLength];
};

struct ProgressInfo {
  uint32_t percentage;
};

struct DeviceFwInfo {
  char fw_version[kVersionLength];
  enum ImageState img_state;
};

struct TableHubVersionsData {
  DeviceFwInfo current_image_data;
  DeviceFwInfo older_image_data;
};

/**
 * Logitech tablehub device class to handle updates for all components in the
 * rally system. Follows the libusb protocol to transfer firmware binaries to
 * tablehub device/read CPI (image version information) from tablehub device.
 * When itb image file is transferred to the device, the components that are
 * checked/updated are:
 * 1.) tablehub
 * 2.) TVhub (audio)
 * 3.) Micpods
 * 4.) Splitters
 * 5.) Eeprom
 * 6.) BLE
 * 7.) Camera (video)
 */
class TableHubDevice : public USBDevice {
 private:
  struct libusb_device_handle* device_handle_;
  bool is_interface_open_;
  std::string device_fw_version_;

 protected:
  TableHubDevice(std::string pid, int type);

 public:
  TableHubDevice(std::string pid);

  virtual ~TableHubDevice();

  virtual bool IsPresent();
  virtual int OpenDevice();
  virtual void CloseDevice();
  virtual int RebootDevice();
  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int IsLogicool(bool* is_logicool);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int GetDeviceName(std::string* device_name);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);

 public:
  /**
   * @brief Once the bulk transferring of the itb image file completes, this
   * call begins the firmware update process for the tablehub device. Constantly
   * polls the device to get progress information.
   * @return kLogiErrorNoError if client was able to poll the device ok, error
   * code otherwise.
   */
  int GetProgress();

  /**
   * @brief Sets the tablehub device instance's fw version attribute.
   * @param version The firmware version to be set to.
   * @return kLogiErrorNoError if version string ok, error code otherwise.
   */
  int SetDeviceVersion(std::string version);

  /**
   * @brief Grabs the itb image's firmware version for each component
   * @param versions The version of each component.
   * @return kLogiErrorNoError if grabs the versions ok, error otherwise.
   */
  int GetAllComponentImageVersions(
      std::map<std::string, std::string>* versions);

 private:
  /**
   * @brief Sends the image to device 512 bytes at a time using libusb protocol.
   * @param data_buffer Byte buffer to be transferred over to the device.
   * @return kLogiErrorNoError if image transferred ok, error code otherwise.
   */
  int SendImage(std::vector<uint8_t> data_buffer);

  /**
   * @brief Libusb bulk transfer to the out endpoint of the bulk interface.
   * @param data Bytes to be transferred to endpoint.
   * @param size Size of the data being transferred.
   * @return kLogiErrorNoError if data transferred ok, error code otherwise.
   */
  int BulkTransferOut(uint8_t* data, int size);

  /**
   * @brief Libusb bulk transfer to the in endpoint of the bulk interface.
   * @param data Bytes to be transferred to endpoint.
   * @param size Size of the data being transferred.
   * @return kLogiErrorNoError if data transferred ok, error code otherwise.
   */
  int BulkTransferIn(uint8_t* data, int size);

  /**
   * @brief Read CPI from tablehub device to grab firmware image version
   * present on the device. Uses libusb bulk transfer protocol.
   * @param hw_version The hardware version of the device.
   * @return kLogiErrorNoError if CPI read ok, error code otherwise.
   */
  int ReadCPIData(std::string* hw_version);

  /**
  * @brief Wait for the tablehub to reboot after update.
  */
  void WaitForReboot();
};
#endif /* SRC_TABLEHUB_DEVICE_H_ */
