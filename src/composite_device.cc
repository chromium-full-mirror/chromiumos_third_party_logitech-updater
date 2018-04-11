// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite_device.h"
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <unistd.h>
#include "eeprom_device.h"
#include "mcu2_device.h"
#include "utilities.h"
#include "video_device.h"

CompositeDevice::CompositeDevice() {}

CompositeDevice::CompositeDevice(std::string video_pid,
                                 std::string eeprom_pid,
                                 std::string mcu2_pid) {
  devices_.push_back(std::make_shared<VideoDevice>(video_pid));
  devices_.push_back(std::make_shared<EepromDevice>(eeprom_pid));
  devices_.push_back(std::make_shared<Mcu2Device>(mcu2_pid));
}

CompositeDevice::~CompositeDevice() {
  CloseDevices();
}

int CompositeDevice::OpenDevices() {
  int error = kLogiErrorUnknown;
  for (auto device : devices_) {
    error = device->OpenDevice();
    if (error)
      return error;
  }
  return error;
}

void CompositeDevice::CloseDevices() {
  for (auto device : devices_)
    device->CloseDevice();
}

int CompositeDevice::GetDevicesVersion(
    std::map<int, std::string>* version_map) {
  std::map<int, std::string> output;
  int error = kLogiErrorNoError;
  for (auto device : devices_) {
    std::string version;
    device->OpenDevice();
    error = device->GetDeviceVersion(&version);
    device->CloseDevice();
    if (error)
      return error;
    (*version_map)[device->device_type_] = version;
  }
  return error;
}

int CompositeDevice::GetDeviceName(std::string* name) {
  // Device name is stored on video device.
  std::shared_ptr<USBDevice> device = GetDeviceFromType(kLogiDeviceVideo);
  if (device == nullptr)
    return kLogiErrorDeviceNotPresent;
  device->OpenDevice();
  int error = device->GetDeviceName(name);
  device->CloseDevice();
  return error;
}

int CompositeDevice::GetImagesVersion(std::map<int, std::string>* version_map) {
  int error = kLogiErrorNoError;
  std::string version;

  for (auto device : devices_) {
    std::string version;
    std::vector<uint8_t> buffer = image_buffers_[device->device_type_];
    error = device->VerifyImage(buffer);
    if (error)
      return error;
    if ((error = device->GetImageVersion(buffer, &version)))
      return error;
    (*version_map)[device->device_type_] = version;
  }
  return error;
}

void CompositeDevice::SetImageBuffer(int device_type,
                                     std::vector<uint8_t> buffer) {
  image_buffers_[device_type] = buffer;
}

void CompositeDevice::SetForceUpdate(bool force) {
  for (auto device : devices_)
    device->force_update_ = force;
}

bool CompositeDevice::IsDeviceUpToDate() {
  for (auto device : devices_) {
    std::vector<uint8_t> buffer = image_buffers_[device->device_type_];
    std::string image_version;
    std::string device_version;
    device->OpenDevice();
    device->GetDeviceVersion(&device_version);
    device->GetImageVersion(buffer, &image_version);
    device->CloseDevice();
    if (CompareVersions(device_version, image_version) < 0)
      return false;
  }
  return true;
}

bool CompositeDevice::IsDevicePresent() {
  return IsDevicePresent(kLogiDeviceVideo);
}

bool CompositeDevice::IsDevicePresent(int device_type) {
  for (auto device : devices_) {
    if (device->device_type_ == device_type && device->IsPresent())
      return true;
  }
  return false;
}

bool CompositeDevice::AreImagesPresent() {
  if (image_buffers_.empty())
    return false;
  for (const auto& image_buffer : image_buffers_) {
    const std::vector<uint8_t>& buffer = image_buffer.second;
    if (buffer.empty())
      return false;
  }
  return true;
}

std::shared_ptr<USBDevice> CompositeDevice::GetDeviceFromType(int device_type) {
  for (auto dev : devices_) {
    if (dev->device_type_ == device_type)
      return dev;
  }
  return nullptr;
}

int CompositeDevice::PerformComponentUpdate() {
  std::vector<int> video_types = {kLogiDeviceEeprom, kLogiDeviceMcu2,
                                  kLogiDeviceVideo};
  std::string name;
  GetDeviceName(&name);
  LOG(INFO) << "Checking " << name << " firmwares...";
  int error = kLogiErrorNoError;
  bool did_update_video = false;
  for (auto const& type : video_types) {
    std::shared_ptr<USBDevice> device = GetDeviceFromType(type);
    std::vector<uint8_t> buffer = image_buffers_[type];
    std::vector<uint8_t> header;
    if (device != nullptr) {
      error = device->OpenDevice();
      bool did_update;
      error = device->PerformUpdate(buffer, header, &did_update);
      if (error) {
        device->CloseDevice();
        return error;
      }
      if (did_update && did_update_video == false)
        did_update_video = true;
      if (did_update_video && type == kLogiDeviceVideo)
        device->RebootDevice();
      device->CloseDevice();
    }
  }
  return error;
}
