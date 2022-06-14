// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_device.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "utilities.h"

const char kDefaultAudioDfuModeVersion[] = "1.0.000";
// HID report number to get device topology.
constexpr int kLogiReportNumberGetSystemTopology = 0x3E;
// HID report number to set device into info querying mode in DFU.
constexpr int kLogiReportNumberSetInfoInDfuMode = 0x02;
// HID report number to set data in DFU mode.
constexpr int kLogiReportNumberSetDataInDfuMode = 0x03;
// HID report data to set for getting info.
constexpr int kLogiReportDataSetGetInfo = 0x04;
// HID report data to set for getting DFU version info.
constexpr int kLogiReportDataSetGetDfuVersion = 0x11;
// HID report data to set for getting DFU version in DFU mode.
constexpr int kLogiReportDataSetGetDfuVersionInDfuMode = 0x07;
// HID report data to get info in DFU mode.
constexpr int kLogiReportNumberGetInfoInDfuMode = 0x01;
// HID report data to get Logicool info.
constexpr int kLogiReportNumberSetGetLogicoolInfo = 0x16;
// HID report data to set device to DFU safe mode.
constexpr int kLogiReportDataSetDfuModeSafe = 0x10;
// HID report data to set device to DFU simple mode.
constexpr int kLogiReportDataSetDfuModeSimple = 0x0A;
// HID report data to reset device in DFU mode.
constexpr int kLogiReportDataResetDevice = 0x0F;
// HID report data to erase flash in DFU mode.
constexpr int kLogiReportDataDfuEraseFlash = 0x01;
// HID report data to set file size in DFU mode.
constexpr int kLogiReportDataDfuSetFileSize = 0x06;
// HID report data to set device in download mode after DFU mode.
constexpr int kLogiReportDataDfuSetDownloadMode = 0x02;
// HID report data to set device to verify image in DFU mode.
constexpr int kLogiReportDataDfuModeVerifyImage = 0x05;
// HID report data to reset device in DFU mode.
constexpr int kLogiReportDataDfuModeResetDevice = 0x04;
// Least significant byte address for build number.
constexpr int kLogiAudioImgBuildLSBAddress = 0x3000;
// Most significant byte address for build number.
constexpr int kLogiAudioImgBuildMSBAddress = 0x3001;
// Address to read minor version data from.
constexpr int kLogiAudioImgMinorVersionAddress = 0x3002;
// Address to read major version data from.
constexpr int kLogiAudioImgMajorVersionAddress = 0x3003;
// Minimum build version that supports DFU API query.
constexpr int kLogiAudioDfuGetVersionMinBuild = 400;
constexpr int kLogiEnterDfuMaxRetries = 3;
// Interval for the device to retry entering DFU mode.
constexpr int kLogiEnterDfuRetryIntervalMs = 1000;
// Timeout to try re-opening device.
constexpr int kLogiAudioRetryOpeningDeviceTimeoutMs = 60000;
// Retry to open device interval.
constexpr int kLogiAudioRetryOpeningDeviceIntervalMs = 1000;
constexpr int kLogiDfuModeEraseFlashDelayMs = 5;
constexpr int kLogiDfuModeSafeTemporaryErrorData = 0xFF;
constexpr int kLogiDfuModeSafeValidData = 0x88;
constexpr int kLogiDfuModeUpdateMaxRetries = 5;
constexpr unsigned int kLogiAudioMaxBinaryFileSize = 0x00FFFFFF;
// 64 data bytes block.
constexpr int kLogiDfuDataBlockDataSize = 64;
// 2 bytes for header.
constexpr int kLogiDfuDataHeaderSize = 2;
// 2 header bytes & 64 data bytes.
constexpr int kLogiDfuDataBlockTotalSize =
    kLogiDfuDataBlockDataSize + kLogiDfuDataHeaderSize;
constexpr int kLogiDfuModeGetStatusTimeoutMs = 10000;
constexpr int kLogiDfuModeGetStatusIntervalMs = 30;
constexpr int kLogiDfuModeStatusDownloadBusy = 0x04;
constexpr int kLogiDfuModeStatusInvalidAuth = 0x0B;
constexpr int kLogiBleDataBlockSize = 64;
constexpr int kLogiHidReportBleNameAppGetCmd = 0x41;
constexpr int kLogiHidReportBleNameAppSetCmd = 0x50;
constexpr int kLogiHidReportBleNameAppSetData = 0x0A;
constexpr int kLogiBleDataDeviceNameStart = 35;
constexpr int kLogiBleDataDeviceNameSize = 29;
constexpr int kTopologyBufferSize = 513;
constexpr int kTopologyStartIndex = 4;
constexpr int kTopologyMaxElements = 15;

