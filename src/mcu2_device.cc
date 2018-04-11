// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mcu2_device.h"
#include "utilities.h"

constexpr unsigned char kLogiMCU2UvcXuDevInfoVersionCs = 11;
constexpr unsigned int kLogiMCU2DeviceVersionDataSize = 2;
// Address to read major version from the image buffer.
constexpr unsigned int kLogiMCU2ImageReadAddressMajorVersion = 0x08;
// Address to read minor version from the image buffer.
constexpr unsigned int kLogiMCU2ImageReadAddressMinorVersion = 0x0c;
// Minium buffer size to read version.
constexpr unsigned int kLogiMCU2ImageReadAddressVersionMinSize =
    kLogiMCU2ImageReadAddressMinorVersion + 1;
constexpr unsigned char kLogiMCU2AitInitiateSetMMPData = 2;
constexpr unsigned char kLogiMCU2AitFinalizeSetMMPData = 3;
// Bypass the 16 bytes information header.
constexpr unsigned int kLogiMCU2AitSendImageInitialOffset = 16;

Mcu2Device::Mcu2Device(std::string pid) : VideoDevice(pid, kLogiDeviceMcu2) {}

Mcu2Device::~Mcu2Device() {}

int Mcu2Device::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  std::vector<unsigned char> output_data;
  int unit_id = GetUnitID(kLogiGuidDeviceInfo);
  int error =
      GetXuControl(unit_id, kLogiMCU2UvcXuDevInfoVersionCs, &output_data);
  if (error)
    return error;
  if (output_data.size() < kLogiMCU2DeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;

  int major = static_cast<int>(output_data[0]);
  int minor = static_cast<int>(output_data[1]);
  *device_version = GetDeviceStringVersion(major, minor);
  return error;
}

int Mcu2Device::GetImageVersion(std::vector<uint8_t> buffer,
                                std::string* image_version) {
  if (buffer.empty() || buffer.size() < kLogiMCU2ImageReadAddressVersionMinSize)
    return kLogiErrorImageBufferReadFailed;
  int major = buffer[kLogiMCU2ImageReadAddressMajorVersion];
  int minor = buffer[kLogiMCU2ImageReadAddressMinorVersion];
  *image_version = GetDeviceStringVersion(major, minor);
  return kLogiErrorNoError;
}

int Mcu2Device::VerifyImage(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  // Motor control unit device has no secured signature image yet.
  return kLogiErrorNoError;
}

int Mcu2Device::AitInitiateUpdate() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  // Disclaimer: Any magic numbers come directly from the FlashGordon code.
  std::vector<unsigned char> data = {kLogiAitSetMmpCmdFwBurning,
                                     kLogiMCU2AitInitiateSetMMPData,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0};
  return AitInitiateUpdateWithData(data);
}

int Mcu2Device::AitSendImage(std::vector<uint8_t> buffer) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  return AitSendImageWithOffset(buffer, kLogiMCU2AitSendImageInitialOffset);
}

int Mcu2Device::AitFinalizeUpdate() {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code.
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  std::vector<unsigned char> data = {kLogiAitSetMmpCmdFwBurning,
                                     kLogiMCU2AitFinalizeSetMMPData,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0};
  return AitFinalizeUpdateWithData(data);
}
