// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_device.h"

#include <algorithm>
#include <thread>

#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "utilities.h"

// HID report data of the address to read value from.
constexpr int kLogiReportDataReadAddress = 0x01;
// Address to read codec firmware version from.
constexpr int kLogiCodecReadVersionAddress = 0x06;
// Minimum size of image buffer to be able to read image version.
constexpr int kLogiCodecMinImageSizeForReadingVersion = 0x31;
// HID report number to set command for codec device.
constexpr int kLogiReportNumberCodecSetCommand = 0x1F;
// HID report number to sent data for codec device.
constexpr int kLogiReportNumberCodecSetData = 0x1C;
// HID report data to set codec device into download mode.
constexpr int kLogiReportDataCodecSetDownloadMode = 0x02;
// HID report data to send file size to the device.
constexpr int kLogiReportDataCodecSendFileSize = 0x06;
// HID report data to verify updated image for codec device.
constexpr int kLogiReportDataCodecVerifyImage = 0x05;
// HID report data to reset the codec device.
constexpr int kLogiReportDataCodecReset = 0x04;
// Byte size of image version info.
constexpr int kLogiCodecImageVersionByteSize = 12;
// Getting status after sending data time out.
constexpr int kLogiCodecGetStatusTimeoutMs = 20000;
// Getting status time interval.
constexpr int kLogiCodecGetStatusIntervalMs = 10;
// Time for codec device to verify the updated image.
constexpr int kLogiCodecVerifyImageWaitTimeMs = 300;
// Codec device is busy downloading data.
constexpr int kLogiCodecStatusDownloadBusy = 0x04;
// 20s wait time to set codec to download mode.
constexpr int kLogiCodecSetDownloadModeWaitTime = 20;
constexpr int kLogiCodecStatusUpdateFailed = 0x0A;
constexpr int kLogiCodecStatusAppIdle = 0x00;

CodecDevice::CodecDevice(std::string pid)
    : AudioDevice(pid, kLogiDeviceCodec) {}

CodecDevice::~CodecDevice() {}

bool CodecDevice::IsPresent() {
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, usb_pid_);
  return ((dev_paths.size() > 0) ? true : false);
}

int CodecDevice::OpenDevice() {
  return OpenDeviceInHidMode();
}

int CodecDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data = {kLogiReportNumberSetInfo,
                               kLogiReportDataReadAddress,
                               kLogiCodecReadVersionAddress, 0, 0};

  // Sets device into version querying state first.
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  // Reads device version info from the device.
  std::vector<uint8_t> query_data = {kLogiReportNumberGetInfo, 0, 0, 0, 0};
  error = ioctl(file_descriptor_, HIDIOCGFEATURE(query_data.size()),
                query_data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  int major = query_data[1];
  int minor = query_data[2];
  int build = (query_data[3] << 8) | query_data[4];

  // When codec fails to update and corrupted, its version can go back to
  // 255.255.65535. When this happens, compare version will always return that
  // device is up to date. Set major version to 1 so it can be updated again.
  if (major > 0xC8)
    major = 1;
  *device_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int CodecDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                 std::string* image_version) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  if (buffer.size() < kLogiCodecMinImageSizeForReadingVersion)
    return kLogiErrorImageVersionSizeTooShort;

  // Searches for the word MAGIC. Since Sharc compiler generates 4 bytes for
  // every characters (ASCII), each char will be prepended with 3 0's.
  // 1. If MAGIC found, the next 12 bytes is the version number.
  // 2. First 4 bytes is major version, next 4 bytes is minor, last 4 is build.
  // 3. Since the chip reads from LSB first, data needs to be bit-reverse.
  // For example, if the read data is 0xA0 (1010 0000), reverses it to
  // 0x05 (0000 0101).
  std::vector<uint8_t> magic_word = {0x4d, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00,
                                     0x00, 0x47, 0x00, 0x00, 0x00, 0x49, 0x00,
                                     0x00, 0x00, 0x43, 0x00, 0x00, 0x00};

  auto iter = std::search(std::begin(buffer), std::end(buffer),
                          std::begin(magic_word), std::end(magic_word));
  if (iter == std::end(buffer))
    return kLogiErrorImageVersionNotFound;

  // Advances the iterator to the beginning of version info.
  std::advance(iter, magic_word.size());
  // Extracts the image version info.
  std::vector<uint8_t> version(kLogiCodecImageVersionByteSize);
  std::copy(iter, iter + kLogiCodecImageVersionByteSize, std::begin(version));
  uint8_t major = ReverseBits(version[0]);
  uint8_t minor = ReverseBits(version[4]);
  uint8_t build = (ReverseBits((version[9]) << 8) | ReverseBits(version[8]));
  *image_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int CodecDevice::VerifyImage(std::vector<uint8_t> buffer) {
  // Codec device does not have signatures embedded in the firmware binary.
  // Check for the image size to make sure to be able to read the
  // version from the image.
  if (buffer.size() < kLogiCodecMinImageSizeForReadingVersion)
    return kLogiErrorImageVersionSizeTooShort;
  return kLogiErrorNoError;
}

