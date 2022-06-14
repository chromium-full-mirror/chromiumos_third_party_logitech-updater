// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ble_device.h"

#include <algorithm>
#include <thread>

#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "utilities.h"

// Default set command data size for BLE device.
constexpr int kLogiBleSetCommandDataSize = 141;
// HID report number to set command for BLE device.
constexpr int kLogiReportNumberBleSetCommand = 0x60;
// HID report number to get command for BLE device.
constexpr int kLogiReportNumberBleGetCommand = 0x61;
// HID report data to set value (true/1) for BLE device.
constexpr int kLogiReportDataBleSetValue = 0x01;
// HID report data to set command for BLE device.
constexpr int kLogiReportDataBleSetCommand = 0x0A;
// HID data for BLE control flag.
constexpr int kLogiBleControlFlag = 0x00;
// 5 bytes for BLE version info.
constexpr int kLogiBleVersionInfoByteSize = 5;
constexpr int kLogiBleDefaultSleepTimeMs = 100;
constexpr int kLogiBleGetInitialPacketMaxRetries = 100;
// Block data size to send to the device when update.
constexpr int kLogiBleBlockDataSize = 128;
constexpr int kLogiBleDefaultReportStatusDataSize = 6;
constexpr int kLogiBleSendImageReportStatusDataSize = 40;
constexpr int kLogiBleGetStatusReportIntervalMs = 500;
constexpr int kLogiBleInitialPacketSubcommand = 0x01;
constexpr int kLogiBleInitialPacketDataType = 0x04;
constexpr int kLogiBleChecksumSubcommand = 0x02;
constexpr int kLogeBleStopPacketSubcommand = 0x04;

BleDevice::BleDevice(std::string pid) : AudioDevice(pid, kLogiDeviceBle) {}

BleDevice::~BleDevice() {}

bool BleDevice::IsPresent() {
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultAudioDeviceMountPoint, kDefaultAudioDevicePoint, usb_pid_);
  return ((dev_paths.size() > 0) ? true : false);
}

int BleDevice::OpenDevice() {
  return OpenDeviceInHidMode();
}

int BleDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<uint8_t> set_data(kLogiBleSetCommandDataSize);
  set_data[0] = kLogiReportNumberBleSetCommand;
  set_data[1] = kLogiReportDataBleSetValue;
  int error =
      ioctl(file_descriptor_, HIDIOCSFEATURE(set_data.size()), set_data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  std::vector<uint8_t> data = {kLogiReportNumberBleGetCommand, 0, 0, 0, 0, 0};
  error = ioctl(file_descriptor_, HIDIOCGFEATURE(data.size()), data.data());
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  uint8_t build = (data[3] << 8) | data[2];
  *device_version = GetDeviceStringVersion(data[5], data[4], build);
  return kLogiErrorNoError;
}

int BleDevice::GetImageVersion(std::vector<uint8_t> buffer,
                               std::string* image_version) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Search for 10 magic bytes. If found, read 5 bytes before the pointer.
  // First byte is most significant byte build number.
  // Second byte is least significant byte build number.
  // Third byte is most significant byte minor number.
  // Fourth byte is least significant byte minor number.
  // Fifth byte is major number.
  std::vector<uint8_t> magic_word = {0x08, 0xB5, 0x07, 0x48, 0x00,
                                     0x90, 0x69, 0x46, 0x06, 0x48};

  if (buffer.size() < magic_word.size() + kLogiBleVersionInfoByteSize)
    return kLogiErrorImageVersionSizeTooShort;

  auto iter = std::search(std::begin(buffer), std::end(buffer),
                          std::begin(magic_word), std::end(magic_word));
  if (iter == std::end(buffer))
    return kLogiErrorImageVersionNotFound;

  std::vector<uint8_t> version_info(kLogiBleVersionInfoByteSize);
  std::copy(iter - kLogiBleVersionInfoByteSize, iter, std::begin(version_info));
  int build = (version_info[0] << 8) | version_info[1];
  int minor = (version_info[2] << 8) | version_info[3];
  int major = version_info[4];
  *image_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int BleDevice::VerifyImage(std::vector<uint8_t> buffer) {
  // Ble device does not have secured signatures embeeded. Just checking if
  // the buffer is empty or not.
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  return kLogiErrorNoError;
}

