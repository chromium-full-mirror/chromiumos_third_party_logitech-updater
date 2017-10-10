// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_device.h"
#include <base/logging.h>
#include <brillo/syslog_logging.h>
#include <fcntl.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include "utilities.h"

const char kDefaultVideoDeviceMountPoint[] = "/sys/class/video4linux";
constexpr unsigned int kLogiDeviceVersionDataSize = 4;
constexpr unsigned char kLogiCameraVersionSelector = 1;
constexpr unsigned int kLogiVideoImageVersionMaxSize = 32;
constexpr unsigned char kLogiVideoAitInitiateSetMMPData = 1;
constexpr unsigned char kLogiVideoAitFinalizeSetMMPData = 1;
// 2 byte for get len query.
constexpr unsigned int kDefaultUvcGetLenQueryControlSize = 2;

constexpr unsigned int kLogiVideoAitSendImageInitialOffset = 0;
constexpr unsigned int kLogiDefaultAitSleepIntervalMs = 2000;
constexpr unsigned char kLogiUvcXuAitCustomCsGetMmpResult = 0x05;
constexpr unsigned char kLogiUvcXuAitCustomCsSetMmp = 0x04;
constexpr unsigned char kLogiUvcXuAitCustomCsSetFwData = 0x03;
constexpr unsigned char kLogiUvcXuAitCustomReboot = 0x11;
// when finalize Ait, max polling duration is 120s.
constexpr unsigned int kLogiDefaultAitFinalizeMaxPollingDurationMs = 120000;
constexpr unsigned int kLogiDefaultAitFirstPassSleepIntervalMs = 8000;
constexpr unsigned char kLogiDefaultAitSuccessValue = 0x00;
constexpr unsigned char kLogiDefaultAitFailureValue = 0x82;
// 32 byte size for updating each trunk.
constexpr unsigned int kLogiDefaultImageBlockSize = 32;

VideoDevice::VideoDevice(std::string pid) : USBDevice(pid, kLogiDeviceVideo) {}

VideoDevice::VideoDevice(std::string pid, int type) : USBDevice(pid, type) {}

VideoDevice::~VideoDevice() {
  closeDevice();
}

std::vector<std::string> VideoDevice::findDevices() {
  std::vector<std::string> devicePaths;

  if (usbPid.empty())
    return devicePaths;

  std::vector<std::string> contents;
  bool getOk = GetDirectoryContents(kDefaultVideoDeviceMountPoint, &contents);
  if (!getOk)
    return devicePaths;  // failed to get contents of mount point

  for (auto const& content : contents) {
    if (content.compare(".") == 0 || content.compare("..") == 0)
      continue;  // ignore . and .. directory

    std::string videoDir =
        std::string(kDefaultVideoDeviceMountPoint) + "/" + content;

    // read mount point device vid & pid here
    std::string productIdPath = videoDir + "/device/../idProduct";
    std::string vendorIdPath = videoDir + "/device/../idVendor";

    std::string vendorID;
    std::string productID;
    if (!ReadFileContent(vendorIdPath, &vendorID))
      continue;  // failed to read vendor ID

    if (!IsLogitechVendorID(vendorID))
      continue;  // not Logitech device

    if (!ReadFileContent(productIdPath, &productID))
      continue;  // failed to read product ID

    int pidNum = 0;
    if (!ConvertHexStringToInt(productID, &pidNum))
      continue;  // failed to convert product ID to int

    int usbPidNum = 0;
    if (!ConvertHexStringToInt(usbPid, &usbPidNum))
      continue;

    if (pidNum == usbPidNum) {
      // found device, add it to the device paths
      std::string devPath = "/dev/" + content;
      devicePaths.push_back(devPath);
    }
  }
  return devicePaths;
}

int VideoDevice::openDevice() {
  if (usbPid.empty())
    return kLogiErrorUsbPidNotFound;
  std::vector<std::string> devPaths = findDevices();
  if (devPaths.size() == 0)
    return kLogiErrorUsbPidNotFound;
  if (devPaths.size() > 1)
    return kLogiErrorMultipleDevicesFound;
  const char* devPath = devPaths.at(0).c_str();
  int fd = open(devPath, O_RDWR | O_NONBLOCK, 0);
  if (fd == -1)  // open() return -1 if failed....
    return kLogiErrorOpenDeviceFailed;
  // everything is ok
  isOpen = true;
  fileDescriptor = fd;
  return kLogiErrorNoError;
}

