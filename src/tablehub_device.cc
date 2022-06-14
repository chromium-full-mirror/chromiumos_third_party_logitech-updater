// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tablehub_device.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "base/strings/string_util.h"
#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>
#include <libusb.h>
#include <stdio.h>

#include "utilities.h"

// device endpoints for bulk interface
constexpr int kEndpointOut = 0x01;
constexpr int kEndpointIn = 0x81;
// commands (sent to/received by) device upon initialization
constexpr uint16_t kInitStatusRequestCmd = 0xBEEF;
constexpr uint16_t kInitIdCmd = 0xCC01;
constexpr uint16_t kInitStatusRebootAck = 0xBBBB;
constexpr uint16_t kInitStatusRequestAck = 0x999;
// command (sent to/received by) device for downloads
constexpr uint16_t kDownloadIdCmd = 0xCC03;
// command (sent to/received by) device to read cpi
constexpr uint16_t kReadCpiIdCmd = 0xCC07;
constexpr uint16_t kStatusRequestCmd = 0xFFFF;
// default bulk transfer timeout
constexpr int kDefaultTimeout = 3000;
// bulk transfer max buffer length
constexpr int kMaxBufferLength = 512;
constexpr int kDefaultRetries = 5;
const std::string kDevicePid = "088f";
const std::string kDeviceVid = "046d";
const std::string kPB3 = "PB3";
const std::string kPB2 = "PB2";
constexpr int kProgressCompleted = 100;
constexpr int kLibusbNoDeviceError = -4;
constexpr int kLibusbTimeoutError = -7;
constexpr int kTablehubRebootTimeoutInSec = 600;
constexpr int kTablehubRebootSleepTimeInSec = 10;
constexpr uint32_t kHubUpdaterTimeoutSeconds = 1680;
constexpr char kLogiHubDeviceName[] = "Logitech Hub";

TableHubDevice::TableHubDevice(std::string pid)
    : USBDevice(pid, kLogiDeviceTableHub), is_interface_open_(false) {
  libusb_init(NULL);
}

TableHubDevice::TableHubDevice(std::string pid, int type)
    : USBDevice(pid, type) {
  libusb_init(NULL);
}

TableHubDevice::~TableHubDevice() {}

int TableHubDevice::OpenDevice() {
  std::vector<std::string> bus_paths =
      FindUsbBus(kLogiVendorIdString, usb_pid_);
  if (bus_paths.size() > 1) {
    LOG(ERROR) << "Failed to open device. Multiple devices found.";
    return kLogiErrorMultipleDevicesFound;
  }
  int error = kLogiErrorNoError;
  int vid;
  ConvertHexStringToInt(kLogiVendorIdString, &vid);
  int pid;
  ConvertHexStringToInt(usb_pid_, &pid);
  // grab device handle by vid/pid.
  device_handle_ = libusb_open_device_with_vid_pid(NULL, vid, pid);
  if (device_handle_ == NULL) {
    is_open_ = false;
    LOG(ERROR) << "Failed to open device for Rally.";
    return kLogiErrorOpenDeviceFailed;
  }
  is_open_ = true;
  // claim the bulk interface.
  error = libusb_claim_interface(device_handle_, 0);
  if (error != kLogiErrorNoError) {
    LOG(ERROR) << "Failed to claim bulk interface for device.";
    return kLogiErrorClaimInterfaceFailed;
  }
  is_interface_open_ = true;
  LogiBulkCmd cmd;
  int retries = kDefaultRetries;
  // check to see if client receives ACK to verify proper
  // connection is established.
  while (retries > 0) {
    retries--;
    cmd.len = 0;
    cmd.status = kInitStatusRequestCmd;
    cmd.id = kInitIdCmd;
    error = BulkTransferOut((uint8_t*)&cmd, sizeof(LogiBulkCmd));
    if (error != kLogiErrorNoError) {
      LOG(ERROR) << "Failed to send ACK to device.";
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultTimeout));
    error = BulkTransferIn((uint8_t*)&cmd, sizeof(LogiBulkCmd));
    if (error != kLogiErrorNoError) {
      LOG(ERROR) << "Failed to receive ACK from device.";
      continue;
    }

    if (cmd.id == kInitIdCmd && cmd.status == kInitStatusRebootAck) {
      LOG(ERROR) << "Rally System needs reboot.";
      break;
    } else if (cmd.id == kInitIdCmd && cmd.status == kInitStatusRequestAck) {
      break;
    }
  }
  return error;
}