constexpr int kVersionInfoRowNum = 5;
constexpr int kVersionInfoColNum = 2;
constexpr int kVersionInfoOffset = 3;

//  These constants are addresses and values of signature which is embedded in
//  the firmware binary.
//  0x3004: 0x04.
//  0x3005: 0x03.
//  0x3006: 0x02.
//  0x3007: 0x01.
//  Starting from first sig address and value, loop through and check
//  all signatures until hit the signature size.
constexpr int kLogiAudioImageSigAddressStart = 0x3004;
constexpr int kLogiAudioImageSigValueStart = 0x04;
constexpr int kLogiAudioImageSigSize = 4;

AudioDevice::AudioDevice(std::string pid, int type)
    : USBDevice(pid, type), dfu_pid_(""), is_dfu_mode_(false) {}

AudioDevice::AudioDevice(std::string pid, std::string dfu_pid)
    : USBDevice(pid, kLogiDeviceAudio),
      dfu_pid_(dfu_pid),
      is_dfu_mode_(false) {}

AudioDevice::~AudioDevice() {}

bool AudioDevice::IsPresent() {
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, usb_pid_);
  if (dev_paths.empty()) {
    // Falls back to find device DFU mode.
    dev_paths = FindDevices(kDefaultAudioDeviceMountPoint,
                            kDefaultAudioDevicePoint, dfu_pid_);
  }
  return (dev_paths.size() > 0) ? true : false;
}

int AudioDevice::OpenDevice() {
  int error = OpenDeviceInHidMode();
  if (error)
    error = OpenDeviceInDfuMode();

  return error;
}

int AudioDevice::OpenDeviceInHidMode() {
  int error = OpenDeviceWithPid(usb_pid_);
  if (error == kLogiErrorNoError)
    is_dfu_mode_ = false;
  return error;
}

int AudioDevice::OpenDeviceInDfuMode() {
  int error = OpenDeviceWithPid(dfu_pid_);
  if (error == kLogiErrorNoError)
    is_dfu_mode_ = true;
  return error;
}

int AudioDevice::OpenDeviceWithPid(std::string usb_pid) {
  if (usb_pid.empty()) {
    LOG(ERROR) << "Failed to open device. USB PID is not set.";
    return kLogiErrorUsbPidNotFound;
  }
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, usb_pid);
  if (dev_paths.empty())
    return kLogiErrorUsbPidNotFound;
  if (dev_paths.size() > 1) {
    LOG(ERROR) << "Failed to open device. Multiple device found.";
    return kLogiErrorMultipleDevicesFound;
  }
  int fd = open(dev_paths.at(0).c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd == -1) {
    LOG(ERROR) << "Failed to open device. Open file failed.";
    is_open_ = false;
    is_dfu_mode_ = false;
    return kLogiErrorOpenDeviceFailed;
  }
  is_open_ = true;
  file_descriptor_ = fd;
  return kLogiErrorNoError;
}

int AudioDevice::GetDeviceName(std::string* device_name) {
  return kLogiErrorDeviceNameNotAvailable;
}

int AudioDevice::RebootDevice() {
  // Device is cycle powered from the video chip only. Calling this method on
  // audio device returns reboot error kLogiErrorDeviceRebootFailed.
  return kLogiErrorDeviceRebootFailed;
}

int AudioDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  // In DFU mode, returns version 1.0.000 as specs.
  if (is_dfu_mode_) {
    *device_version = kDefaultAudioDfuModeVersion;
    return kLogiErrorNoError;
  }

  std::vector<uint8_t> vers_data;
  int error = SendCommandInHidMode(kLogiReportDataSetGetInfo, &vers_data);
  if (error)
    return error;
  // Reported version data is little-endian.
  int major = vers_data[4];
  int minor = vers_data[3];
  int build = ((vers_data[2]) << 8) | vers_data[1];
  *device_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int AudioDevice::VerifyImage(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  if (buffer.size() > kLogiAudioMaxBinaryFileSize)
    return kLogiErrorImageVersionExceededMaxSize;

  // Checks image's secure signature. The audio's image signature is embedded in
  // the firmware binary as follow (Address: Value):
  // 0x3004: 0x04
  // 0x3005: 0x03
  // 0x3006: 0x02
  // 0x3007: 0x01
  if (buffer.size() < kLogiAudioImageSigAddressStart + kLogiAudioImageSigSize)
    return kLogiErrorImageVerificationFailed;
  int address = kLogiAudioImageSigAddressStart;
  int value = kLogiAudioImageSigValueStart;
  for (int i = 0; i < kLogiAudioImageSigSize; i++) {
    if (buffer[address] != value)
      return kLogiErrorImageVerificationFailed;
    address++;
    value--;
  }
  return kLogiErrorNoError;
}

int AudioDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                 std::string* image_version) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Image version info starts with the fixed offset at address 0x3000.
  // 0x3000: Build number (Least Significant Byte).
  // 0x3001: Build number (Most Significant Byte).
  // 0x3002: Minor version.
  // 0x3003: Major version.
  if (buffer.size() <= kLogiAudioImgMajorVersionAddress)
    return kLogiErrorImageVersionSizeTooShort;

  uint8_t major = buffer[kLogiAudioImgMajorVersionAddress];
  uint8_t minor = buffer[kLogiAudioImgMinorVersionAddress];
  uint8_t least_sig_byte = buffer[kLogiAudioImgBuildLSBAddress];
  uint16_t most_sig_byte =
      static_cast<uint16_t>(buffer[kLogiAudioImgBuildMSBAddress]) << 8;
  uint16_t build = most_sig_byte | least_sig_byte;
  *image_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int AudioDevice::PrepareForUpdate(std::vector<uint8_t> buffer,
                                  bool* should_update) {
  *should_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Check for the devices with same DFU pid that are
  // currently in DFU mode. There are 2 cases:
  // 1. If the device is in DFU mode and found more than 1 (count 1 as itself),
  // bail out.
  // 2. If the device is NOT in DFU mode and found any others, bail out.
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, dfu_pid_);
  int dfu_devices = dev_paths.size();
  if ((is_dfu_mode_ && dfu_devices > 1) || (!is_dfu_mode_ && dfu_devices > 0)) {
    std::string msg = "Multiple devices in DFU mode found. DFU devices found: ";
    if (!is_dfu_mode_)
      msg = "There are other device(s) in DFU mode. DFU devices found: ";
    LOG(ERROR) << msg.c_str() << dfu_devices;
    return kLogiErrorMultipleDevicesDfuModeFound;
  }

  // Checks if the device is already in DFU mode. Should always perform
  // firmware update when device is in DFU mode.
  if (is_dfu_mode_) {
    LOG(INFO) << "Device is in DFU mode.";
    *should_update = true;
    return kLogiErrorNoError;
  }
  LOG(INFO) << "Checking audio firmware...";
  int error = CheckForUpdate(buffer, should_update);
  if (error || !(*should_update))
    return error;

  LOG(INFO) << "Firmware is not up to date. Setting device to DFU mode...";
  // Sets device into Device Firmware Update mode.
  error = SetDfuMode();
  if (error) {
    LOG(ERROR) << "Failed to set device to DFU mode. Error: " << error;
  } else {
    *should_update = true;
    LOG(INFO) << "Successfully set device into DFU mode.";
  }

  return error;
}