void VideoDevice::closeDevice() {
  if (fileDescriptor >= 0)
    close(fileDescriptor);

  fileDescriptor = -1;
  isOpen = false;
}

int VideoDevice::getDeviceName(std::string* deviceName) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // get device name here
  struct v4l2_capability videoCap;
  int error = ioctl(fileDescriptor, VIDIOC_QUERYCAP, &videoCap);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  std::string str(videoCap.card, videoCap.card + sizeof(videoCap.card) /
                                                     sizeof(videoCap.card[0]));

  *deviceName = str;
  return kLogiErrorNoError;
}

int VideoDevice::readDeviceVersion(std::string* deviceVersion) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;
  std::vector<unsigned char> outputData;
  int unitId = GetUnitID(kLogiGuidDeviceInfo);
  int error = getXuControl(unitId, kLogiCameraVersionSelector, &outputData);
  if (error)
    return error;
  if (outputData.size() < kLogiDeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;

  // little-endian data
  int major = (int)outputData[1];
  int minor = (int)outputData[0];
  int build = (int)((outputData[3] << 8) | outputData[2]);
  *deviceVersion = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int VideoDevice::getImageVersion(std::vector<char> buffer,
                                 std::string* imageVersion) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  // Find the start of the version string.
  // The version information is located at an arbitrary position coded in the
  // following format: VERSIONMAGIC<x.y.z> As a sanity check the length of the
  // version "x.y.z" string may not exceed 32 characters.
  std::vector<uint8_t> versionMagic = {'V', 'E', 'R', 'S', 'I', 'O', 'N',
                                       'M', 'A', 'G', 'I', 'C', '<'};
  auto iter = std::search(std::begin(buffer), std::end(buffer),
                          std::begin(versionMagic), std::end(versionMagic));
  if (iter == std::end(buffer))
    return kLogiErrorImageVersionNotFound;

  // Copy all characters until the closing '>' marker.
  std::string version;
  std::advance(iter, versionMagic.size());
  while (iter != std::end(buffer) && *iter != '>') {
    version.push_back(*iter);
    if (version.size() > kLogiVideoImageVersionMaxSize)
      return kLogiErrorImageVersionExceededMaxSize;
    ++iter;
  }

  *imageVersion = version;
  return kLogiErrorNoError;
}

int VideoDevice::verifyImage(std::vector<char> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  std::vector<char> signature = {'A', 'I', 'T', '8', '4', '2'};
  if (buffer.size() < signature.size())
    return kLogiErrorImageVerificationFailed;
  auto iter =
      std::search(std::begin(buffer), std::begin(buffer) + signature.size(),
                  std::begin(signature), std::end(signature));
  if (iter != std::begin(buffer))
    return kLogiErrorImageVerificationFailed;
  return kLogiErrorNoError;
}

int VideoDevice::aitInitiateUpdate() {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // Disclaimer: Any magic numbers come directly from the FlashGordon code.

  // set the MMP
  std::vector<unsigned char> mmpSetData = {kLogiAitSetMmpCmdFwBurning,
                                           0,
                                           0,
                                           kLogiVideoAitInitiateSetMMPData,
                                           0,
                                           0,
                                           0,
                                           0};
  return aitInitiateUpdateWithData(mmpSetData);
}

int VideoDevice::aitSendImage(std::vector<char> buffer) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  return aitSendImageWithOffset(buffer, kLogiVideoAitSendImageInitialOffset);
}

int VideoDevice::aitFinalizeUpdate() {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code.
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // set MMP
  std::vector<unsigned char> setData = {kLogiAitSetMmpCmdFwBurning,
                                        kLogiVideoAitFinalizeSetMMPData,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0};  // always 8 bytes
  return aitFinalizeUpdateWithData(setData);
}

int VideoDevice::rebootDevice() {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  int unitId = GetUnitID(kLogiGuidPeripheralControl);
  std::vector<unsigned char> data = {1};
  return setXuControl(unitId, kLogiUvcXuAitCustomReboot, data);
}