void TableHubDevice::CloseDevice() {
  if (is_interface_open_) {
    libusb_release_interface(device_handle_, 0);
  }
  if (is_open_) {
    libusb_close(device_handle_);
  }
  is_interface_open_ = false;
  is_open_ = false;
}

int TableHubDevice::RebootDevice() {
  return kLogiErrorDeviceRebootFailed;
}

int TableHubDevice::BulkTransferOut(uint8_t* data, int size) {
  if (!is_open_ || !is_interface_open_) {
    LOG(ERROR) << "Device or interface not open to perform bulk transfer.";
    return kLogiErrorDeviceNotOpen;
  }
  int error = kLogiErrorNoError;
  int sent;
  int success = libusb_bulk_transfer(device_handle_, kEndpointOut, data, size,
                                     &sent, kDefaultTimeout);
  if (success != 0) {
    if (success == kLibusbNoDeviceError)
      error = kLogiErrorDeviceNotPresent;
    else if (success == kLibusbTimeoutError)
      error = kLogiErrorTimeout;
    else
      error = kLogiErrorIOControlOperationFailed;
  }
  return error;
}

int TableHubDevice::BulkTransferIn(uint8_t* data, int size) {
  if (!is_open_ || !is_interface_open_) {
    LOG(ERROR) << "Device or interface not open to perform bulk transfer.";
    return kLogiErrorDeviceNotOpen;
  }
  int error = kLogiErrorNoError;
  int sent;
  int success = libusb_bulk_transfer(device_handle_, kEndpointIn, data, size,
                                     &sent, kDefaultTimeout);
  if (success != 0) {
    if (success == kLibusbNoDeviceError)
      error = kLogiErrorDeviceNotPresent;
    else if (success == kLibusbTimeoutError)
      error = kLogiErrorTimeout;
    else
      error = kLogiErrorIOControlOperationFailed;
  }
  return error;
}

int TableHubDevice::SendImage(std::vector<uint8_t> data_buffer) {
  if (!is_open_ || !is_interface_open_) {
    LOG(ERROR) << "Device or interface not open to send image.";
    return kLogiErrorDeviceNotOpen;
  }
  int error = kLogiErrorNoError;
  int bytes_sent = 0;
  int to_send = data_buffer.size();
  // sends 512 byte chunks (or less if remaining bytes at the end) to device
  while (bytes_sent != to_send) {
    int length = (to_send - bytes_sent < kMaxBufferLength)
                     ? to_send - bytes_sent
                     : kMaxBufferLength;
    error = BulkTransferOut(&data_buffer[bytes_sent], length);
    if (error) {
      LOG(ERROR) << "Download failed.";
      error = kLogiErrorSendImageFailed;
      break;
    }
    bytes_sent += length;
  }
  return error;
}

bool TableHubDevice::IsPresent() {
  std::vector<std::string> paths;
  paths = FindUsbBus(kDeviceVid, kDevicePid);
  if (paths.size() > 0)
    return true;
  return false;
}

// table hub device version can only be queried through the audio device, so
// this instance parameter needs to be set ahead of time or will return an error
int TableHubDevice::ReadDeviceVersion(std::string* device_version) {
  if (device_fw_version_.length() == 0)
    return kLogiErrorInvalidDeviceVersionString;
  *device_version = device_fw_version_;
  return kLogiErrorNoError;
}

