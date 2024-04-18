// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_device.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include <base/logging.h>
#include <cfm-dfu-notification/idfu_notification.h>
#include <fcntl.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>
#include <linux/v4l2-controls.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "utilities.h"

const char kDefaultVideoDeviceMountPoint[] = "/sys/class/video4linux";
const char kDefaultVideoDevicePoint[] = "/device/..";
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

namespace {

/* Returns true of is a valid video capture device, otherwise false. */
bool IsCaptureDevice(int file_descriptor, const std::string& path) {
  if (file_descriptor < 0) {
    return false;
  }

  struct v4l2_capability caps;
  int error = ioctl(file_descriptor, VIDIOC_QUERYCAP, &caps);
  if (error < 0)  // ioctl return -1 on failure.
    return false;

  // If true, the driver determines the device capabilities field.
  uint32_t dev_caps = (caps.capabilities & V4L2_CAP_DEVICE_CAPS)
                          ? caps.device_caps
                          : caps.capabilities;

  // Valid capture devices should not additionally have OUTPUT capability
  // Old drivers use (CAPTURE | OUTPUT) for memory-to-memory video devices.
  const uint32_t kCaptureMask =
      V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
  const uint32_t kOutputMask =
      V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
  const uint32_t kM2mMask = V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE;

  return (dev_caps & kCaptureMask) && !(dev_caps & kOutputMask) &&
         !(dev_caps & kM2mMask);
}

}  //  namespace

VideoDevice::VideoDevice(std::string pid) : USBDevice(pid, kLogiDeviceVideo) {}

VideoDevice::VideoDevice(std::string pid, int type) : USBDevice(pid, type) {}

VideoDevice::~VideoDevice() {}

bool VideoDevice::IsPresent() {
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultVideoDeviceMountPoint, kDefaultVideoDevicePoint, usb_pid_);
  return ((dev_paths.size() > 0) ? true : false);
}

int VideoDevice::OpenDevice() {
  if (usb_pid_.empty()) {
    LOG(ERROR) << "Failed to open device. USB PID is not set.";
    return kLogiErrorUsbPidNotFound;
  }
  std::vector<std::string> dev_paths = FindDevices(
      kDefaultVideoDeviceMountPoint, kDefaultVideoDevicePoint, usb_pid_);
  if (dev_paths.empty())
    return kLogiErrorUsbPidNotFound;

  // For video devices with 2 pins like BRIO, it mounts as 2 separate
  // /dev/video, check and returns multiple devices only if they are not on the
  // same usb bus.
  std::vector<std::string> bus_paths =
      FindUsbBus(kLogiVendorIdString, usb_pid_);
  if (dev_paths.size() > 1 && bus_paths.size() > 1) {
    LOG(ERROR) << "Failed to open device. Multiple device found.";
    return kLogiErrorMultipleDevicesFound;
  }

  int fd = -1;
  for (std::string dev_path : dev_paths) {
    fd = open(dev_path.c_str(), O_RDWR | O_NONBLOCK, 0);

    if (fd < 0) {
      LOG(INFO) << "Failed to open " << dev_path.c_str();
      continue;
    }

    if (IsCaptureDevice(fd, dev_path)) {
      LOG(INFO) << "Found capture device at " << dev_path.c_str();
      break;
    }

    close(fd);
  }

  if (fd == -1) {
    LOG(ERROR) << "Failed to open device. Open file failed.";
    return kLogiErrorOpenDeviceFailed;
  }
  is_open_ = true;
  file_descriptor_ = fd;
  return kLogiErrorNoError;
}

int VideoDevice::GetDeviceName(std::string* device_name) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  struct v4l2_capability video_cap;
  int error = ioctl(file_descriptor_, VIDIOC_QUERYCAP, &video_cap);
  if (error < 0)  // ioctl return -1 on failure.
    return kLogiErrorIOControlOperationFailed;
  // copy the whole video_cap array including null character.
  std::string str(
      video_cap.card,
      video_cap.card + sizeof(video_cap.card) / sizeof(video_cap.card[0]));
  // erase extra nulls.
  str.erase(std::find(str.begin(), str.end(), '\0'), str.end());
  *device_name = str;
  return kLogiErrorNoError;
}

