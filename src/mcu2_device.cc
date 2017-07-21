// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mcu2_device.h"
#include "utilities.h"

constexpr unsigned char kLogiMCU2UvcXuDevInfoVersionCs = 11;
constexpr unsigned int kLogiMCU2DeviceVersionDataSize = 2;
// address to read major version from the image buffer
constexpr unsigned int kLogiMCU2ImageReadAddressMajorVersion = 0x08;
// address to read minor version from the image buffer
constexpr unsigned int kLogiMCU2ImageReadAddressMinorVersion = 0x0c;
// minimum size of image buffer to read versions
constexpr unsigned int kLogiMCU2ImageReadAddressVersionMinSize =
    kLogiMCU2ImageReadAddressMinorVersion + 1;
constexpr unsigned char kLogiMCU2AitInitiateSetMMPData = 2;
constexpr unsigned char kLogiMCU2AitFinalizeSetMMPData = 3;
// bypass the 16 bytes information header
constexpr unsigned int kLogiMCU2AitSendImageInitialOffset = 16;

Mcu2Device::Mcu2Device(std::string pid) : VideoDevice(pid, kLogiDeviceMcu2) {}

Mcu2Device::~Mcu2Device() {
  closeDevice();
}

int Mcu2Device::readDeviceVersion(std::string* deviceVersion) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  std::vector<unsigned char> outputData;
  int unitId = GetUnitID(kLogiGuidDeviceInfo);
  int error = getXuControl(unitId, kLogiMCU2UvcXuDevInfoVersionCs, &outputData);
  if (error)
    return error;
  if (outputData.size() < kLogiMCU2DeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;

  int major = (int)outputData[0];
  int minor = (int)outputData[1];
  *deviceVersion = GetDeviceStringVersion(major, minor);
  return error;
}

int Mcu2Device::getImageVersion(std::vector<char> buffer,
                                std::string* imageVersion) {
  if (buffer.empty() || buffer.size() < kLogiMCU2ImageReadAddressVersionMinSize)
    return kLogiErrorImageBufferReadFailed;
  int major = buffer[kLogiMCU2ImageReadAddressMajorVersion];
  int minor = buffer[kLogiMCU2ImageReadAddressMinorVersion];
  *imageVersion = GetDeviceStringVersion(major, minor);
  return kLogiErrorNoError;
}

int Mcu2Device::verifyImage(std::vector<char> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // no verification here
  return kLogiErrorNoError;
}

int Mcu2Device::aitInitiateUpdate() {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // Disclaimer: Any magic numbers come directly from the FlashGordon code.

  // set the MMP
  std::vector<unsigned char> mmpSetData = {kLogiAitSetMmpCmdFwBurning,
                                           kLogiMCU2AitInitiateSetMMPData,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0};
  return aitInitiateUpdateWithData(mmpSetData);
}

int Mcu2Device::aitSendImage(std::vector<char> buffer) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  return aitSendImageWithOffset(buffer, kLogiMCU2AitSendImageInitialOffset);
}

int Mcu2Device::aitFinalizeUpdate() {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // set MMP
  std::vector<unsigned char> setMmpData = {kLogiAitSetMmpCmdFwBurning,
                                           kLogiMCU2AitFinalizeSetMMPData,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0,
                                           0};  // always 8 bytes
  return aitFinalizeUpdateWithData(setMmpData);
}
