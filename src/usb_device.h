// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_USB_DEVICE_H_
#define SRC_USB_DEVICE_H_

#include <stdio.h>
#include <string>
#include <vector>

enum {
  kLogiErrorNoError = 0,
  // device errors
  kLogiErrorUsbPidNotFound,
  kLogiErrorMultipleDevicesFound,
  kLogiErrorDeviceNotOpen,
  kLogiErrorOpenDeviceFailed,
  kLogiErrorDeviceRebootFailed,
  kLogiErrorInvalidDeviceVersionDataSize,
  // binary image errors
  kLogiErrorImageBufferReadFailed,
  kLogiErrorImageVersionNotFound,
  kLogiErrorImageVersionExceededMaxSize,
  kLogiErrorImageVerificationFailed,
  kLogiErrorImageBurningFinalizeFailed,
  kLogiErrorParseS19BinaryFailed,
  kLogiErrorReadS19ImageByteFailed,
  kLogiErrorSendImageFailed,
  // I/O operation errors
  kLogiErrorXuUnitIdInvalid,
  kLogiErrorVideoDeviceAitInitiateGetDataFailed,
  kLogiErrorIOControlOperationFailed,
  kLogiErrorUnknown
};

enum {
  kLogiDeviceGeneric = 0,
  kLogiDeviceVideo,
  kLogiDeviceEeprom,
  kLogiDeviceMcu2
};

/**
 * Generic Logitech usb device class containing common usb device information
 * and firmware updating processes.
 */
class USBDevice {
 public:
  std::string usbPid;  // USB product id string.
  int deviceType;  // Device type: video, eeprom, mcu2 or other type. This is
                   // convenient to get the versions/name from composite device.

 protected:
  int fileDescriptor;
  bool isOpen;

 protected:
  /**
   * @brief Constructor with product id.
   * @param pid Product id string.
   * @param type Device type.
   */
  USBDevice(std::string pid, int type);

 public:
  virtual ~USBDevice();

  /**
   * @brief Gets the device version. Implement readDeviceVersion to read the
   * device version.
   * @param deviceVersion Output device version string.
   * @return kLogiErrorNoError if failed, error code otherwise.
   */
  virtual int getDeviceVersion(std::string* deviceVersion);

  /**
   * @brief Finds the device with usbPid & usbVid. To be implemented by
   * subclass.
   * @return list of device paths.
   */
  virtual std::vector<std::string> findDevices() = 0;

  /**
   * @brief Opens the device. To be implemented by subclass.
   * @return kLogiErrorNoError if open ok, error code otherwise.
   */
  virtual int openDevice() = 0;

  /**
   * @brief Closes the device. To be implemented by subclass.
   */
  virtual void closeDevice() = 0;

  /**
   * @brief Gets the device name. To be implemented by subclass
   * @param deviceName Output device name.
   * @return kLogiErrorNoError if gets ok, error code otherwise.
   */
  virtual int getDeviceName(std::string* deviceName) = 0;

  /**
   * @brief Gets the binary image version. To be implemented by subclass
   * @param buffer The image buffer to read from
   * @param imageVersion Output device version string.
   * @return kLogiErrorNoError if gets ok, error code otherwise.
   */
  virtual int getImageVersion(std::vector<char> buffer,
                              std::string* imageVersion) = 0;

  /**
   * @brief Verifies the image to make sure it's secure. To be implemented by
   * subclass.
   * @param buffer The image buffer to verify.
   * @return kLogiErrorNoError if verified ok, error code otherwise.
   */
  virtual int verifyImage(std::vector<char> buffer) = 0;

  /**
   * @brief Reboots the device. To be implemented by subclass
   * @return kLogiErrorNoError if rebooted ok, error code otherwise.
   */
  virtual int rebootDevice() = 0;

  /**
   * @brief Performs update. To be implemented by subclass.
   * @param buffer Firmware image buffer.
   * @return  kLogiErrorNoError if update ok, error code otherwise.
   */
  virtual int performUpdate(std::vector<char> buffer) = 0;

 protected:
  /**
   * @brief Reads the device version. To be implemented by subclass.
   * @param deviceVersion Output device version string.
   * @return kLogiErrorNoError if gets ok, error code otherwise.
   */
  virtual int readDeviceVersion(std::string* deviceVersion) = 0;
};
#endif /* SRC_USB_DEVICE_H_ */
