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
#include <linux/hidraw.h>
#include <thread>
#include <libusb.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include "base/strings/string_number_conversions.h"
#include "eeprom_device.h"
#include "mcu2_device.h"
#include "utilities.h"
#include "video_device.h"
#include "audio_device.h"
#include "ble_device.h"
#include "codec_device.h"
#include "hdmi_device.h"
#include "tablehub_device.h"
#include "usb_device.h"

constexpr int kDefaultPwrCycleMs = 2000;
// Commands to powercycle on mcu chip.
constexpr uint8_t kMcuTdeUsagePage = 0x1A;
constexpr uint8_t kMcuTdeEnable = 0x02;
constexpr uint8_t kMcuTdeOn = 0x01;
constexpr uint8_t kMcuTdeOff = 0x00;
constexpr uint8_t kMcuPinClear = 0x05;
constexpr uint8_t kMcuPinSet = 0x06;
constexpr uint8_t kMcuPwrSet = 0x2D;
constexpr uint8_t kMcuPwrReset = 0x2E;

const char kLogiMeetUpName[] = "MeetUp";
const char kLogiMeetUpOldCodecFirmwareVersion[] = "8.0.216";

const char kLogiPtzPro2Name[] = "PTZ Pro 2";

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
    std::map<int, std::string>& version_map) {
  std::map<int, std::string> output;
  int error = kLogiErrorNoError;
  for (auto device : devices_) {
    std::string version;
    device->OpenDevice();
    error = device->GetDeviceVersion(&version);
    device->CloseDevice();
    if (error)
      return error;
    version_map[device->device_type_] = version;
  }
  return error;
}

int CompositeDevice::GetDevicesVersionFromAudio(
    std::map<int, std::string>& version_map) {
  int error = kLogiErrorNoError;
  std::shared_ptr<AudioDevice> audio_device =
      std::static_pointer_cast<AudioDevice>(
          GetDeviceFromType(kLogiDeviceAudio));
  audio_device->OpenDevice();
  SystemTopology topology;
  error = audio_device->ReadSystemTopology(&topology);
  if (error)
    return error;
  std::string device_version =
      base::NumberToString(topology.tablehub.ver_major) + "." +
      base::NumberToString(topology.tablehub.ver_minor) + "." +
      base::NumberToString(topology.tablehub.ver_build);
  version_map[kLogiDeviceTableHub] = device_version;
  if (topology.micpods.size() > 0) {
    device_version = base::NumberToString(topology.micpods[0].ver_major) + "." +
                     base::NumberToString(topology.micpods[0].ver_minor) + "." +
                     base::NumberToString(topology.micpods[0].ver_build);
    version_map[kLogiDeviceMicPod] = device_version;
  }
  if (topology.splitters.size() > 0) {
    device_version =
        base::NumberToString(topology.splitters[0].ver_major) + "." +
        base::NumberToString(topology.splitters[0].ver_minor) + "." +
        base::NumberToString(topology.splitters[0].ver_build);
    version_map[kLogiDeviceSplitter] = device_version;
  }
  version_map[kLogiDeviceVideo] = topology.video_version;
  version_map[kLogiDeviceEeprom] = topology.video_eeprom_version;
  version_map[kLogiDeviceAudio] = topology.audio_version;
  version_map[kLogiDeviceBle] = topology.audio_ble_version;
  version_map[kLogiDeviceVideoBle] = topology.video_ble_version;
  return error;
}

int CompositeDevice::GetDeviceName(std::string& name) {
  // Device name is stored on video device.
  std::shared_ptr<USBDevice> device = GetDeviceFromType(kLogiDeviceVideo);
  // Added hdmi in case of tap.
  if (device == nullptr)
    device = GetDeviceFromType(kLogiDeviceHdmi);
  if (device == nullptr)
    return kLogiErrorDeviceNotPresent;
  device->OpenDevice();
  int error = device->GetDeviceName(&name);
  device->CloseDevice();
  return error;
}

