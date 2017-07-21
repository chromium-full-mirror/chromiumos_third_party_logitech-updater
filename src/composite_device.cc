// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite_device.h"
#include <unistd.h>
#include "eeprom_device.h"
#include "mcu2_device.h"
#include "utilities.h"
#include "video_device.h"

constexpr unsigned int kLogiRebootingWaitTime = 10;

CompositeDevice::CompositeDevice(std::string videoPid,
                                 std::string eepromPid,
                                 std::string mcu2Pid) {
  devices.push_back(std::make_shared<VideoDevice>(videoPid));
  devices.push_back(std::make_shared<EepromDevice>(eepromPid));
  devices.push_back(std::make_shared<Mcu2Device>(mcu2Pid));
  openDevices();
}

CompositeDevice::~CompositeDevice() {
  closeDevices();
}

int CompositeDevice::openDevices() {
  int error = kLogiErrorUnknown;
  for (auto device : devices) {
    // open each device
    error = device->openDevice();
    if (error)
      break;
  }
  return error;
}

void CompositeDevice::closeDevices() {
  for (auto device : devices)
    device->closeDevice();
}

int CompositeDevice::getDevicesVersion(std::map<int, std::string>* versionMap) {
  std::map<int, std::string> output;
  int error = kLogiErrorUnknown;
  for (auto device : devices) {
    std::string version;
    if ((error = device->getDeviceVersion(&version)))
      break;  // error getting version, break out and return
    (*versionMap)[device->deviceType] = version;
  }
  return error;
}

int CompositeDevice::getDevicesName(std::map<int, std::string>* nameMap) {
  int error = kLogiErrorUnknown;
  for (auto device : devices) {
    std::string name;
    if ((error = device->getDeviceName(&name)))
      break;  // error getting name, break out and return
    (*nameMap)[device->deviceType] = name;
  }
  return error;
}

int CompositeDevice::getImagesVersion(std::map<int, std::string>* versionMap) {
  int error = kLogiErrorUnknown;
  std::string version;

  for (auto device : devices) {
    std::string version;
    std::vector<char> buffer = getImageBuffer(device->deviceType);

    error = device->verifyImage(buffer);
    if (error)
      return error;  // failed to verify image
    if ((error = device->getImageVersion(buffer, &version)))
      return error;  // failed to get image version
    (*versionMap)[device->deviceType] = version;
  }
  return error;
}

bool CompositeDevice::isDeviceUpToDate() {
  for (auto device : devices) {
    std::vector<char> buffer = getImageBuffer(device->deviceType);
    std::string imageVersion;
    std::string deviceVersion;
    device->getDeviceVersion(&deviceVersion);
    device->getImageVersion(buffer, &imageVersion);
    if (CompareVersions(deviceVersion, imageVersion) < 0)
      return false;
  }
  return true;
}

bool CompositeDevice::isDevicePresent() {
  for (auto device : devices) {
    std::vector<std::string> devicepath = device->findDevices();
    if (devicepath.size() > 0)
      return true;
  }
  return false;
}

int CompositeDevice::performUpdate() {
  int error = kLogiErrorUnknown;
  std::shared_ptr<USBDevice> videoDevice;
  if (!isDeviceUpToDate()) {
    for (auto device : devices) {
      std::vector<char> buffer = getImageBuffer(device->deviceType);
      error = device->performUpdate(buffer);
      if (error)
        return error;  // failed to update
      if (device->deviceType == kLogiDeviceVideo)
        videoDevice = device;  // get a hold of video device to do reboot later
    }

    // updated ok, reboot devices
    for (auto device : devices) {
      if (device->deviceType != kLogiDeviceVideo)
        device->closeDevice();  // close all other device before rebooting
    }
    videoDevice->rebootDevice();
    sleep(kLogiRebootingWaitTime);  // wait for device to reboot
    openDevices();                  // make sure to reopen devices
  }
  return kLogiErrorNoError;
}

std::vector<char> CompositeDevice::getImageBuffer(int deviceType) {
  std::vector<char> buffer;
  switch (deviceType) {
    case kLogiDeviceVideo:
      buffer = videoImageBuffer;
      break;

    case kLogiDeviceEeprom:
      buffer = eepromImageBuffer;
      break;

    case kLogiDeviceMcu2:
      buffer = mcu2ImageBuffer;
      break;

    default:
      break;
  }
  return buffer;
}
