// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_AUDIO_DEVICE_H_
#define SRC_AUDIO_DEVICE_H_

#include <stdio.h>
#include "usb_device.h"

struct DeviceInfo {
  uint8_t id;         // Tablehub:0x10, Micpods:0x21~0x27, Splitters:0x31~0x37
  uint8_t father_id;  // The previous device ID
  uint8_t slot_idx;   // The connector index of father device, 0:father device
                      // is Micpod, 0~2:father device is splitter
  uint8_t update_status;  // MSB 1 indicating updating, rest bits for percentage
  uint8_t ver_major;      // Major number of version
  uint8_t ver_minor;      // Minor number of version
  uint16_t ver_build;     // Build number of version
  uint32_t fpga_version;
};

struct SystemTopology {
  DeviceInfo tablehub;              // info pertaining to tablehub device
  std::vector<DeviceInfo> micpods;  // info pertaining to all micpods in system
  std::vector<DeviceInfo> splitters;  // splitters info in system
  std::string audio_version;          // audio firmware version
  std::string audio_ble_version;      // audio ble firmware version
  std::string video_version;          // video firmware version
  std::string video_eeprom_version;   // eeprom firmware version
  std::string video_ble_version;      // video ble firmware version
  uint8_t left_speaker_connected;     // bit to show left speaker connected
  uint8_t right_speaker_connected;    // bit to show right speaker connected
};

const char kDefaultAudioDeviceMountPoint[] = "/sys/class/hidraw";
const char kDefaultAudioDevicePoint[] = "/device/../..";
// HID report number to set device into info querying mode.
constexpr int kLogiReportNumberSetInfo = 0x1B;
// HID report number to get info.
constexpr int kLogiReportNumberGetInfo = 0x19;

// ID values for DeviceInfo struct.
constexpr uint8_t kLogiTableHubId = 0x10;
constexpr uint8_t kLogiMicpodIdStart = 0x21;
constexpr uint8_t kLogiMicpodIdEnd = 0x27;
constexpr uint8_t kLogiSplitterIdStart = 0x31;
constexpr uint8_t kLogiSplitterIdEnd = 0x37;
/**
 * Logitech audio device class to handle audio firmware update. These are the
 * steps performed by this updater:
 *  1. Open the device in HID mode using product id. If multiple devices are
 *     found, return error.
 *      1.1 If failed, falls back to open in Device Firmware Update (DFU) mode.
 *      1.2 If failed to open both states, returns error opening device.
 *  2. Put device into DFU mode (simple or safe). Device is reset afterward.
 *      2.1 After device resets, re-open device in DFU mode.
 *      2.2 Checks the DFU API version in DFU mode.
 *  3. Update the device. The update process involves these steps:
 *      3.1 Erase the audio flash memory.
 *      3.2 Send the file size to the flash memory.
 *      3.3 Put the devices into firmware download mode.
 *      3.4 Send image data to the flash memory.
 *      3.5 Verify the new image.
 *      3.6 Reset DFU mode to Audio/HID mode.
 *
 * In Udev env, updating device will be performed under 2 steps. Udev will put
 * the device into DFU mode in step 1 and perform actual firmware update in
 * step 2.
 */
class AudioDevice : public USBDevice {
 private:
  // For audio devices, when update firmware, the device is set to Device
  // Firmware Update (DFU) mode. In this mode, the product id (PID) is changed.
  // Hold on the dfu pid and re-open the device.
  std::string dfu_pid_;
  bool is_dfu_mode_;

 protected:
  /**
   * @brief Constructor with product id and device type.
   * @param pid Product id string.
   * @param type Device type.
   */
  AudioDevice(std::string pid, int type);

 public:
  /**
   * @brief Constructor with product id and DFU product id.
   * @param pid Product id string.
   * @param dfu_pid Device Firmware Update product id.
   */
  AudioDevice(std::string pid, std::string dfu_pid);

  virtual ~AudioDevice();