int CodecDevice::PerformUpdate(std::vector<uint8_t> buffer,
                               std::vector<uint8_t> secure_header,
                               bool* did_update) {
  *did_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  LOG(INFO) << "Checking codec firmware...";
  // The audio is reset after updating, should wait a bit here for the device to
  // come back.
  WaitForHidDevice();
  bool should_update;
  int error = CheckForUpdate(buffer, &should_update);
  if (error || !should_update)
    return error;

  LOG(INFO) << "Codec firmware is not up to date. Updating firmware...";

  // Note: GetDeviceName here will return kLogiErrorDeviceNameNotAvailable
  std::string device_name = "Logitech Camera Codec";
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(device_name);
  dfu_notification->NotifyStartUpdate();

  // 1. Set device into firmware download mode.
  error = SetDownloadMode(kLogiReportNumberCodecSetCommand,
                          kLogiReportDataCodecSetDownloadMode);
  if (error) {
    LOG(ERROR) << "Failed to set device to download mode. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  std::this_thread::sleep_for(
      std::chrono::seconds(kLogiCodecSetDownloadModeWaitTime));

  // 2. Send image file size to the device.
  uint32_t buffer_size = static_cast<uint32_t>(buffer.size());
  error = SendImageFileSize(kLogiReportNumberCodecSetCommand,
                            kLogiReportDataCodecSendFileSize, buffer_size);
  if (error) {
    LOG(ERROR) << "Failed to send image file size. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // 3. Sends secure header to the device.
  if (secure_boot_) {
    error = SendHeaderImage(kLogiReportNumberCodecSetData, secure_header);
    if (error) {
      LOG(ERROR) << "Failed to send header image. Error: " << error;
      dfu_notification->NotifyEndUpdate(false);
      return error;
    }
  }
  // 4. Sends image buffer to the device.
  error = SendImage(kLogiReportNumberCodecSetData, buffer);
  if (error) {
    LOG(ERROR) << "Failed to send image to device. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  // Delays for image verification.
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiCodecGetStatusIntervalMs));

  // 4. Verifies updated image.
  error = VerifyUpdateImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to verify updated image. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiCodecGetStatusIntervalMs));
  // 5. Resets the device.
  error = Reset();
  if (error) {
    LOG(ERROR) << "Failed to reset device. Error: " << error;
  } else {
    LOG(INFO) << "Successfully updated codec firmware.";
    *did_update = true;
  }

  dfu_notification->NotifyEndUpdate(!error);

  return error;
}

int CodecDevice::CheckDeviceStatusAfterSendingData() {
  int duration = 0;
  int error = kLogiErrorNoError;
  while (duration < kLogiCodecGetStatusTimeoutMs) {
    std::vector<uint8_t> data = {kLogiReportNumberGetInfo, 0, 0, 0, 0};
    int result =
        ioctl(file_descriptor_, HIDIOCGFEATURE(data.size()), data.data());
    uint8_t status = static_cast<uint8_t>(data[1]);
    if (result < 0 || status == kLogiCodecStatusDownloadBusy) {
      error = kLogiErrorSendImageFailed;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kLogiCodecGetStatusIntervalMs));
      duration += kLogiCodecGetStatusIntervalMs;
    } else {
      error = kLogiErrorNoError;
      break;
    }
  }

  // This delay is neccessary for codec audio firmware update after stress
  // testings.
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiCodecGetStatusIntervalMs));
  return error;
}

int CodecDevice::VerifyUpdateImage(std::vector<uint8_t> buffer) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  uint32_t checksum = 0;
  for (int i = 0; i < buffer.size(); i++)
    checksum += static_cast<uint8_t>(buffer[i]);
  std::vector<uint8_t> data = {kLogiReportNumberCodecSetCommand,
                               kLogiReportDataCodecVerifyImage,
                               static_cast<uint8_t>(checksum & 0xFF),
                               static_cast<uint8_t>((checksum >> 8) & 0xFF),
                               static_cast<uint8_t>((checksum >> 16) & 0xFF)};
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  // Waits after sending checksum for the device to verify.
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiCodecVerifyImageWaitTimeMs));
  int duration = 0;
  while (duration < kLogiCodecGetStatusTimeoutMs) {
    std::vector<uint8_t> status_data = {kLogiReportNumberGetInfo, 0, 0, 0, 0};
    int result = ioctl(file_descriptor_, HIDIOCGFEATURE(status_data.size()),
                       status_data.data());

    uint8_t status = static_cast<uint8_t>(status_data[1]);
    if (result < 0 || (status != kLogiCodecStatusUpdateFailed &&
                       status != kLogiCodecStatusAppIdle)) {
      // IO Control operation error or the returned status is not a failure or
      // App Idle, wait and retry
      error = kLogiErrorVerifyUpdatedImageFailed;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(kLogiCodecGetStatusIntervalMs));
      duration += kLogiCodecGetStatusIntervalMs;
      LOG(INFO) << "Codec verify image failed... Retrying...";
    } else if (status == kLogiCodecStatusUpdateFailed) {
      return kLogiErrorVerifyUpdatedImageFailed;
    } else {
      // Everything seems to be ok. Return no error here.
      return kLogiErrorNoError;
    }
  }
  return error;
}

int CodecDevice::Reset() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> data = {kLogiReportNumberCodecSetCommand,
                               kLogiReportDataCodecReset, 0, 0, 0};
  // Do not check for IO returned error here because this commands reset the
  // the device immediately. The returned value is usually -1.
  ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());

  // Sets device to command state.
  std::vector<uint8_t> state_data = {kLogiReportNumberSetInfo, 0x0F, 0, 0, 0};
  ioctl(file_descriptor_, HIDIOCSFEATURE(state_data.size()), state_data.data());

  CloseDevice();
  int error = WaitForHidDevice();
  if (error) {
    LOG(ERROR) << "Failed to wait for device to come back after reset. Error: "
               << error;
  }
  return error;
}