int CompositeDevice::GetImagesVersionFromFile(
    std::map<int, std::string>& version_map) {
  int error = kLogiErrorNoError;
  std::shared_ptr<TableHubDevice> tablehub_device =
      std::static_pointer_cast<TableHubDevice>(
          GetDeviceFromType(kLogiDeviceTableHub));
  std::map<std::string, std::string> image_version_map;
  error = tablehub_device->GetAllComponentImageVersions(&image_version_map);
  if (error) {
    return error;
  }
  for (const auto& pair : image_version_map) {
    if (pair.first == kVideoSearchString)
      version_map[kLogiDeviceVideo] = pair.second;
    else if (pair.first == kAudioSearchString)
      version_map[kLogiDeviceAudio] = pair.second;
    else if (pair.first == kAudioBleSearchString)
      version_map[kLogiDeviceBle] = pair.second;
    else if (pair.first == kEepromSearchString)
      version_map[kLogiDeviceEeprom] = pair.second;
    else if (pair.first == kTableHubSearchString)
      version_map[kLogiDeviceTableHub] = pair.second;
    else if (pair.first == kMicPodSearchString)
      version_map[kLogiDeviceMicPod] = pair.second;
    else if (pair.first == kSplitterSearchString)
      version_map[kLogiDeviceSplitter] = pair.second;
  }
  return error;
}

int CompositeDevice::GetImagesVersion(std::map<int, std::string>& version_map) {
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
    version_map[device->device_type_] = version;
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
  // Added checking for HDMI in case of Tap device.
  return (IsDevicePresent(kLogiDeviceVideo) ||
          IsDevicePresent(kLogiDeviceHdmi));
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

int CompositeDevice::PerformComponentUpdate(bool* updated) {
  // This method checks and updates these devices in order: eeprom, mcu2, video,
  // ble, codec, then reboot and updates audio. It is done this way because of
  // rebooting and wait for device causes permission issue on the device mount
  // point (device is mounted as root instead of cfm-firmware-updaters user).
  std::vector<int> types = {kLogiDeviceEeprom, kLogiDeviceMcu2,
                            kLogiDeviceHdmi,   kLogiDeviceVideo,
                            kLogiDeviceBle,    kLogiDeviceCodec};
  bool did_video_update = false;
  *updated = false;
  std::string name;
  GetDeviceName(name);
  LOG(INFO) << "Checking " << name << " firmwares...";
  int error = kLogiErrorNoError;

  // Device availibility is limited during upgrade.
  // Query versions beforehand. Version query may fail in DFU mode
  // Version needed only for CODEC, so safe to ignore other failures
  std::map<int, std::string> info_map;
  GetDevicesVersion(info_map);

  for (auto const& type : types) {
    std::shared_ptr<USBDevice> device = GetDeviceFromType(type);
    if (!device)
      continue;

    // To support backward compatibility with older codec firmware version,
    // always forcefully upgrade codec firmware (if running 8.0.216)
    if (type == kLogiDeviceCodec) {
      if (name.find(kLogiMeetUpName) != std::string::npos) {
        if (info_map.find(kLogiDeviceCodec) != info_map.end()) {
          if (CompareVersions(info_map[kLogiDeviceCodec],
                              kLogiMeetUpOldCodecFirmwareVersion) == 0) {
            LOG(WARNING) << "Device running older Codec firmware "
                         << info_map[kLogiDeviceCodec].c_str()
                         << ", upgrading forcefully";
            device->force_update_ = true;
          }
        }
      }
    }

    // For PTZPro2, skip MCU upgrade. Let new Video firmware (2.0.187+)
    // upgrade MCU
    if ((type == kLogiDeviceMcu2) &&
        (name.find(kLogiPtzPro2Name) != std::string::npos)) {
      bool skip_update;
      error = CheckMCUUpdate(&skip_update, name);
      if ((error) || (skip_update))
        continue;
    }

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
                       type == kLogiDeviceVideo || type == kLogiDeviceHdmi))
      did_video_update = true;

    if (did_video_update && type == kLogiDeviceVideo)
      device->RebootDevice();

    device->CloseDevice();
    if (did_update)
      *updated = true;
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
    case kLogiDeviceHdmi:
      devices_.push_back(std::make_shared<HdmiDevice>(pid));
      break;
    case kLogiDeviceTableHub:
      devices_.push_back(std::make_shared<TableHubDevice>(pid));
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
                                bool& logicool) {
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
      logicool = video_cool;
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
      logicool = audio_cool;
      return kLogiErrorNoError;
    }
  }

  // Checks both video and audio Logicool firmware for mismatched brand.
  if (video_cool != audio_cool)
    return kLogiErrorBrandMismatched;
  logicool = video_cool;
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