int VideoDevice::aitInitiateUpdateWithData(std::vector<unsigned char> mmpData) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // Disclaimer: Any magic numbers come directly from the FlashGordon code.

  // set the MMP
  int unitId = GetUnitID(kLogiGuidAITCustom);
  int error = setXuControl(unitId, kLogiUvcXuAitCustomCsSetMmp, mmpData);
  if (error) {
    LOG(ERROR) << "Failed to set command for AIT initialization. Error code: "
               << error;
    return error;
  }

  // get the MMP
  std::vector<unsigned char> mmpGetData;
  error = getXuControl(unitId, kLogiUvcXuAitCustomCsGetMmpResult, &mmpGetData);
  if (error) {
    LOG(ERROR) << "Failed to get result for AIT initialization. Error code: "
               << error;
    return error;
  }
  if (mmpGetData.empty() || mmpGetData[0] != 0) {
    LOG(ERROR) << "Failed to initialize AIT update. Invalid result data.";
    return kLogiErrorVideoDeviceAitInitiateGetDataFailed;
  }

  return kLogiErrorNoError;
}

int VideoDevice::aitSendImageWithOffset(std::vector<char> buffer,
                                        unsigned int offset) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  int unitId = GetUnitID(kLogiGuidAITCustom);
  unsigned int remainingSize = buffer.size() - offset;
  while (remainingSize > 0) {
    // get the blockSize
    unsigned int blockSize =
        std::min<unsigned int>(remainingSize, kLogiDefaultImageBlockSize);

    // prepare the blockData
    std::vector<unsigned char> data(kLogiDefaultImageBlockSize);
    std::fill(std::begin(data), std::end(data), 0);
    std::copy(buffer.cbegin() + offset, buffer.cbegin() + offset + blockSize,
              std::begin(data));

    int error = setXuControl(unitId, kLogiUvcXuAitCustomCsSetFwData, data);
    if (error) {
      LOG(ERROR) << "Failed to send block data at offset: " << offset
                 << ". Error code: " << error;
      return error;
    }
    offset += blockSize;
    remainingSize -= blockSize;
  }
  return kLogiErrorNoError;
}

int VideoDevice::aitFinalizeUpdateWithData(std::vector<unsigned char> mmpData) {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code.
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  // set MMP
  int unitId = GetUnitID(kLogiGuidAITCustom);
  int error = setXuControl(unitId, kLogiUvcXuAitCustomCsSetMmp, mmpData);
  if (error) {
    LOG(ERROR) << "Failed to set command for AIT finalization. Error code: "
               << error;
    return error;
  }

  // get MMP data
  // Poll until the device returns either success or failure.
  auto durationMs = 0;
  const auto doSleep = [&](int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    durationMs += ms;
  };

  // This is the way AIT chipset is finalized (based on AIT finalizing method).
  // Try to poll for burning fw result or return failure if hit max polling
  // duration.
  for (int pass = 0;; pass++) {
    std::vector<unsigned char> data;
    int attempts = 3;

    doSleep(kLogiDefaultAitSleepIntervalMs);
    // Sometimes on a very fast cpu, AIT chip returns error when querying
    // extension control unit on first time. If fails, attempt to retry for 3
    // times before returning error.
    do {
      error = getXuControl(unitId, kLogiUvcXuAitCustomCsGetMmpResult, &data);
      if (error) {
        LOG(ERROR) << "Failed to get result for AIT finalization. Error code: "
                   << error << ". Attempt to retry: " << attempts;
        doSleep(kLogiDefaultAitSleepIntervalMs);
      }

      attempts--;
    } while (attempts > 0 && error);

    if (error || data.empty()) {
      LOG(ERROR) << "Failed to finalize AIT update. Invalid result data.";
      return kLogiErrorImageBurningFinalizeFailed;
    }

    // process the response
    if (data.size() > 0) {
      if (data[0] == kLogiDefaultAitSuccessValue) {
        // If there is not positive response from the device that the flash was
        // written  estimate the time needed plus a time buffer to determine
        // when the flash write process has completed.  In this case, time
        // buffer is about 8s for the flash write operation to complete  This is
        // done using both device spec. and stress testings.
        if (pass == 0)
          doSleep(kLogiDefaultAitFirstPassSleepIntervalMs);
        break;
      } else if (data[0] == kLogiDefaultAitFailureValue) {
        LOG(ERROR) << "Failed to finalize AIT update. Failure result data.";
        return kLogiErrorImageBurningFinalizeFailed;
      }
    }
    if (durationMs > kLogiDefaultAitFinalizeMaxPollingDurationMs) {
      // In case device never returns 0x82 or 0x00, bail out.
      LOG(ERROR) << "Failed to finalize AIT update. Checking result timeout.";
      return kLogiErrorImageBurningFinalizeFailed;
    }
  }
  return kLogiErrorNoError;
}