int AudioDevice::PerformUpdate(std::vector<uint8_t> buffer,
                               std::vector<uint8_t> secure_header,
                               bool* did_update) {
  *did_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  bool should_update;
  int error = PrepareForUpdate(buffer, &should_update);
  if (error)
    return error;
  if (!should_update) {
    LOG(INFO) << "Audio firmware is up to date.";
    return kLogiErrorNoError;
  }

  LOG(INFO) << "Audio firmware is not up to date. Updating firmware...";

  // Note: GetDeviceName here will return kLogiErrorDeviceNameNotAvailable
  std::string device_name = "Logitech Camera Audio";
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(device_name);
  dfu_notification->NotifyStartUpdate();

  // Read BLE name before updating audio firmware if supported.
  int read_ble_error = kLogiErrorNoError;
  std::string ble_name;
  if (support_ble_)
    read_ble_error = ReadBleDeviceName(&ble_name);

  error = WaitForDfuDevice();
  if (error) {
    LOG(ERROR) << "Failed to wait for DFU device to come back. ";
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Check DFU version after device is in DFU mode.
  std::string dfu_version;
  error = GetDfuApiVersionInDfuMode(&dfu_version);
  if (error) {
    LOG(ERROR) << "Failed to read DFU API version in DFU mode. Error: "
               << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  // Update firmware in DFU mode. Attempts to retry this step if failed.
  for (int i = 0; i < kLogiDfuModeUpdateMaxRetries; i++) {
    error = UpdateFirmwareInDfuMode(buffer, secure_header);
    if (error == kLogiErrorNoError)
      break;
    LOG(INFO) << "Failed to update firmware. Attempt to retry one more time.";
  }
  if (error) {
    LOG(ERROR) << "Failed to update firmware, out of attempts. Error: "
               << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Verify sent image.
  error = VerifyDfuUpdateImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to verify updated image. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Reset to Audio/HID mode.
  error = ResetToHidMode();
  if (error) {
    LOG(ERROR) << "Failed to reset device to Audio/HID mode. Error: " << error;
  } else {
    LOG(INFO) << "Successfully updated audio firmware.";
    *did_update = true;
  }
  dfu_notification->NotifyEndUpdate(!error);

  // Write BLE name if supported.
  if (support_ble_ && read_ble_error == kLogiErrorNoError)
    WriteBleDeviceName(ble_name);
  return error;
}

int AudioDevice::GetDfuApiVersionInHidMode(std::string* dfu_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::string device_version;
  int error = GetDeviceVersion(&device_version);
  if (error)
    return error;

  // DFU API query feature is available with version 8.1.400 and higher only.
  // Earlier versions do not support this and return garbage data. In those
  // cases, return DFU API version 0.0 and success value.
  int major, minor, build;
  if (!GetDeviceVersionNumber(device_version, &major, &minor, &build))
    return kLogiErrorGetDfuApiVersionFailed;
  if (build < kLogiAudioDfuGetVersionMinBuild) {
    *dfu_version = "0.0";
    return kLogiErrorNoError;
  }

  std::vector<uint8_t> out_data;
  error = SendCommandInHidMode(kLogiReportDataSetGetDfuVersion, &out_data);
  if (error)
    return error;
  *dfu_version = GetDeviceStringVersion(out_data[2], out_data[1]);
  return kLogiErrorNoError;
}

int AudioDevice::GetDfuApiVersionInDfuMode(std::string* dfu_version) {
  if (!is_open_ || !is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  // Puts the device into querying state.
  std::vector<uint8_t> data = {kLogiReportNumberSetInfoInDfuMode,
                               kLogiReportDataSetGetDfuVersionInDfuMode, 0, 0,
                               0};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  // Queries info after it's ready to accept query commands.
  std::vector<uint8_t> query_data = {kLogiReportNumberGetInfoInDfuMode, 0, 0, 0,
                                     0};
  error = ioctl(file_descriptor_, HIDIOCGFEATURE(query_data.size()),
                query_data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  *dfu_version = GetDeviceStringVersion(query_data[2], query_data[1]);

  return kLogiErrorNoError;
}

int AudioDevice::SetDfuMode() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  int error = kLogiErrorNoError;
  if (!is_dfu_mode_) {
    // Different DFU API versions use different method to update the device.
    // DFU version 0.0 and 1.0 uses one-stage method (erase + reset).
    // DFU version >= 1.1 uses 2-stage method (erase, then reset).
    std::string dfu_version;
    error = GetDfuApiVersionInHidMode(&dfu_version);
    if (error)
      return error;
    int compare_dfu_version = CompareVersions(dfu_version, "1.1");
    if (compare_dfu_version >= 0) {
      error = SetDfuModeSafe();
    } else {
      error = SetDfuModeSimple();
    }
  }
  return error;
}

int AudioDevice::SetDfuModeSafe() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  int error = kLogiErrorNoError;
  // On some older machine or with a long cable, entering DFU mode in the first
  // try might fail. Based on stress testings and device specs, retry up to 3
  // times here to enter DFU mode.
  for (int i = 0; i < kLogiEnterDfuMaxRetries; i++) {
    error = TrySetDfuModeSafe();
    if (error == kLogiErrorNoError)
      break;
    // Enter DFU mode failed. Wait here a bit before trying again.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiEnterDfuRetryIntervalMs));
  }
  return error;
}

int AudioDevice::SetDfuModeSimple() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data = {kLogiReportNumberSetInfo,
                               kLogiReportDataSetDfuModeSimple, 0, 0, 0};

  // When setting device to DFU mode simple, the device is reset and returns
  // error -1 value. There is not really a way to check for real errors here.
  ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  return kLogiErrorNoError;
}

int AudioDevice::WaitForDfuDevice() {
  if (is_open_ && is_dfu_mode_)
    return kLogiErrorNoError;
  int duration = 0;
  int error = kLogiErrorWaitForDfuDeviceReopenFailed;
  while (duration < kLogiAudioRetryOpeningDeviceTimeoutMs) {
    error = OpenDeviceInDfuMode();
    if (error == kLogiErrorNoError)
      break;

    // Failed to open device in DFU mode, wait and try again.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiAudioRetryOpeningDeviceIntervalMs));
    duration += kLogiAudioRetryOpeningDeviceIntervalMs;
  }
  return error;
}

int AudioDevice::WaitForHidDevice() {
  if (is_open_ && !is_dfu_mode_)
    return kLogiErrorNoError;
  int duration = 0;
  int error = kLogiErrorWaitForHidDeviceReopenFailed;
  while (duration < kLogiAudioRetryOpeningDeviceTimeoutMs) {
    error = OpenDeviceInHidMode();
    if (error == kLogiErrorNoError)
      break;

    // Failed to open device in DFU mode, wait and try again.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiAudioRetryOpeningDeviceIntervalMs));
    duration += kLogiAudioRetryOpeningDeviceIntervalMs;
  }
  return error;
}

int AudioDevice::SendHeaderImage(uint8_t report_number,
                                 std::vector<uint8_t> buffer) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  int error = kLogiErrorNoError;
  int offset = 0;
  int remaining_size = buffer.size();
  while (remaining_size > 0) {
    std::vector<uint8_t> data(kLogiDfuDataBlockTotalSize);
    uint8_t block_size = static_cast<uint8_t>(
        std::min(remaining_size, kLogiDfuDataBlockDataSize));
    data[0] = report_number;
    data[1] = block_size;
    std::copy(buffer.cbegin() + offset, buffer.cbegin() + offset + block_size,
              data.begin() + kLogiDfuDataHeaderSize);
    error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
    if (error < 0)
      return kLogiErrorIOControlOperationFailed;

    offset += block_size;
    remaining_size -= block_size;

    // Delay checking for status to account for device with long cable.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiDfuModeEraseFlashDelayMs));
    error = CheckDeviceStatusAfterSendingData();
    if (error) {
      LOG(ERROR) << "Failed to check status after sending block data for "
                    "header image. Error: "
                 << error;
      return error;
    }
  }
  return kLogiErrorNoError;
}

int AudioDevice::UpdateFirmwareInDfuMode(std::vector<uint8_t> buffer,
                                         std::vector<uint8_t> secure_header) {
  if (!is_open_ || !is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;

  // Erase flash memory. If erasing flash fails (device busy), attempt to retry.
  int error = kLogiErrorNoError;
  for (int i = 0; i < kLogiDfuModeUpdateMaxRetries; i++) {
    error = EraseFlash();
    if (error == kLogiErrorNoError)
      break;
    LOG(INFO) << "Failed to erase flash memory. Attempt to try one more time.";
  }
  if (error) {
    LOG(ERROR) << "Failed to erase flash memory, out of attempts. Error: "
               << error;
    return error;
  }

  // Send image file size
  uint32_t buffer_size = static_cast<uint32_t>(buffer.size());
  error = SendImageFileSize(kLogiReportNumberSetInfoInDfuMode,
                            kLogiReportDataDfuSetFileSize, buffer_size);
  if (error) {
    LOG(ERROR) << "Failed to send image file size. Error: " << error;
    return error;
  }

  // Set device into download mode.
  error = SetDownloadMode(kLogiReportNumberSetInfoInDfuMode,
                          kLogiReportDataDfuSetDownloadMode);
  if (error) {
    LOG(ERROR) << "Failed to set device into DFU download mode. Error: "
               << error;
    return error;
  }

  // Send secure header image if enable.
  if (secure_boot_) {
    error = SendHeaderImage(kLogiReportNumberSetDataInDfuMode, secure_header);
    if (error) {
      LOG(ERROR) << "Failed to send header image. Error: " << error;
      return error;
    }
  }

  // Send binary image to the device.
  error = SendImage(kLogiReportNumberSetDataInDfuMode, buffer);
  if (error)
    LOG(ERROR) << "Failed to send image to device. Error: " << error;

  return error;
}

int AudioDevice::EraseFlash() {
  if (!is_open_ || !is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;

  int error = kLogiErrorNoError;
  // Erases the flash from first sector to last sector.
  std::vector<uint8_t> data = {kLogiReportNumberSetInfoInDfuMode,
                               kLogiReportDataDfuEraseFlash, 0, 0, 0};
  int first_sector = 3;
  int last_sector = 11;
  for (int sector = first_sector; sector <= last_sector; sector++) {
    data[2] = sector;
    error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
    if (error < 0)
      return kLogiErrorIOControlOperationFailed;
    // Gives some delay for the firmware to erase the flash sector.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiDfuModeEraseFlashDelayMs));
  }

  // Sends a zero sector number to indicate the end of erasing process.
  data[2] = 0;
  error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int AudioDevice::SendImageFileSize(uint8_t report_number,
                                   uint8_t report_data,
                                   uint32_t file_size) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  // Sets the data as little endian format.
  std::vector<uint8_t> data = {report_number, report_data,
                               static_cast<uint8_t>(file_size & 0xFF),
                               static_cast<uint8_t>((file_size >> 8) & 0xFF),
                               static_cast<uint8_t>((file_size >> 16) & 0xFF)};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int AudioDevice::SetDownloadMode(uint8_t report_number, uint8_t report_data) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data = {report_number, report_data, 0, 0, 0};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int AudioDevice::SendImage(uint8_t report_number, std::vector<uint8_t> buffer) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  int error = kLogiErrorNoError;
  int offset = 0;
  int remaining_size = buffer.size();

  while (remaining_size > 0) {
    // Divides the buffer into block data of 64 bytes each and sends to the
    // device.
    std::vector<uint8_t> data(kLogiDfuDataBlockTotalSize);
    uint8_t block_size = static_cast<uint8_t>(
        std::min(remaining_size, kLogiDfuDataBlockDataSize));
    data[0] = report_number;
    data[1] = block_size;
    std::copy(buffer.cbegin() + offset, buffer.cbegin() + offset + block_size,
              data.begin() + kLogiDfuDataHeaderSize);
    error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
    if (error < 0) {
      LOG(ERROR) << "Failed to send image data at offset " << offset
                 << ". Error: " << error;
      return kLogiErrorIOControlOperationFailed;
    }
    offset += block_size;
    remaining_size -= block_size;

    error = CheckDeviceStatusAfterSendingData();
    if (error) {
      LOG(ERROR) << "Failed to check status after sending block data. Error: "
                 << error;
      return error;
    }
  }
  // Sets zero data length block to indicate the end of data.
  std::vector<uint8_t> end_data(kLogiDfuDataBlockTotalSize);
  end_data[0] = report_number;
  error =
      ioctl(file_descriptor_, HIDIOCSFEATURE(end_data.size()), end_data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to set end block data. Error: " << error;
    return kLogiErrorIOControlOperationFailed;
  }
  return kLogiErrorNoError;
}

int AudioDevice::CheckDeviceStatusAfterSendingData() {
  // After sending the data block to the device, checks for status.
  // If IO control operation fails, wait and retry checking.
  // If returned status is invalid authentication, error has occurred,
  // returns error status right away.
  // If IO operation failed or returned status is download busy, wait and
  // retry checking.
  int duration = 0;
  int error = kLogiErrorNoError;
  while (duration < kLogiDfuModeGetStatusTimeoutMs) {
    std::vector<uint8_t> status_data = {kLogiReportNumberGetInfoInDfuMode, 0, 0,
                                        0, 0};
    int result = ioctl(file_descriptor_, HIDIOCGFEATURE(status_data.size()),
                       status_data.data());
    uint8_t status = static_cast<uint8_t>(status_data[1]);
    if (result < 0 || status == kLogiDfuModeStatusDownloadBusy) {
      error = kLogiErrorSendImageFailed;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kLogiDfuModeGetStatusIntervalMs));
      duration += kLogiDfuModeGetStatusIntervalMs;
    } else if (status == kLogiDfuModeStatusInvalidAuth) {
      LOG(ERROR) << "Failed to send image data. Invalid authentication.";
      return kLogiErrorSendImageFailed;  // Authentication failed.
    } else {
      error = kLogiErrorNoError;
      break;
    }
  }
  return error;
}

int AudioDevice::VerifyDfuUpdateImage(std::vector<uint8_t> buffer) {
  if (!is_open_ || !is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Computes and sends checksum to the device for verification. Upon success,
  // update the internal flag to Audio/HID mode upon reset.
  uint32_t checksum = 0;
  for (int i = 0; i < buffer.size(); i++)
    checksum += static_cast<uint8_t>(buffer[i]);
  std::vector<uint8_t> data = {kLogiReportNumberSetInfoInDfuMode,
                               kLogiReportDataDfuModeVerifyImage,
                               static_cast<uint8_t>(checksum & 0xFF),
                               static_cast<uint8_t>((checksum >> 8) & 0xFF),
                               static_cast<uint8_t>((checksum >> 16) & 0xFF)};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int AudioDevice::ResetToHidMode() {
  if (!is_open_ || !is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data = {kLogiReportNumberSetInfoInDfuMode,
                               kLogiReportDataDfuModeResetDevice, 0, 0, 0};

  // Do not check for IO returned error here because this commands reset the
  // the device immediately. The returned value is usually -1.
  ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  CloseDevice();
  return kLogiErrorNoError;
}

int AudioDevice::IsLogicool(bool* is_logicool) {
  // Logicool info can be queried on audio device in HID mode. If firmware is
  // Logicool, returned data is 1. Logitech firmware returns 0.
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (is_dfu_mode_)
    return kLogiErrorBrandCheckFailed;

  std::vector<uint8_t> data;
  int error = SendCommandInHidMode(kLogiReportNumberSetGetLogicoolInfo, &data);
  if (error) {
    LOG(ERROR) << "Failed to check brand. Failed to query brand info.";
    return error;
  }
  *is_logicool = (data[1] == 0x01) ? true : false;
  return kLogiErrorNoError;
}

int AudioDevice::ReadSystemTopology(SystemTopology* topology) {
  if (!is_open_ || is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data(kTopologyBufferSize);
  data[0] = kLogiReportNumberGetSystemTopology;
  int error = ioctl(file_descriptor_, HIDIOCGFEATURE(data.size()), data.data());
  if (error < 0) {
    return kLogiErrorIOControlOperationFailed;
  }
  int index = kTopologyStartIndex;
  for (int i = 0; i < kTopologyMaxElements; i++) {
    if (index >= data.size() || index + sizeof(DeviceInfo) > data.size())
      return kLogiErrorInvalidResultData;
    struct DeviceInfo device;
    memcpy(&device, &data[index], sizeof(DeviceInfo));
    if (device.id == kLogiTableHubId) {
      topology->tablehub = device;
    } else if (device.id >= kLogiMicpodIdStart &&
               device.id <= kLogiMicpodIdEnd) {
      topology->micpods.push_back(device);
    } else if (device.id >= kLogiSplitterIdStart &&
               device.id <= kLogiSplitterIdEnd) {
      topology->splitters.push_back(device);
    }
    index += sizeof(DeviceInfo);
  }
  uint32_t version_info[kVersionInfoRowNum][kVersionInfoColNum];
  index = sizeof(DeviceInfo) * kTopologyMaxElements + kVersionInfoOffset;
  if (index >= data.size() || index + sizeof(version_info) > data.size())
    return kLogiErrorInvalidResultData;
  memcpy(&version_info, &data[index], sizeof(version_info));
  std::string version;
  for (int i = 0; i < kVersionInfoRowNum; i++) {
    bool is_version = GetDeviceVersionFromInt(version_info[i][1], &version);
    if (!is_version)
      return kLogiErrorInvalidDeviceVersionString;
    switch (version_info[i][0]) {
      case 1:
        topology->audio_version = version;
        break;
      case 2:
        topology->audio_ble_version = version;
        break;
      case 3:
        topology->video_version = version;
        break;
      case 4:
        topology->video_eeprom_version = version.erase(1, 2);
        break;
      case 5:
        topology->video_ble_version = version.erase(1, 2);
        break;
    }
  }
  index += sizeof(DeviceInfo);
  if (index >= data.size())
    return kLogiErrorInvalidResultData;
  topology->left_speaker_connected = data[index];
  index += 1;
  if (index >= data.size())
    return kLogiErrorInvalidResultData;
  topology->right_speaker_connected = data[index];
  return kLogiErrorNoError;
}

int AudioDevice::ReadBleDeviceName(std::string* ble_name) {
  if (!is_open_ || is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data(kLogiBleDataBlockSize);
  data[0] = kLogiHidReportBleNameAppGetCmd;
  int error = ioctl(file_descriptor_, HIDIOCGFEATURE(data.size()), data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to read BLE name.";
    return kLogiErrorIOControlOperationFailed;
  }
  std::string name(
      data.cbegin() + kLogiBleDataDeviceNameStart,
      data.cbegin() + kLogiBleDataDeviceNameStart + kLogiBleDataDeviceNameSize);
  if (name.empty() || name.c_str() == NULL)
    return kLogiErrorInvalidBleNameSize;
  *ble_name = name;
  return kLogiErrorNoError;
}

int AudioDevice::WriteBleDeviceName(std::string ble_name) {
  if (!is_open_ || is_dfu_mode_)
    return kLogiErrorDeviceNotOpen;
  if (ble_name.empty())
    return kLogiErrorInvalidBleNameSize;
  std::vector<uint8_t> data(kLogiBleDataBlockSize);
  data[0] = kLogiHidReportBleNameAppSetCmd;
  data[1] = kLogiHidReportBleNameAppSetData;
  int len = ble_name.size();
  int command_size = 2;
  if (len > kLogiBleDataDeviceNameSize)
    len = kLogiBleDataDeviceNameSize;
  if (len > kLogiBleDataBlockSize - command_size)
    return kLogiErrorInvalidBleNameSize;
  std::vector<uint8_t> name(ble_name.c_str(), ble_name.c_str() + len);
  std::copy(std::begin(name), std::end(name), std::begin(data) + command_size);
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to write BLE name.";
    return kLogiErrorIOControlOperationFailed;
  }
  return kLogiErrorNoError;
}

int AudioDevice::SendCommandInHidMode(uint8_t report_number,
                                      std::vector<uint8_t>* data) {
  std::vector<uint8_t> state_data = {kLogiReportNumberSetInfo, report_number, 0,
                                     0, 0};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(state_data.size()),
                    state_data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  std::vector<uint8_t> out_data = {kLogiReportNumberGetInfo, 0, 0, 0, 0};
  error =
      ioctl(file_descriptor_, HIDIOCGFEATURE(out_data.size()), out_data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  *data = out_data;
  return kLogiErrorNoError;
}

int AudioDevice::TrySetDfuModeSafe() {
  std::vector<uint8_t> out_data;
  int error = SendCommandInHidMode(kLogiReportDataSetDfuModeSafe, &out_data);
  if (error)
    return error;

  // Except for the first index of data, if the queried data is filled with
  // temporary error data, retry to enter DFU. If it's filled with
  // valid data, reset the device. Otherwise, it's an undefined state,
  // should return error.
  bool temp_error = std::all_of(
      out_data.begin() + 1, out_data.end(),
      [](uint8_t i) { return (i == kLogiDfuModeSafeTemporaryErrorData); });
  if (temp_error)
    return kLogiErrorSetDeviceToDfuModeFailed;
  bool is_valid =
      std::all_of(out_data.begin() + 1, out_data.end(),
                  [](uint8_t i) { return (i == kLogiDfuModeSafeValidData); });
  if (!is_valid)
    return kLogiErrorSetDeviceToDfuModeFailed;

  // Everything is good. Sends command to reset the device now.
  std::vector<uint8_t> reset_data = {kLogiReportNumberSetInfo,
                                     kLogiReportDataResetDevice, 0, 0, 0};
  error = ioctl(file_descriptor_, HIDIOCSFEATURE(reset_data.size()),
                reset_data.data());
  if (error < 0) {
    error = kLogiErrorIOControlOperationFailed;
  } else {
    // When succeeded, the ioctl returns non-negative value here. It will be
    // conflicted with the enum error, therefore, set it to kLogiErrorNoError.
    error = kLogiErrorNoError;
  }
  return error;
}