int TableHubDevice::IsLogicool(bool* is_logicool) {
  return 0;
}

// Verification is done at the beginning with encrypted signiture files
int TableHubDevice::VerifyImage(std::vector<uint8_t> buffer) {
  return kLogiErrorNoError;
}

int TableHubDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                    std::string* image_version) {
  int error = kLogiErrorImageVersionNotFound;
  std::string line;
  std::ifstream file;
  file.open(kTableHubVersionFilePath);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open file.";
    return kLogiErrorImageVersionNotFound;
  }
  while (!file.eof()) {
    getline(file, line);
    if (line.find(kTableHubSearchString) != std::string::npos) {
      int start_index = line.find("-");
      *image_version = line.substr(start_index + 1);
      error = kLogiErrorNoError;
      break;
    }
  }
  return error;
}

int TableHubDevice::GetAllComponentImageVersions(
    std::map<std::string, std::string>* versions) {
  std::string line;
  std::ifstream file;
  file.open(kTableHubVersionFilePath);
  if (!file.is_open()) {
    LOG(ERROR) << "Failed to open file.";
    return kLogiErrorImageVersionNotFound;
  }
  std::string version;
  while (!file.eof()) {
    getline(file, line);
    int index = line.find("-");
    if (line.length() > 0 && index == std::string::npos) {
      LOG(ERROR) << "Incorrect format of version file.";
      return kLogiErrorImageVersionNotFound;
    }
    version = line.substr(index + 1);
    if (line.find(kTableHubSearchString) != std::string::npos) {
      versions->insert({kTableHubSearchString, version});
    } else if (line.find(kBleSearchString) != std::string::npos) {
      versions->insert({kAudioBleSearchString, version});
      versions->insert({kVideoBleSearchString, version.erase(1, 2)});
    } else if (line.find(kMicPodSearchString) != std::string::npos) {
      versions->insert({kMicPodSearchString, version});
    } else if (line.find(kSplitterSearchString) != std::string::npos) {
      versions->insert({kSplitterSearchString, version});
    } else if (line.find(kAudioSearchString) != std::string::npos) {
      versions->insert({kAudioSearchString, version});
    } else if (line.find(kVideoSearchString) != std::string::npos) {
      versions->insert({kVideoSearchString, version});
    } else if (line.find(kEepromSearchString) != std::string::npos) {
      versions->insert({kEepromSearchString, version});
    }
  }
  return kLogiErrorNoError;
}