int BleDevice::PerformUpdate(std::vector<uint8_t> buffer,
                             std::vector<uint8_t> secure_header,
                             bool* did_update) {
  *did_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  LOG(INFO) << "Checking BLE firmware...";
  // Check device version and verifies image.
  bool should_update;
  int error = CheckForUpdate(buffer, &should_update);
  if (error || !should_update)
    return error;

  LOG(INFO) << "BLE firmware is not up to date. Updating firmware...";

  // Note: GetDeviceName here will return kLogiErrorDeviceNameNotAvailable
  std::string device_name = "Logitech Camera BLE";
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(device_name);
  dfu_notification->NotifyStartUpdate();

  // Send initial packet to the device.
  error = SendInitialPacket(buffer.size());
  if (error) {
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Send checksum to the device.
  error = SendChecksum(buffer);
  if (error) {
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Send image to the device.
  error = SendBleImage(buffer);
  if (error) {
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  // Send stop-packet to device.
  error = SendStopPacket(buffer);
  if (error) {
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }

  CloseDevice();
  error = WaitForHidDevice();
  if (error) {
    LOG(ERROR) << "Failed to wait for HID device to come back. Error: "
               << error;
  } else {
    LOG(INFO) << "Successfully updated BLE firmware.";
    *did_update = true;
  }

  dfu_notification->NotifyEndUpdate(!error);

  return kLogiErrorNoError;
}

uint16_t BleDevice::ComputeBlockCRC(const uint8_t* data,
                                    int size,
                                    uint16_t pre_crc) {
  if (data == NULL)
    return 0;

  uint16_t crc = pre_crc;
  for (int i = 0; i < size; i++) {
    crc = (crc >> 8) | (crc << 8);
    crc ^= data[i];
    crc ^= (crc & 0xFF) >> 4;
    crc ^= crc << 12;
    crc ^= (crc & 0xFF) << 5;
  }
  return crc;
}

uint16_t BleDevice::ComputeChecksum(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return 0;

  uint16_t checksum = 0xFFFF;
  int offset = 0;
  int remaining_size = buffer.size();
  while (remaining_size > 0) {
    uint8_t* block_data = buffer.data() + offset;
    int size = std::min(remaining_size, kLogiBleBlockDataSize);
    checksum = ComputeBlockCRC(block_data, size, checksum);
    offset += size;
    remaining_size -= size;
  }
  return checksum;
}

int BleDevice::GetPacketReportStatus(int report_size) {
  if (report_size <= 0)
    return kLogiErrorIOControlInvalidDataSize;
  std::vector<uint8_t> data(report_size);
  data[0] = kLogiReportNumberBleGetCommand;
  for (int i = 0; i < kLogiBleGetInitialPacketMaxRetries; i++) {
    int error =
        ioctl(file_descriptor_, HIDIOCGFEATURE(data.size()), data.data());
    if (error >= 0 && data[1] == kLogiReportDataBleSetCommand)
      return kLogiErrorNoError;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiBleGetStatusReportIntervalMs));
  }
  return kLogiErrorIOControlOperationFailed;
}

int BleDevice::SendBleImage(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  int offset = 0;
  int remaining_size = buffer.size();
  while (remaining_size > 0) {
    uint8_t block_size = std::min(remaining_size, kLogiBleBlockDataSize);
    std::vector<uint8_t> data(kLogiBleSetCommandDataSize);
    data[0] = kLogiReportNumberBleSetCommand;
    data[1] = kLogiReportDataBleSetCommand;
    data[2] = kLogiBleControlFlag;
    data[3] = 0x03;
    // Data[4] to data[7] is block size.
    data[4] = block_size & 0xFF;
    data[5] = block_size >> 8 & 0xFF;
    data[6] = block_size >> 16 & 0xFF;
    data[7] = block_size >> 24 & 0xFF;

    std::copy(buffer.cbegin() + offset, buffer.cbegin() + offset + block_size,
              data.begin() + 8);
    int error =
        ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
    if (error < 0) {
      LOG(ERROR) << "Failed to send binary image at offet " << offset;
      return kLogiErrorIOControlOperationFailed;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kLogiBleDefaultSleepTimeMs));

    error = GetPacketReportStatus(kLogiBleSendImageReportStatusDataSize);
    if (error) {
      LOG(ERROR) << "Failed to check status after sending packet at offset "
                 << offset;
      return error;
    }
    offset += block_size;
    remaining_size -= block_size;
  }
  return kLogiErrorNoError;
}