int VideoDevice::IsLogicool(bool* is_logicool) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  // On video device, checks if device's name contains Logicool string.
  std::string name;
  int error = GetDeviceName(&name);
  if (error)
    return error;

  if (ContainString(name, "logicool", false)) {
    *is_logicool = true;
    return kLogiErrorNoError;
  }

  // Not Logicool, checks if Logitech. There might be Logi only.
  if (ContainString(name, "logitech", false)) {
    *is_logicool = false;
    return kLogiErrorNoError;
  }

  // Might be Logi (Logi Group), or something unknown.
  return kLogiErrorBrandCheckFailed;
}

int VideoDevice::ReadDeviceVersion(std::string* device_version) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<unsigned char> output_data;
  int unit_id = GetUnitID(kLogiGuidDeviceInfo);
  int error = GetXuControl(unit_id, kLogiCameraVersionSelector, &output_data);
  if (error)
    return error;
  if (output_data.size() < kLogiDeviceVersionDataSize)
    return kLogiErrorInvalidDeviceVersionDataSize;
  // little-endian data
  int major = static_cast<int>(output_data[1]);
  int minor = static_cast<int>(output_data[0]);
  int build = static_cast<int>((output_data[3] << 8) | output_data[2]);
  *device_version = GetDeviceStringVersion(major, minor, build);
  return kLogiErrorNoError;
}

int VideoDevice::GetImageVersion(std::vector<uint8_t> buffer,
                                 std::string* image_version) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  // Find the start of the version string.
  // The version information is located at an arbitrary position coded in the
  // following format: VERSIONMAGIC<x.y.z> As a sanity check the length of the
  // version "x.y.z" string may not exceed 32 characters.
  std::vector<uint8_t> version_magic = {'V', 'E', 'R', 'S', 'I', 'O', 'N',
                                        'M', 'A', 'G', 'I', 'C', '<'};
  auto iter = std::search(std::begin(buffer), std::end(buffer),
                          std::begin(version_magic), std::end(version_magic));
  if (iter == std::end(buffer))
    return kLogiErrorImageVersionNotFound;
  // Copy all characters until the closing '>' marker.
  std::string version;
  std::advance(iter, version_magic.size());
  while (iter != std::end(buffer) && *iter != '>') {
    version.push_back(*iter);
    if (version.size() > kLogiVideoImageVersionMaxSize)
      return kLogiErrorImageVersionExceededMaxSize;
    ++iter;
  }
  *image_version = version;
  return kLogiErrorNoError;
}

int VideoDevice::VerifyImage(std::vector<uint8_t> buffer) {
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  std::vector<uint8_t> signature = {'A', 'I', 'T', '8', '4', '2'};
  if (buffer.size() < signature.size())
    return kLogiErrorImageVerificationFailed;
  auto iter =
      std::search(std::begin(buffer), std::begin(buffer) + signature.size(),
                  std::begin(signature), std::end(signature));
  if (iter != std::begin(buffer))
    return kLogiErrorImageVerificationFailed;
  return kLogiErrorNoError;
}

int VideoDevice::AitInitiateUpdate() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  // Disclaimer: Any magic numbers come directly from the FlashGordon code.
  std::vector<unsigned char> data = {kLogiAitSetMmpCmdFwBurning,
                                     0,
                                     0,
                                     kLogiVideoAitInitiateSetMMPData,
                                     0,
                                     0,
                                     0,
                                     0};
  return AitInitiateUpdateWithData(data);
}

int VideoDevice::AitSendImage(std::vector<uint8_t> buffer) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;
  return AitSendImageWithOffset(buffer, kLogiVideoAitSendImageInitialOffset);
}

int VideoDevice::AitFinalizeUpdate() {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code.
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  std::vector<unsigned char> data = {kLogiAitSetMmpCmdFwBurning,
                                     kLogiVideoAitFinalizeSetMMPData,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0,
                                     0};
  return AitFinalizeUpdateWithData(data);
}

int VideoDevice::RebootDevice() {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  int unit_id = GetUnitID(kLogiGuidPeripheralControl);
  std::vector<unsigned char> data = {1};

  return SetXuControl(unit_id, kLogiUvcXuAitCustomReboot, data);
}

