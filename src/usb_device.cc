// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_device.h"
#include <base/logging.h>
#include <unistd.h>
#include <thread>
#include "utilities.h"

// 500ms to retry reading device version.
constexpr unsigned int kLogiReadDeviceVersionRetryIntervalMs = 500;
// Max retry to read device version.
constexpr unsigned int kLogiReadDeviceVersionMaxRetry = 3;

USBDevice::USBDevice(std::string pid, int type)
    : usb_pid_(pid),
      device_type_(type),
      force_update_(false),
      secure_boot_(false),
      support_ble_(false),
      file_descriptor_(-1),
      is_open_(false) {}

USBDevice::~USBDevice() {
  CloseDevice();
}

int USBDevice::GetDeviceVersion(std::string* device_version) {
  int error = kLogiErrorUnknown;
  std::string version;
  for (int i = 0; i < kLogiReadDeviceVersionMaxRetry; i++) {
    error = ReadDeviceVersion(&version);
    if (!error && CompareVersions(version, "0.0.0") != 0)
      break;

    // Occasionally, the device is not ready for version reading (just rebooted,
    // or finalized update process, etc...)  version 0.0.0 not actually an
    // error, it's because internal chipset returns a valid but not correct
    // version when it's not ready. Wait and retry will solve the problem.
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiReadDeviceVersionRetryIntervalMs));
  }
  if (error)
    return error;

  *device_version = version;
  return kLogiErrorNoError;
}

void USBDevice::CloseDevice() {
  if (file_descriptor_ >= 0)
    close(file_descriptor_);

  file_descriptor_ = -1;
  is_open_ = false;
}

int USBDevice::CheckForUpdate(std::vector<uint8_t> buffer,
                              bool* should_update) {
  std::string device_version;
  std::string image_version;
  int error = GetDeviceVersion(&device_version);
  if (error) {
    LOG(ERROR) << "Failed to read device version. Error: " << error;
    return error;
  }

  error = VerifyImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to verify binary image. Error: " << error;
    return error;
  }

  error = GetImageVersion(buffer, &image_version);
  if (error) {
    LOG(ERROR) << "Failed to read binary image version. Error: " << error;
    return error;
  }

  if (force_update_) {
    LOG(INFO) << "Force update enabled. Will not check firmware version.";
    *should_update = true;
    return kLogiErrorNoError;
  }

  int compare_version = CompareVersions(device_version, image_version);
  if (compare_version >= 0) {
    LOG(INFO) << "Firmware is up to date.";
    *should_update = false;
  } else {
    *should_update = true;
  }
  return kLogiErrorNoError;
}

void USBDevice::SetVersionFile(const std::string& version_file) {
  version_file_ = version_file;
}