int TableHubDevice::PerformUpdate(std::vector<uint8_t> buffer,
                                  std::vector<uint8_t> secure_header,
                                  bool* did_update) {
  *did_update = false;
  if (!is_open_ || !is_interface_open_) {
    return kLogiErrorDeviceNotOpen;
  }

  int error = kLogiErrorNoError;
  LOG(INFO) << "Checking table hub firmware...";
  bool should_update;
  error = CheckForUpdate(buffer, &should_update);
  if (error || !should_update) {
    return error;
  }
  LOG(INFO) << "Table hub firmware is not up to date. Sending "
               "image to device...";
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(kLogiHubDeviceName);
  dfu_notification->NotifyStartUpdate(kHubUpdaterTimeoutSeconds);
  std::string hw_version;
  error = ReadCPIData(&hw_version);
  if (error) {
    LOG(ERROR) << "Failed to read CPI data";
    return error;
  }
  struct DownloadPacket pkt;
  pkt.cmd.id = kDownloadIdCmd;
  pkt.cmd.status = kStatusRequestCmd;
  pkt.cmd.len = buffer.size();
  std::string image_version;
  error = GetImageVersion(buffer, &image_version);
  if (error) {
    LOG(ERROR) << "Failed to grab image version. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return kLogiErrorVerifyImageVersionFailed;
  }
  if (image_version.length() > kVersionLength) {
    LOG(ERROR) << "Version Length is incorrect. Error: "
               << kLogiErrorVerifyImageVersionFailed;
    dfu_notification->NotifyEndUpdate(false);
    return kLogiErrorVerifyImageVersionFailed;
  }
  if (hw_version.compare(kPB3) == 0)
    strcpy(pkt.fw_version, image_version.c_str());
  else
    strcpy(pkt.fw_version, kDefaultVersionString);

  // handshake before sending image file for download. Make sure to
  // receive ACK before sending image.
  error = BulkTransferOut((uint8_t*)&pkt, sizeof(DownloadPacket));
  if (error) {
    LOG(ERROR) << "Failed to send request for download. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  memset(&pkt, '\0', sizeof(DownloadPacket));
  error = BulkTransferIn((uint8_t*)&pkt, sizeof(DownloadPacket));
  if (error) {
    LOG(ERROR) << "Failed to receive acknowlegement for download. Error: "
               << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  if (pkt.cmd.id != kDownloadIdCmd && (pkt.cmd.status)) {
    LOG(ERROR) << "Acknowlegement received is incorrect. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  error = SendImage(buffer);
  if (error) {
    LOG(INFO) << "Failed to send image to device. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  *did_update = true;
  LOG(INFO) << "Successfully sent image to table hub.";
  dfu_notification->NotifyEndUpdate(true);
  return error;
}

int TableHubDevice::GetProgress() {
  LOG(INFO) << "Will wait for tablehub update to complete.";
  int error = kLogiErrorNoError;
  struct ProgressInfo progress;
  do {
    error = BulkTransferIn((uint8_t*)&progress, sizeof(ProgressInfo));
    if (error == kLogiErrorDeviceNotPresent) {
      LOG(ERROR) << "Failed to get progress.";
      return kLogiErrorIOControlOperationFailed;
    }
  } while (progress.percentage != kProgressCompleted);
  LOG(INFO) << "Tablehub update completed.  Will wait for device to reboot.";
  // Wait additional 7 mins for valens update
  WaitForReboot();
  return kLogiErrorNoError;
}

// Currently no means of querying table hub for device name.
int TableHubDevice::GetDeviceName(std::string* device_name) {
  return kLogiErrorDeviceNameNotAvailable;
}

int TableHubDevice::SetDeviceVersion(std::string version) {
  int major, minor, build;
  bool isVersion = GetDeviceVersionNumber(version, &major, &minor, &build);
  if (!isVersion)
    return kLogiErrorInvalidDeviceVersionString;
  device_fw_version_ = version;
  return kLogiErrorNoError;
}

int TableHubDevice::ReadCPIData(std::string* hw_version) {
  int error = kLogiErrorNoError;

  struct LogiBulkCmd cmd;
  cmd.id = kReadCpiIdCmd;
  cmd.status = kStatusRequestCmd;
  cmd.len = 0;
  error = BulkTransferOut((uint8_t*)&cmd, sizeof(LogiBulkCmd));
  if (error)
    return error;

  struct TableHubVersionsData ver_data;
  memset(&ver_data, '\0', sizeof(TableHubVersionsData));
  int actual;
  int success = libusb_bulk_transfer(
      device_handle_, kEndpointIn, (uint8_t*)&ver_data,
      sizeof(TableHubVersionsData), &actual, kDefaultTimeout);
  // CPI data sructure different depending on hardware version.
  if (success != 0 || (actual != sizeof(TableHubVersionsData) &&
                       actual != sizeof(DeviceFwInfo)))
    return kLogiErrorIOControlOperationFailed;
  *hw_version = (actual == sizeof(TableHubVersionsData))
                    ? kPB3
                    : ((actual == sizeof(DeviceFwInfo)) ? kPB2 : "");
  return error;
}

void TableHubDevice::WaitForReboot() {
  int duration = 0;
  while (duration < kTablehubRebootTimeoutInSec) {
    std::this_thread::sleep_for(
        std::chrono::seconds(kTablehubRebootSleepTimeInSec));
    duration += kTablehubRebootSleepTimeInSec;
  }
}