int VideoDevice::AitInitiateUpdateWithData(
    std::vector<unsigned char> mmp_data) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  // Disclaimer: Any magic numbers come directly from the FlashGordon code.
  int unit_id = GetUnitID(kLogiGuidAITCustom);
  int error = SetXuControl(unit_id, kLogiUvcXuAitCustomCsSetMmp, mmp_data);
  if (error) {
    LOG(ERROR) << "Failed to set command for AIT initialization. Error: "
               << error;
    return error;
  }
  std::vector<unsigned char> mmp_get_data;
  error =
      GetXuControl(unit_id, kLogiUvcXuAitCustomCsGetMmpResult, &mmp_get_data);
  if (error) {
    LOG(ERROR) << "Failed to get result for AIT initialization. Error: "
               << error;
    return error;
  }
  if (mmp_get_data.empty() || mmp_get_data[0] != 0) {
    LOG(ERROR) << "Failed to initialize AIT update. Invalid result data.";
    return kLogiErrorVideoDeviceAitInitiateGetDataFailed;
  }
  return kLogiErrorNoError;
}

int VideoDevice::AitSendImageWithOffset(std::vector<uint8_t> buffer,
                                        unsigned int offset) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (buffer.empty())
    return kLogiErrorImageBufferReadFailed;

  int unit_id = GetUnitID(kLogiGuidAITCustom);
  unsigned int remaining_size = buffer.size() - offset;
  while (remaining_size > 0) {
    unsigned int block_size =
        std::min<unsigned int>(remaining_size, kLogiDefaultImageBlockSize);

    // prepare the blockData
    std::vector<unsigned char> data(kLogiDefaultImageBlockSize);
    std::fill(std::begin(data), std::end(data), 0);
    std::copy(buffer.cbegin() + offset, buffer.cbegin() + offset + block_size,
              std::begin(data));

    int error = SetXuControl(unit_id, kLogiUvcXuAitCustomCsSetFwData, data);
    if (error) {
      LOG(ERROR) << "Failed to send block data at offset: " << offset
                 << ". Error: " << error;
      return error;
    }
    offset += block_size;
    remaining_size -= block_size;
  }
  return kLogiErrorNoError;
}

int VideoDevice::AitFinalizeUpdateWithData(
    std::vector<unsigned char> mmp_data) {
  // Disclaimer: any magic numbers and sleeps originate in the FlashGordon code.
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  int unit_id = GetUnitID(kLogiGuidAITCustom);
  int error = SetXuControl(unit_id, kLogiUvcXuAitCustomCsSetMmp, mmp_data);
  if (error) {
    LOG(ERROR) << "Failed to set command for AIT finalization. Error: "
               << error;
    return error;
  }

  // Get MMP data. Poll until the device returns either success or failure.
  auto duration_ms = 0;
  const auto doSleep = [&](int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    duration_ms += ms;
  };

  // This is the way AIT chipset is finalized (based on AIT finalizing method).
  // Try to poll for burning fw result or return failure if it hits max polling
  // duration.
  for (int pass = 0;; pass++) {
    std::vector<unsigned char> data;
    int attempts = 3;

    doSleep(kLogiDefaultAitSleepIntervalMs);
    // Sometimes on a very fast cpu, AIT chip returns error when querying
    // extension control unit on first time. If it fails, attempt to retry for 3
    // times before returning error.
    do {
      error = GetXuControl(unit_id, kLogiUvcXuAitCustomCsGetMmpResult, &data);
      if (error) {
        LOG(ERROR) << "Failed to get result for AIT finalization. Error: "
                   << error << ". Attempt to retry: " << attempts;
        doSleep(kLogiDefaultAitSleepIntervalMs);
      }
      attempts--;
    } while (attempts > 0 && error);

    if (error || data.empty()) {
      LOG(ERROR) << "Failed to finalize AIT update. Invalid result data.";
      return kLogiErrorImageBurningFinalizeFailed;
    }

    // process the response.
    if (data.size() > 0) {
      if (data[0] == kLogiDefaultAitSuccessValue) {
        // If there is not positive response from the device that the flash was
        // written, estimate the time needed plus a time buffer to determine
        // when the flash write process has completed.  In this case, time
        // buffer is about 8s for the flash write operation to complete. This is
        // done using both device spec and stress testings.
        if (pass == 0)
          doSleep(kLogiDefaultAitFirstPassSleepIntervalMs);
        break;
      } else if (data[0] == kLogiDefaultAitFailureValue) {
        LOG(ERROR) << "Failed to finalize AIT update. Failure result data.";
        return kLogiErrorImageBurningFinalizeFailed;
      }
    }
    if (duration_ms > kLogiDefaultAitFinalizeMaxPollingDurationMs) {
      // In case device never returns 0x82 or 0x00, bail out.
      LOG(ERROR) << "Failed to finalize AIT update. Checking result timeout.";
      return kLogiErrorImageBurningFinalizeFailed;
    }
  }
  return kLogiErrorNoError;
}

