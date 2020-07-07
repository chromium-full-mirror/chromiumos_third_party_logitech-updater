// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_USB_DEVICE_H_
#define SRC_USB_DEVICE_H_

#include <stdio.h>
#include <string>
#include <vector>

enum {
  kLogiErrorNoError = 0,
  // Device errors.
  kLogiErrorUsbPidNotFound,
  kLogiErrorUsbDfuPidNotFound,
  kLogiErrorMultipleDevicesFound,
  kLogiErrorMultipleDevicesDfuModeFound,
  kLogiErrorDeviceNotOpen,     // Device present but not opened.
  kLogiErrorDeviceNotPresent,  // Device not present.
  kLogiErrorOpenDeviceFailed,  // Device present but failed to open.
  kLogiErrorDeviceRebootFailed,
  kLogiErrorInvalidDeviceVersionDataSize,
  kLogiErrorInvalidDeviceVersionString,
  kLogiErrorInvalidResultData,
  kLogiErrorDeviceNameNotAvailable,
  kLogiErrorGetDfuApiVersionFailed,
  kLogiErrorSetDeviceToDfuModeFailed,
  kLogiErrorWaitForDfuDeviceReopenFailed,
  kLogiErrorWaitForHidDeviceReopenFailed,
  kLogiErrorBrandCheckFailed,
  kLogiErrorBrandMismatched,
  kLogiErrorClaimInterfaceFailed,
  // Binary image errors.
  kLogiErrorImageBufferReadFailed,
  kLogiErrorImageVersionNotFound,
  kLogiErrorImageVersionExceededMaxSize,
  kLogiErrorImageVersionSizeTooShort,
  kLogiErrorImageVerificationFailed,
  kLogiErrorImageBurningFinalizeFailed,
  kLogiErrorParseS19BinaryFailed,
  kLogiErrorReadS19ImageByteFailed,
  kLogiErrorSendImageFailed,
  kLogiErrorVerifyUpdatedImageFailed,
  kLogiErrorVerifyImageVersionFailed,
  // I/O operation errors.
  kLogiErrorXuUnitIdInvalid,
  kLogiErrorVideoDeviceAitInitiateGetDataFailed,
  kLogiErrorIOControlOperationFailed,
  kLogiErrorIOControlInvalidDataSize,
  // Sig validation errors.
  kLogiErrorSignatureValidationFailed,
  kLogiErrorInvalidBleNameSize,
  kLogiErrorUnknown,
  kLogiErrorTimeout,
  kLogiErrorLockFileOpenFailed,
  kLogiErrorLockFileLockFailed,
  kLogiErrorProcessAlreadyRunning
};

enum {
  kLogiDeviceGeneric = 0,
  kLogiDeviceVideo,
  kLogiDeviceEeprom,
  kLogiDeviceMcu2,
  kLogiDeviceAudio,
  kLogiDeviceCodec,
  kLogiDeviceBle,  // Bluetooth Low Energy device.
  kLogiDeviceHdmi,
  kLogiDeviceVideoBle,
  kLogiDeviceTableHub,
  kLogiDeviceMicPod,
  kLogiDeviceSplitter,
};

/**
 * Generic Logitech usb device class containing common usb device information
 * and firmware updating processes.
 */
class USBDevice {
 public:
  // USB product id string.
  std::string usb_pid_;
  // Device type: video, eeprom, mcu2 or other type. This is convenient to get
  // the versions/name from composite device.
  int device_type_;
  // Force update without checking version.
  bool force_update_;
  // If true, perform update with secure boot image.
  bool secure_boot_;
  // If set true, this device supports BLE feature.
  bool support_ble_;
  // For firmware without embedded version string, use separate version file.
  std::string version_file_;

 protected:
  int file_descriptor_;
  // Flag to check if the device is being opened.
  bool is_open_;

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
   * device.
   * @param device_version Output device version string.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int GetDeviceVersion(std::string* device_version);

  /**
   * @brief Closes the device.
   */
  virtual void CloseDevice();

  /**
   * @brief Opens the device.
   * @return kLogiErrorNoError if open ok, error code otherwise.
   */
  virtual int OpenDevice() = 0;

  /**
   * @brief Checks if the device is present.
   */
  virtual bool IsPresent() = 0;

  /**
   * @brief Gets the device name.
   * @param device_name Output device name.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int GetDeviceName(std::string* device_name) = 0;

  /**
   * @brief Gets the binary image version.
   * @param buffer The image buffer to read from.
   * @param image_version Output device version string.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version) = 0;

  /**
   * @brief Verifies the image to make sure it's secure.
   * @param buffer The image buffer to verify.
   * @return kLogiErrorNoError if verified ok, error code otherwise.
   */
  virtual int VerifyImage(std::vector<uint8_t> buffer) = 0;

  /**
   * @brief Reboots the device.
   * @return kLogiErrorNoError if rebooted ok, error code otherwise.
   */
  virtual int RebootDevice() = 0;

  /**
   * @brief Check if the device firmware is Logicool.
   * @param is_logicool True if device firmware is Logicool.
   * @return kLogiErrorNoError if checked ok, error code otherwise.
   */
  virtual int IsLogicool(bool* is_logicool) = 0;

  /**
   * @brief Performs firmware update.
   * @param buffer Firmware image buffer.
   * @param secure_header Secure header buffer if secure_boot is enabled.
   * @parma did_update True if update was performed successfully.
   * @return kLogiErrorNoError if updated ok, error code otherwise.
   */
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update) = 0;

  /**
   * @brief Checks the device if it should perform the update. This compares
   * the version of the binary image and device. If device version is lower than
   * image version, should_update is true.
   * @param buffer Image buffer to check for update.
   * @param should_update True if device should perform update.
   * @return kLogiErrorNoError if checked ok, error code otherwise
   */
  virtual int CheckForUpdate(std::vector<uint8_t> buffer, bool* should_update);

  /**
   * @brief Set version text file.
   * @param version_file The path to version file to be set.
   */
  void SetVersionFile(const std::string& version_file);

 protected:
  /**
   * @brief Reads the device version.
   * @param device_version Output device version string.
   * @return kLogiErrorNoError if read ok, error code otherwise
   */
  virtual int ReadDeviceVersion(std::string* device_version) = 0;
};
#endif /* SRC_USB_DEVICE_H_ */