  virtual bool IsPresent();
  virtual int OpenDevice();
  virtual int RebootDevice();
  virtual int ReadDeviceVersion(std::string* device_version);
  virtual int IsLogicool(bool* is_logicool);
  virtual int VerifyImage(std::vector<uint8_t> buffer);
  virtual int GetImageVersion(std::vector<uint8_t> buffer,
                              std::string* image_version);
  virtual int PerformUpdate(std::vector<uint8_t> buffer,
                            std::vector<uint8_t> secure_header,
                            bool* did_update);
  /**
   * @brief Device name is stored on video chipset. Attempts to call this method
   * on audio chipset will return kLogiErrorDeviceNameNotAvailable. You should
   * open and read device name from video instead.
   */
  virtual int GetDeviceName(std::string* device_name);

 public:
  /**
   * @brief Prepares the audio for actual firmware update. This puts the device
   * into DFU mode.
   * @param buffer The image buffer to send to the device.
   * @param should_update True if audio device should be updated.
   * @return kLogiErrorNoError if prepared ok, error code otherwise.
   */
  int PrepareForUpdate(std::vector<uint8_t> buffer, bool* should_update);

  /**
   * @brief Reads the topology of the system. Grabs information such as which
   * device components connected to the system, firmware version(s) for each
   * device component, and update progress.
   * @param topology The system's topology.
   * @return kLogiErrorNoError if read topology correctly, error code otherwise.
   */
  int ReadSystemTopology(SystemTopology* topology);

 protected:
  /**
   * @brief Check status after sending data to the device. If IO Control
   * Operation failed or device is busy downloading, wait and retry.
   * @return kLogiErrorNoError if device is ready to accept the next data block,
   * error code otherwise.
   */
  virtual int CheckDeviceStatusAfterSendingData();

  /**
   * @brief Opens the device in HID/audio mode.
   * @return kLogiErrorNoError if opened ok, error code otherwise.
   */
  int OpenDeviceInHidMode();

  /**
   * @brief Sets the device into download mode. In this mode, the device is
   * ready to accept binary data sent to it. When it receives the data, it will
   * write to the flash memory.
   * @param report_number Download mode HID command report number to set.
   * @param report_data Download mode HID command report data to set.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int SetDownloadMode(uint8_t report_number, uint8_t report_data);

  /**
   * @brief Sends binary file size to the device before attempting to write the
   * the device during updating process.
   * @param report_number HID report number to send file size.
   * @param report_data HID report data to send file size.
   * @param file_size Binary image file size in bytes to send to the device.
   * @return kLogiErrorNoError if sent ok, error code otherwise.
   */
  int SendImageFileSize(uint8_t report_number,
                        uint8_t report_data,
                        uint32_t file_size);

  /**
   * @brief Sends image to the device. The device needs to be in DFU download
   * mode first before it can accept the image binary data.
   * @param buffer The image buffer to send to the device.
   * @param report_number HID report number to set data to the device.
   * @return kLogiErrorNoError if sent successfully, error code otherwise.
   */
  int SendImage(uint8_t report_number, std::vector<uint8_t> buffer);

  /**
   * @brief Waits and re-opens the device after it was put into Audio/HID mode.
   * This method attempts to open the device multiple times until max timeout.
   * @return kLogiErrorNoError if succeeded, error code otherwise
   */
  int WaitForHidDevice();

  /**
   * @brief Sends header image to the device before sending binary data image if
   * the secure_boot_ is set.
   * @param report_number HID report number to send header image to device.
   * @param buffer header image buffer to send to device.
   * @return kLogiErrorNoError if sent successfully, error code otherwise.
   */
  int SendHeaderImage(uint8_t report_number, std::vector<uint8_t> buffer);

  /**
   * @brief Send command with report number when device in HID mode.
   * @param report_number Hex number to get report from.
   * @param data Output report data after sending command.
   * @return kLogiErrorNoError if get report ok, error code otherwise.
   */
  int SendCommandInHidMode(uint8_t report_number, std::vector<uint8_t>* data);

 private:
  /**
   * @brief Opens the device in Device Firmware Update mode.
   * @return kLogiErrorNoError if opened ok, error code otherwise.
   */
  int OpenDeviceInDfuMode();

  /**
   * @brief Opens the device with product id. Because audio device has 2 pids:
   * audio/HID pid and DFU PID, uses this method to open the correct mode.
   * @param usb_pid The PID of audio device.
   * @return kLogiErrorNoError if opened ok, error code otherwise.
   */
  int OpenDeviceWithPid(std::string usb_pid);