int VideoDevice::PerformUpdate(std::vector<uint8_t> buffer,
                               std::vector<uint8_t> secure_header,
                               bool* did_update) {
  *did_update = false;
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  std::string name;
  if (device_type_ == kLogiDeviceVideo) {
    name = "video";
  } else if (device_type_ == kLogiDeviceMcu2) {
    name = "mcu2";
  } else if (device_type_ == kLogiDeviceHdmi) {
    name = "HDMI";
  }

  LOG(INFO) << "Checking " << name << " firmware...";
  bool should_update;
  int error = CheckForUpdate(buffer, &should_update);
  if (error || !should_update)
    return error;

  LOG(INFO) << name << " firmware is not up to date. Updating firmware...";
  std::string device_name;
  if (GetDeviceName(&device_name) != kLogiErrorNoError
      || device_name.length() == 0) {
    // Default name used instead if no name was found.
    device_name = "Logitech Camera";
  }
  device_name.append(" ");
  device_name.append(name);
  std::unique_ptr<IDfuNotification> dfu_notification =
      IDfuNotification::For(device_name);
  dfu_notification->NotifyStartUpdate();

  error = AitInitiateUpdate();
  if (error) {
    LOG(ERROR) << "Failed to initiate update. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  error = AitSendImage(buffer);
  if (error) {
    LOG(ERROR) << "Failed to send image. Error: " << error;
    dfu_notification->NotifyEndUpdate(false);
    return error;
  }
  error = AitFinalizeUpdate();
  if (error) {
    LOG(ERROR) << "Failed to finalize image. Error: " << error;
  } else {
    LOG(INFO) << "Successfully updated " << name << " firmware.";
    *did_update = true;
  }
  dfu_notification->NotifyEndUpdate(!error);
  return error;
}

int VideoDevice::SetXuControl(unsigned char unit_id,
                              unsigned char control_selector,
                              std::vector<unsigned char> data) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (unit_id < 0)
    return kLogiErrorXuUnitIdInvalid;

  struct uvc_xu_control_query control_query;
  control_query.unit = static_cast<uint8_t>(unit_id);
  control_query.selector = static_cast<uint8_t>(control_selector);
  control_query.query = UVC_SET_CUR;
  control_query.size = data.size();
  control_query.data = data.data();
  // A few ioctl requests use return value as an output parameter
  // and return a nonnegative value on success, so we should check
  // for real error before returning.
  int error = ioctl(file_descriptor_, UVCIOC_CTRL_QUERY, &control_query);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  return kLogiErrorNoError;
}

int VideoDevice::GetXuControl(unsigned char unit_id,
                              unsigned char control_selector,
                              std::vector<unsigned char>* data) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;
  if (unit_id < 0)
    return kLogiErrorXuUnitIdInvalid;
  int data_len;
  int error = QueryDataSize(unit_id, control_selector, &data_len);
  if (error)
    return error;

  uint8_t query_data[data_len];
  memset(query_data, 0, sizeof(query_data));
  struct uvc_xu_control_query control_query;
  control_query.unit = unit_id;
  control_query.selector = control_selector;
  control_query.query = UVC_GET_CUR;
  control_query.size = data_len;
  control_query.data = query_data;
  error = ioctl(file_descriptor_, UVCIOC_CTRL_QUERY, &control_query);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;
  for (int i = 0; i < data_len; i++) {
    data->push_back(query_data[i]);
  }
  return kLogiErrorNoError;
}

int VideoDevice::QueryDataSize(unsigned char unit_id,
                               unsigned char control_selector,
                               int* data_size) {
  if (!is_open_)
    return kLogiErrorDeviceNotOpen;

  uint8_t size_data[kDefaultUvcGetLenQueryControlSize];
  struct uvc_xu_control_query size_query;
  size_query.unit = unit_id;
  size_query.selector = control_selector;
  size_query.query = UVC_GET_LEN;
  size_query.size = kDefaultUvcGetLenQueryControlSize;
  size_query.data = size_data;
  int error = ioctl(file_descriptor_, UVCIOC_CTRL_QUERY, &size_query);
  if (error < 0)
    return kLogiErrorIOControlOperationFailed;

  // convert the data byte to int
  int size = static_cast<int>((size_data[1] << 8) | (size_data[0]));
  *data_size = size;
  return kLogiErrorNoError;
}