int CompositeDevice::ReadBrandInfoFromEeprom(bool& logicool) {
  std::shared_ptr<EepromDevice> device = std::static_pointer_cast<EepromDevice>(
      GetDeviceFromType(kLogiDeviceEeprom));
  if (device == nullptr) {
    LOG(ERROR) << "Failed to write brand info to EEPROM. Eeprom not found.";
    return kLogiErrorDeviceNotPresent;
  }
  device->OpenDevice();
  int error = device->ReadBrandInfo(&logicool);
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
  GetDeviceName(name);
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

int CompositeDevice::PowerCycleTDEMode(std::string pid) {
  std::vector<std::string> dev_paths =
      FindDevices(kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, pid);
  if (dev_paths.empty())
    return kLogiErrorUsbPidNotFound;
  int fd = open(dev_paths.at(0).c_str(), O_RDWR | O_NONBLOCK, 0);
  if (fd < 0)
    return kLogiErrorDeviceNotOpen;
  const std::vector<uint8_t> enableTDE = {kMcuTdeUsagePage, kMcuTdeEnable,
                                          kMcuTdeOn, 0, 0};
  const std::vector<uint8_t> clearPwrPin = {kMcuTdeUsagePage, kMcuPinClear,
                                            kMcuPwrSet, 0, 0};
  const std::vector<uint8_t> clearRstPin = {kMcuTdeUsagePage, kMcuPinClear,
                                            kMcuPwrReset, 0, 0};
  const std::vector<uint8_t> setPwrPin = {kMcuTdeUsagePage, kMcuPinSet,
                                          kMcuPwrSet, 0, 0};
  const std::vector<uint8_t> setRstPin = {kMcuTdeUsagePage, kMcuPinSet,
                                          kMcuPwrReset, 0, 0};
  const std::vector<uint8_t> disableTDE = {kMcuTdeUsagePage, kMcuTdeEnable,
                                           kMcuTdeOff, 0, 0};
  // Enable TDE mode on the device.
  int result = ioctl(fd, HIDIOCSFEATURE(enableTDE.size()), enableTDE.data());
  // Clear power pin on the device.
  if (result >= 0)
    result = ioctl(fd, HIDIOCSFEATURE(clearPwrPin.size()), clearPwrPin.data());
  // Clear reset pin on the device.
  if (result >= 0)
    result = ioctl(fd, HIDIOCSFEATURE(clearRstPin.size()), clearRstPin.data());
  // Wait for 2s after clearing reset pin and set power pin.
  if (result >= 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultPwrCycleMs));
    result = ioctl(fd, HIDIOCSFEATURE(setPwrPin.size()), setPwrPin.data());
  }
  // Wait for 2s after power pin set and set reset pin.
  if (result >= 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultPwrCycleMs));
    result = ioctl(fd, HIDIOCSFEATURE(setRstPin.size()), setRstPin.data());
  }
  // Disable TDE mode on the device.
  if (result >= 0)
    result = ioctl(fd, HIDIOCSFEATURE(disableTDE.size()), disableTDE.data());
  close(fd);
  if (result < 0) {
    LOG(ERROR) << "Failed to send reset command to device.";
    return kLogiErrorIOControlOperationFailed;
  }
  return kLogiErrorNoError;
}

void CompositeDevice::SetVersionFile(int device_type,
                                     const std::string& version_file) {
  std::shared_ptr<USBDevice> device = GetDeviceFromType(device_type);
  if (!device)
    return;
  device->SetVersionFile(version_file);
}