  /**
   * @brief Gets Device Firmware Update (DFU) API version from the device in HID
   * mode. The returned value is used to determine which method of putting
   * device (safe or simple) into DFU should be used.
   * @param dfu_version Device firmware update version output.
   * @return If the device does not support DFU API version query, this still
   * returns kLogiErrorNoError and the output version will be 0.0. An error code
   * is returned only if IO Control Operation fails.
   */
  int GetDfuApiVersionInHidMode(std::string* dfu_version);

  /**
   * @brief Gets DFU API version from the device in DFU mode.
   * @param dfu_version DFU version output.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int GetDfuApiVersionInDfuMode(std::string* dfu_version);

  /**
   * @brief Sets the device to Device Firmware Update to safe mode or simple
   * mode. Devices with DFU API >= 1.1 support DFU safe mode while those < 1.1
   * use simple mode. When setting device to DFU mode, it is reset and rejected
   * from the system. This method waits for the device to come back and re-open
   * it.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int SetDfuMode();

  /**
   * @brief Sets the device to Device Firmware Update safe mode. This mode is
   * available to devices with DFU API version >= 1.1.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int SetDfuModeSafe();

  /**
   * @brief Sets the device to Device Firmware Update simple mode. This mode is
   * available to devices with DFU API version < 1.1.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int SetDfuModeSimple();

  /**
   * @brief Waits and re-opens the device after it was put into Device Firmware
   * update mode. This method attempts to open the device multiple times until
   * timeout.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int WaitForDfuDevice();

  /**
   * @brief Updades the firmware when the device is in Device Firmware Update
   * mode. Based on device specs and cables, DFU update process might fail in
   * the first try and be retried multiple times. This method triggers the
   * following steps:
   * 1. Erase the flash memory using EraseFlash().
   * 2. Send image file size using SendImageFileSize().
   * 3. Put device into firmware download mode using SetDownloadMode().
   * 4. Send secure header image if enable using SendHeaderImage().
   * 4. Send binary image using SendImage().
   * @param buffer Image buffer to send to the device.
   * @param secure_header Secure image buffer to send to the device.
   * @return kLogiErrorNoError if sent succesfully, error code otherwise.
   */
  int UpdateFirmwareInDfuMode(std::vector<uint8_t> buffer,
                              std::vector<uint8_t> secure_header);

  /**
   * @brief Erases the flash when the device is in DFU mode. Erasing flash
   * memory might fail if device is busy. This method will attempt to try
   * multiple times.
   * @return kLogiErrorNoError if succeeded, error code otherwise.
   */
  int EraseFlash();

  /**
   * @brief Verifies the Device Firmware Update process after sending image to
   * the device. Successfully verified the process does not mean the update has
   * been completed. This computes the checksum and sends it to the device. If
   * checksum matches, the Audio/HID mode internal flag is set for the next
   * reset. Device firmware update completes only after the device is reset and
   * re-enumerated in HID/Audio mode.
   * @param buffer The image buffer that has been sent to the device.
   * @return kLogiErrorNoError if verified ok, error code otherwise.
   */
  int VerifyDfuUpdateImage(std::vector<uint8_t> buffer);

  /**
   * @brief Resets the device when it's in Device Firmware Update mode to
   * Audio/HID mode. This method will close the device and re-open it in HID
   * mode.
   * @return kLogiErrorNoError if resets ok, error code otherwise.
   */
  int ResetToHidMode();

  /**
   * @brief Reads BLE name from the device.
   * @param ble_name BLE name of the device.
   * @return kLogiErrorNoError if reads ok, error code otherwise.
   */
  int ReadBleDeviceName(std::string* ble_name);

  /**
   * @brief Writes BLE name to the device.
   * @param ble_name BLE name to write to the device.
   * @return kLogiErrorNoError if writes ok, error code otherwise.
   */
  int WriteBleDeviceName(std::string ble_name);

  /**
   * @brief Try to set the device into DFU mode using DFU safe mode method.
   * @return kLogiErrorNoError if succeeded setting device into DFU mode or
   * error code otherwise.
   */
  int TrySetDfuModeSafe();
};
#endif /* SRC_AUDIO_DEVICE_H_ */