int VideoDevice::performUpdate(std::vector<char> buffer, bool force) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  if (deviceType == kLogiDeviceVideo) {
    LOG(INFO) << "Updating video firmware...";
  } else if (deviceType == kLogiDeviceMcu2) {
    LOG(INFO) << "Updating MCU2 firmware...";
  }

  std::string deviceVersion;
  std::string imageVersion;
  int error = getDeviceVersion(&deviceVersion);
  if (error)
    return error;

  error = verifyImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to verify image. Error code: " << error;
    return error;
  }

  error = getImageVersion(buffer, &imageVersion);
  if (error)
    return error;

  // check if update available
  int compareVersion = CompareVersions(deviceVersion, imageVersion);
  if (compareVersion < 0 || force) {
    // initiate AIT update
    error = aitInitiateUpdate();
    if (error) {
      LOG(ERROR) << "Failed to initilize update. Error code: " << error;
      return error;
    }

    // send image
    error = aitSendImage(buffer);
    if (error)
      return error;

    // finalize AIT update
    error = aitFinalizeUpdate();
    if (error) {
      LOG(ERROR) << "Failed to finalize update. Error code: " << error;
    } else {
      LOG(INFO) << "Successfully updated firmware.";
    }
  }
  return error;
}

int VideoDevice::setXuControl(unsigned char unitId,
                              unsigned char controlSelector,
                              std::vector<unsigned char> data) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  if (unitId < 0)
    return kLogiErrorXuUnitIdInvalid;

  struct uvc_xu_control_query controlQuery;
  controlQuery.unit = static_cast<uint8_t>(unitId);
  controlQuery.selector = static_cast<uint8_t>(controlSelector);
  controlQuery.query = UVC_SET_CUR;
  controlQuery.size = data.size();
  controlQuery.data = data.data();
  // A few ioctl requests use return value as an output parameter
  // and return a nonnegative value on success, so should check
  // for real error before returning.
  int error = ioctl(fileDescriptor, UVCIOC_CTRL_QUERY, &controlQuery);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int VideoDevice::getXuControl(unsigned char unitId,
                              unsigned char controlSelector,
                              std::vector<unsigned char>* data) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  if (unitId < 0)
    return kLogiErrorXuUnitIdInvalid;

  int dataLen;
  int error = queryDataSize(unitId, controlSelector, &dataLen);
  if (error)
    return error;
  uint8_t queryData[dataLen];
  memset(queryData, '\0', sizeof(queryData));

  struct uvc_xu_control_query controlQuery;
  controlQuery.unit = unitId;
  controlQuery.selector = controlSelector;
  controlQuery.query = UVC_GET_CUR;
  controlQuery.size = dataLen;
  controlQuery.data = queryData;
  error = ioctl(fileDescriptor, UVCIOC_CTRL_QUERY, &controlQuery);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  for (int i = 0; i < dataLen; i++) {
    data->push_back(queryData[i]);
  }
  return kLogiErrorNoError;
}

int VideoDevice::queryDataSize(unsigned char unitId,
                               unsigned char controlSelector,
                               int* dataSize) {
  if (!isOpen)
    return kLogiErrorDeviceNotOpen;

  uint8_t sizeData[kDefaultUvcGetLenQueryControlSize];
  struct uvc_xu_control_query sizeQuery;
  sizeQuery.unit = unitId;
  sizeQuery.selector = controlSelector;
  sizeQuery.query = UVC_GET_LEN;
  sizeQuery.size = kDefaultUvcGetLenQueryControlSize;
  sizeQuery.data = sizeData;
  int error = ioctl(fileDescriptor, UVCIOC_CTRL_QUERY, &sizeQuery);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  // convert the data byte to int
  int size = (sizeData[1] << 8) | (sizeData[0]);
  *dataSize = size;
  return kLogiErrorNoError;
}