int CompositeDevice::PerformSystemUpdate() {
  struct SystemTopology top;
  int error = kLogiErrorNoError;
  std::string name;
  GetDeviceName(name);
  LOG(INFO) << "Checking " << name.substr(0, 10) << " firmwares...";
  std::shared_ptr<AudioDevice> audio_device =
      std::static_pointer_cast<AudioDevice>(
          GetDeviceFromType(kLogiDeviceAudio));
  audio_device->OpenDevice();
  struct SystemTopology topology;
  error = audio_device->ReadSystemTopology(&topology);
  if (error != kLogiErrorNoError) {
    LOG(ERROR) << "Failed to grab Rally topology.";
    audio_device->CloseDevice();
    return error;
  }
  std::shared_ptr<TableHubDevice> tablehub_device =
      std::static_pointer_cast<TableHubDevice>(
          GetDeviceFromType(kLogiDeviceTableHub));
  error = tablehub_device->OpenDevice();
  if (error != 0) {
    LOG(ERROR) << "Failed to open table hub device.";
    return error;
  }
  // Have to set tablehub firmware version prior to performing update since
  // version is queried from audio.
  std::string version = GetDeviceStringVersion(topology.tablehub.ver_major,
                                               topology.tablehub.ver_minor,
                                               topology.tablehub.ver_build);
  tablehub_device->SetDeviceVersion(version);
  std::vector<uint8_t> buffer = image_buffers_[kLogiDeviceTableHub];
  std::vector<uint8_t> header = secure_headers_[kLogiDeviceTableHub];
  bool did_update;
  error = tablehub_device->PerformUpdate(buffer, header, &did_update);
  if (error) {
    LOG(INFO) << "Failed to perform update.";
    tablehub_device->CloseDevice();
    audio_device->CloseDevice();
    return error;
  }
  if (did_update) {
    // Successfully sent image over to tablehub. Get progress.
    error = tablehub_device->GetProgress();
    if (error)
      return error;
    LOG(INFO) << "Updating system component firmwares...";
    error = CheckComponentsUpdated(audio_device, tablehub_device, topology);
    if (error)
      LOG(ERROR) << "Failed to update Rally system. Error: " << error;
    else
      LOG(INFO) << "Successfully updated Rally system.";
  }
  audio_device->CloseDevice();
  tablehub_device->CloseDevice();
  return error;
}

int CompositeDevice::CheckComponentsUpdated(
    const std::shared_ptr<AudioDevice>& audio,
    const std::shared_ptr<TableHubDevice>& tablehub,
    SystemTopology& topology) {
  int error = kLogiErrorNoError;
  int timeout = 0;
  int count;
  GetComponentCount(count, topology);
  std::map<std::string, std::string> image_versions;
  tablehub->GetAllComponentImageVersions(&image_versions);
  bool tablehub_updated = false, micpods_updated = false,
       splitters_updated = false, audio_updated = false,
       audio_ble_updated = false, video_updated = false, eeprom_updated = false,
       video_ble_updated = false;
  std::string device_version;
  std::string image_version;
  do {
    // Timeout (30 min) so that the application does not hang while polling.
    if (timeout == kLogiPollingMaxTimeout) {
      LOG(ERROR) << "Polling update progress timed out.";
      return kLogiErrorTimeout;
    }
    // Initialize SystemTopology calling default initializers for vectors and
    // strings contained within it.
    topology = SystemTopology{};
    if (!audio->IsPresent())
      continue;
    if (!audio->OpenDevice())
      error = audio->ReadSystemTopology(&topology);
    if (error) {
      continue;
    }
    if (topology.tablehub.id != 0 && !tablehub_updated) {
      device_version = GetDeviceStringVersion(topology.tablehub.ver_major,
                                              topology.tablehub.ver_minor,
                                              topology.tablehub.ver_build);
      image_version = image_versions[kTableHubSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Table hub firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated table hub firmware.";
        tablehub_updated = true;
        count--;
      }
    }
    if (topology.micpods.size() > 0 && !micpods_updated) {
      micpods_updated = true;
      for (auto micpod : topology.micpods) {
        device_version = GetDeviceStringVersion(
            micpod.ver_major, micpod.ver_minor, micpod.ver_build);
        image_version = image_versions[kMicPodSearchString];
        if (CompareVersions(device_version, image_version) != 0)
          micpods_updated = false;
      }
      if (micpods_updated) {
        if (timeout == 0)
          LOG(INFO) << "Micpod firmware(s) up to date.";
        else
          LOG(INFO) << "Successfully updated micpod firmware(s).";
        count -= topology.micpods.size();
      }
    }
    if (topology.splitters.size() > 0 && !splitters_updated) {
      splitters_updated = true;
      for (auto splitter : topology.splitters) {
        device_version = GetDeviceStringVersion(
            splitter.ver_major, splitter.ver_minor, splitter.ver_build);
        image_version = image_versions[kSplitterSearchString];
        if (CompareVersions(device_version, image_version) != 0)
          splitters_updated = false;
      }
      if (splitters_updated) {
        if (timeout == 0)
          LOG(INFO) << "Micpod hub firmware(s) up to date.";
        else
          LOG(INFO) << "Successfully updated splitter firmware(s).";
        count -= topology.splitters.size();
      }
    }
    if (topology.audio_version.length() != 0 && !audio_updated) {
      device_version = topology.audio_version;
      image_version = image_versions[kAudioSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Audio firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated audio firmware.";
        audio_updated = true;
        count--;
      }
    }
    if (topology.audio_ble_version.length() != 0 && !audio_ble_updated) {
      device_version = topology.audio_ble_version;
      image_version = image_versions[kAudioBleSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Audio ble firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated audio ble firmware.";
        audio_ble_updated = true;
        count--;
      }
    }
    if (topology.video_version.length() != 0 && !video_updated) {
      device_version = topology.video_version;
      image_version = image_versions[kVideoSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Video firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated video firmware.";
        video_updated = true;
        count--;
      }
    }
    if (topology.video_eeprom_version.length() != 0 && !eeprom_updated) {
      device_version = topology.video_eeprom_version;
      image_version = image_versions[kEepromSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Eeprom firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated eeprom firmware.";
        eeprom_updated = true;
        count--;
      }
    }
    if (topology.video_ble_version.length() != 0 && !video_ble_updated) {
      device_version = topology.video_ble_version;
      image_version = image_versions[kVideoBleSearchString];
      if (CompareVersions(device_version, image_version) == 0) {
        if (timeout == 0)
          LOG(INFO) << "Video ble firmware is up to date.";
        else
          LOG(INFO) << "Successfully updated video ble firmware.";
        video_ble_updated = true;
        count--;
      }
    }
    timeout++;
    std::this_thread::sleep_for(std::chrono::milliseconds(kLogiPollingSleep));
  } while (count > 0);
  return kLogiErrorNoError;
}

