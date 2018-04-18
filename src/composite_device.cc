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
#include "audio_device.h"
#include "ble_device.h"
#include "codec_device.h"

CompositeDevice::CompositeDevice() {}

CompositeDevice::CompositeDevice(std::string video_pid,
                                 std::string eeprom_pid,
                                 std::string mcu2_pid) {
  devices_.push_back(std::make_shared<VideoDevice>(video_pid));
  devices_.push_back(std::make_shared<EepromDevice>(eeprom_pid));
  devices_.push_back(std::make_shared<Mcu2Device>(mcu2_pid));
  should_reset_hub_ = false;
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
  // This method checks and updates these devices in order: eeprom, mcu2, video,
  // ble, codec, then reboot and updates audio. It is done this way because of
  // rebooting and wait for device causes permission issue on the device mount
  // point (device is mounted as root instead of cfm-firmware-updaters user).
  std::vector<int> types = {kLogiDeviceEeprom, kLogiDeviceMcu2,
                            kLogiDeviceVideo, kLogiDeviceBle, kLogiDeviceCodec};
  bool did_video_update = false;
  std::string name;
  GetDeviceName(&name);
  LOG(INFO) << "Checking " << name << " firmwares...";
  int error = kLogiErrorNoError;

  for (auto const& type : types) {
    std::shared_ptr<USBDevice> device = GetDeviceFromType(type);
    if (!device)
      continue;
    std::vector<uint8_t> buffer = image_buffers_[type];
    std::vector<uint8_t> header = secure_headers_[type];

    error = device->OpenDevice();
    bool did_update;
    error = device->PerformUpdate(buffer, header, &did_update);
    if (error) {
      device->CloseDevice();
      return error;
    }
    if (did_update && (type == kLogiDeviceEeprom || type == kLogiDeviceMcu2 ||
                       type == kLogiDeviceVideo))
      did_video_update = true;

    if (did_video_update && type == kLogiDeviceVideo)
      device->RebootDevice();

    device->CloseDevice();

    if (did_update && type == kLogiDeviceCodec) {
      // Updating codec device requires reset. This causes permission issue in
      // minijail when device comes back. Return and exit the updater to allow
      // Udev to relaunch it and continue updating audio device.
      return error;
    }
  }

  // For audio: just perform preparing and put into DFU mode. Udev will launch
  // this updater again when device reboots in DFU mode.
  std::shared_ptr<AudioDevice> audio_device =
      std::static_pointer_cast<AudioDevice>(
          GetDeviceFromType(kLogiDeviceAudio));
  std::vector<uint8_t> buffer = image_buffers_[kLogiDeviceAudio];
  std::vector<uint8_t> header = secure_headers_[kLogiDeviceAudio];
  if (audio_device != nullptr) {
    error = audio_device->OpenDevice();
    bool should_update;
    error = audio_device->PrepareForUpdate(buffer, &should_update);
    audio_device->CloseDevice();

    // Device with long audio cable sometimes fails to be re-mounted after being
    // put into dfu mode. Reset usb hub to power-cyle the device.
    if (should_update && should_reset_hub_)
      ResetUsbHub(hub_vid_, hub_pid_);
  }
  return error;
}

void CompositeDevice::AddDevice(int device_type,
                                std::string pid,
                                std::string dfu_pid) {
  switch (device_type) {
    case kLogiDeviceVideo:
      devices_.push_back(std::make_shared<VideoDevice>(pid));
      break;
    case kLogiDeviceEeprom:
      devices_.push_back(std::make_shared<EepromDevice>(pid));
      break;
    case kLogiDeviceMcu2:
      devices_.push_back(std::make_shared<Mcu2Device>(pid));
      break;
    case kLogiDeviceAudio:
      devices_.push_back(std::make_shared<AudioDevice>(pid, dfu_pid));
      break;
    case kLogiDeviceCodec:
      devices_.push_back(std::make_shared<CodecDevice>(pid));
      break;
    case kLogiDeviceBle:
      devices_.push_back(std::make_shared<BleDevice>(pid));
      break;
    default:
      break;
  }
}

void CompositeDevice::SetSecureHeader(int device_type,
                                      std::vector<uint8_t> secure_header,
                                      bool enable) {
  secure_headers_[device_type] = secure_header;
  std::shared_ptr<USBDevice> device = GetDeviceFromType(device_type);
  if (!device)
    return;
  device->secure_boot_ = enable;
}

void CompositeDevice::SetSupportBle(bool support_ble) {
  for (auto device : devices_)
    device->support_ble_ = support_ble;
}

