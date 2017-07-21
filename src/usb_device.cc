// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_device.h"
#include <thread>
#include "utilities.h"

// 500ms to retry reading device version
constexpr unsigned int kLogiReadDeviceVersionRetryIntervalMs = 500;
// max retry to read device version
constexpr unsigned int kLogiReadDeviceVersionMaxRetry = 3;

USBDevice::USBDevice(std::string pid, int type)
    : usbPid(pid), deviceType(type), fileDescriptor(-1), isOpen(false) {}

USBDevice::~USBDevice() {}

int USBDevice::getDeviceVersion(std::string* deviceVersion) {
  int error = kLogiErrorUnknown;
  std::string version;
  for (int i = 0; i < kLogiReadDeviceVersionMaxRetry; i++) {
    error = readDeviceVersion(&version);
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

  *deviceVersion = version;
  return kLogiErrorNoError;
}