int CompositeDevice::GetComponentCount(int& count,
                                       const SystemTopology& topology) {
  count = 0;
  if (topology.tablehub.id == kLogiTableHubId)
    count++;
  if (topology.micpods.size() > 0)
    count += topology.micpods.size();
  if (topology.splitters.size() > 0)
    count += topology.splitters.size();
  if (topology.audio_version.length() != 0)
    count++;
  if (topology.audio_ble_version.length() != 0)
    count++;
  if (topology.video_version.length() != 0 &&
      CompareVersions(topology.video_version, kLogiDefaultRallyVideoVersion) <
          0)

    count++;
  if (topology.video_eeprom_version.length() != 0 &&
      CompareVersions(topology.video_eeprom_version,
                      kLogiDefaultRallyEepromVersion) < 0)
    count++;
  if (topology.video_ble_version.length() != 0 &&
      CompareVersions(topology.video_ble_version,
                      kLogiDefaultRallyVideoBleVersion) < 0)
    count++;
  return kLogiErrorNoError;
}

int CompositeDevice::CheckMCUUpdate(bool* skip_update,
                                    std::string device_name) {
  *skip_update = false;
  std::map<int, std::string> image_info_map;
  int error = GetImagesVersion(image_info_map);
  if (error) {
    LOG(ERROR) << "Failed to determine Video image version for "
               << device_name.c_str();
    return error;
  }
  std::map<int, std::string> device_info_map;
  error = GetDevicesVersion(device_info_map);
  if (error) {
    LOG(ERROR) << "Failed to determine Video device version for "
               << device_name.c_str();
    return error;
  }
  if (CompareVersions(device_info_map[kLogiDeviceVideo],
                      image_info_map[kLogiDeviceVideo]) != 0) {
    LOG(INFO) << "Skipping MCU upgrade. Let new Video firmware "
              << image_info_map[kLogiDeviceVideo].c_str() << ", upgrade MCU";
    *skip_update = true;
    return kLogiErrorNoError;
  }
  if (CompareVersions(device_info_map[kLogiDeviceMcu2],
                      image_info_map[kLogiDeviceMcu2]) != 0) {
    LOG(INFO) << "New Video firmware updating MCU firmware. Device will be "
                 "reset after update";
    *skip_update = true;
    return kLogiErrorNoError;
  }
  // If here then MCU update needed
  return kLogiErrorNoError;
}
