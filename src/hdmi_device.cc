// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hdmi_device.h"
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include "utilities.h"

constexpr int kLogiHdmiVerSetSelector = 0x01;
constexpr int kLogiHdmiVerGetSelector = 0x02;
constexpr int kLogiHdmiVerSetData = 0x0B;
constexpr int kLogiHdmiVerSize = 8;
HdmiDevice::HdmiDevice(std::string pid) : VideoDevice(pid, kLogiDeviceHdmi) {}

HdmiDevice::~HdmiDevice() {}

int HdmiDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  int unit_id = GetUnitID(kLogiGuidAITCustom);
  std::vector<unsigned char> data = {kLogiHdmiVerSetData, 0, 0, 0, 0, 0, 0, 0};
  int error = SetXuControl(unit_id, kLogiHdmiVerSetSelector, data);
  if (error) {
    LOG(ERROR) << "Failed to set data to read HDMI version. Error: " << error;
    return error;
  }
  std::vector<unsigned char> ver_data;
  error = GetXuControl(unit_id, kLogiHdmiVerGetSelector, &ver_data);
  if (error || ver_data.size() < kLogiHdmiVerSize) {
    if (ver_data.size() < kLogiHdmiVerSize)
      error = kLogiErrorInvalidDeviceVersionDataSize;
    LOG(ERROR) << "Failed to read HDMI version. Error: " << error;
    return error;
  }
  int major = static_cast<int>(ver_data[3] + ver_data[2] * 100);
  int minor = static_cast<int>(ver_data[5] + ver_data[4] * 100);
  int build = static_cast<int>(ver_data[7] + ver_data[6] * 100);
  *device_version = GetDeviceStringVersion(major, minor, build);
  return error;
}

int HdmiDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                std::string* image_version) {
  if (version_file_.empty())
    return kLogiErrorImageVersionNotFound;
  if (!ReadFileContent(version_file_, image_version))
    return kLogiErrorImageVersionNotFound;
  return kLogiErrorNoError;
}

int HdmiDevice::VerifyImage(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  return kLogiErrorNoError;
}