int BleDevice::SendInitialPacket(int buffer_size) {
  // Set BLE initial packet.
  std::vector<uint8_t> data(kLogiBleSetCommandDataSize);
  data[0] = kLogiReportNumberBleSetCommand;
  data[1] = kLogiReportDataBleSetCommand;
  data[2] = kLogiBleControlFlag;
  data[3] = kLogiBleInitialPacketSubcommand;
  data[4] = kLogiBleInitialPacketDataType;
  // Data[5] to data[8] is initial version 0xFFFFFFFF.
  data[5] = 0xFF;
  data[6] = 0xFF;
  data[7] = 0xFF;
  data[8] = 0xFF;
  // Data[9] to data[12] is buffer size.
  data[9] = buffer_size & 0xFF;
  data[10] = buffer_size >> 8 & 0xFF;
  data[11] = buffer_size >> 16 & 0xFF;
  data[12] = buffer_size >> 24 & 0xFF;

  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to send initial packet to the device. Error: "
               << error;
    return kLogiErrorIOControlOperationFailed;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiBleDefaultSleepTimeMs));

  // Get the initial packet from the device after sending.
  error = GetPacketReportStatus(kLogiBleDefaultReportStatusDataSize);
  if (error) {
    LOG(ERROR) << "Failed to check initial packet status. Error: " << error;
    return error;
  }
  return kLogiErrorNoError;
}

std::vector<uint8_t> BleDevice::ComputeChecksumPacket(
    std::vector<uint8_t> buffer, uint8_t subcommand) {
  uint16_t checksum = ComputeChecksum(buffer);
  std::vector<uint8_t> data(kLogiBleSetCommandDataSize);
  data[0] = kLogiReportNumberBleSetCommand;
  data[1] = kLogiReportDataBleSetCommand;
  data[2] = kLogiBleControlFlag;
  data[3] = subcommand;
  data[4] = checksum & 0xFF;
  data[5] = checksum >> 8 & 0xFF;
  return data;
}

int BleDevice::SendChecksum(std::vector<uint8_t> buffer) {
  std::vector<uint8_t> data =
      ComputeChecksumPacket(buffer, kLogiBleChecksumSubcommand);
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to send checksum data to device. Error: " << error;
    return kLogiErrorIOControlOperationFailed;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiBleDefaultSleepTimeMs));

  // Check status of checksum packet.
  error = GetPacketReportStatus(kLogiBleDefaultReportStatusDataSize);
  if (error) {
    LOG(ERROR) << "Failed to check checksum packet status. Error: " << error;
    return error;
  }
  return kLogiErrorNoError;
}

int BleDevice::SendStopPacket(std::vector<uint8_t> buffer) {
  std::vector<uint8_t> data =
      ComputeChecksumPacket(buffer, kLogeBleStopPacketSubcommand);
  int error = ioctl(file_descriptor_, HIDIOCSFEATURE(data.size()), data.data());
  if (error < 0) {
    LOG(ERROR) << "Failed to send stop-packet to device. Error: " << error;
    return kLogiErrorIOControlOperationFailed;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(kLogiBleDefaultSleepTimeMs));

  // Check stop-packet status report.
  error = GetPacketReportStatus(kLogiBleDefaultReportStatusDataSize);
  if (error) {
    LOG(ERROR) << "Failed to check stop-packet report status. Error: " << error;
    return error;
  }
  return kLogiErrorNoError;
}