int CompositeDevice::IsLogicool(bool check_video,
                                bool check_audio,
                                bool* logicool) {
  bool video_present = IsDevicePresent(kLogiDeviceVideo);
  bool audio_present = IsDevicePresent(kLogiDeviceAudio);
  if (!video_present && !audio_present) {
    LOG(ERROR) << "Failed to check Logicool. Audio and video are not present.";
    return kLogiErrorDeviceNotPresent;
  }

  std::shared_ptr<USBDevice> video_device;
  std::shared_ptr<USBDevice> audio_device;
  bool video_cool = false;
  bool audio_cool = false;
  int error = kLogiErrorNoError;

  if (check_video) {
    if (!video_present) {
      LOG(ERROR)
          << "Failed to check Logicool on video device. Device not present.";
      return kLogiErrorDeviceNotPresent;
    }
    video_device = GetDeviceFromType(kLogiDeviceVideo);
    video_device->OpenDevice();
    error = video_device->IsLogicool(&video_cool);
    video_device->CloseDevice();
    if (error)
      return error;
    if (!check_audio) {
      *logicool = video_cool;
      return kLogiErrorNoError;
    }
  }

  if (check_audio) {
    if (!audio_present) {
      LOG(ERROR)
          << "Failed to check Logicool on audio device. Device not present.";
      return kLogiErrorDeviceNotPresent;
    }
    audio_device = GetDeviceFromType(kLogiDeviceAudio);
    audio_device->OpenDevice();
    error = audio_device->IsLogicool(&audio_cool);
    audio_device->CloseDevice();
    if (error)
      return error;
    if (!check_video) {
      *logicool = audio_cool;
      return kLogiErrorNoError;
    }
  }

  // Checks both video and audio Logicool firmware for mismatched brand.
  if (video_cool != audio_cool)
    return kLogiErrorBrandMismatched;
  *logicool = video_cool;
  return error;
}

int CompositeDevice::WriteBrandInfoToEeprom(bool logicool) {
  std::shared_ptr<EepromDevice> device = std::static_pointer_cast<EepromDevice>(
      GetDeviceFromType(kLogiDeviceEeprom));
  if (device == nullptr) {
    LOG(ERROR) << "Failed to write brand info to EEPROM. Eeprom not found.";
    return kLogiErrorDeviceNotPresent;
  }
  device->OpenDevice();
  int error = device->WriteBrandInfo(logicool);
  device->CloseDevice();
  return error;
}

int CompositeDevice::ReadBrandInfoFromEeprom(bool* logicool) {
  std::shared_ptr<EepromDevice> device = std::static_pointer_cast<EepromDevice>(
      GetDeviceFromType(kLogiDeviceEeprom));
  if (device == nullptr) {
    LOG(ERROR) << "Failed to write brand info to EEPROM. Eeprom not found.";
    return kLogiErrorDeviceNotPresent;
  }
  device->OpenDevice();
  int error = device->ReadBrandInfo(logicool);
  device->CloseDevice();
  return error;
}

int CompositeDevice::PerformDfuAudioUpdate() {
  int error = kLogiErrorNoError;
  std::shared_ptr<AudioDevice> audio_device =
      std::static_pointer_cast<AudioDevice>(
          GetDeviceFromType(kLogiDeviceAudio));
  if (audio_device == nullptr)
    return kLogiErrorDeviceNotPresent;
  std::string name;
  GetDeviceName(&name);
  LOG(INFO) << "Checking " << name << " audio firmware...";
  std::vector<uint8_t> buffer = image_buffers_[kLogiDeviceAudio];
  std::vector<uint8_t> header = secure_headers_[kLogiDeviceAudio];
  bool did_update;
  audio_device->OpenDevice();
  error = audio_device->PerformUpdate(buffer, header, &did_update);
  audio_device->CloseDevice();

  return error;
}

int CompositeDevice::ResetUsbHub(std::string vid, std::string pid) {
  std::vector<std::string> bus_paths = FindUsbBus(vid, pid);
  if (bus_paths.empty())
    return kLogiErrorUsbPidNotFound;

  int error = kLogiErrorNoError;
  for (auto const& path : bus_paths) {
    int fd = open(path.c_str(), O_WRONLY);
    if (fd == -1) {
      LOG(ERROR) << "Failed to open hub: " << path;
      error = kLogiErrorOpenDeviceFailed;
      continue;
    }
    int res = ioctl(fd, USBDEVFS_RESET, 0);
    if (res < 0) {
      error = kLogiErrorIOControlOperationFailed;
      LOG(ERROR) << "Failed to reset hub: " << path;
    }
    if (fd >= 0)
      close(fd);
  }
  return error;
}
