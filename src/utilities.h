// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UTILITIES_H_
#define SRC_UTILITIES_H_

#include <stdio.h>
#include <string>
#include <vector>
#include <openssl/rsa.h>

const char kLogiGuidDeviceInfo[] = "69678ee4-410f-40db-a850-7420d7d8240e";
const char kLogiGuidAITCustom[] = "23e49ed0-1178-4f31-ae52-d2fb8a8d3b48";
const char kLogiGuidTestDebug[] = "1f5d4ca9-de11-4487-840d-50933c8ec8d1";
const char kLogiGuidPeripheralControl[] =
    "ffe52d21-8030-4e2c-82d9-f587d00540bd";
const char kLogiVendorIdString[] = "0x046D";
constexpr int kLogiPollingSleep = 20000;
constexpr int kLogiPollingMaxTimeout = 90;

/**
 * @brief Gets contents of the directory.
 * @param directory The directory path to get contents.
 * @param contents Ouput vector of the content paths.
 * @return true if succeeded, false otherwise.
 */
bool GetDirectoryContents(std::string directory,
                          std::vector<std::string>* contents);

/**
 * @brief Checks if VendorID belongs to Logitech.
 * @param vendor_id The vendorID string to check against.
 * @return true if it's Logitech, false otherwise.
 */
bool IsLogitechVendorID(std::string vendor_id);

/**
 * @brief Reads file contents.
 * @param filepath The file path to read.
 * @param output Output string from reading.
 * @return true if read ok, false otherwise.
 */
bool ReadFileContent(std::string filepath, std::string* output);

/**
 * @brief Converts hex string to int.
 * @param hex_string The input hex string.
 * @param output_value The output int value.
 * @return false if convert failed, true otherwise.
 */
bool ConvertHexStringToInt(std::string hex_string, int* output_value);

/**
 * @brief Converts hex char to unsigned int.
 * @param c The hex char to be converted.
 * @param output_value Output unsigned int value.
 * @return false if convert failed, true otherwise.
 */
bool ConvertHexCharToUnsignedInt(const char c, uint8_t* output_value);

/**
 * @brief Converts int representation of device's version
 * to string representation.
 * @param num The int to be converted.
 * @param version_str Output string value.
 * @return false if convert failed, true otherwise.
 */
bool GetDeviceVersionFromInt(int num, std::string* version_str);

/**
 * @brief Gets the device string version.
 * @param major Major version number.
 * @param minor Minor version number.
 * @param build Build version number.
 * @return std::string Device version number.
 */
std::string GetDeviceStringVersion(int major, int minor, int build);

/**
 * @brief Gets the device string version.
 * @param major Major version number.
 * @param minor Minor version number.
 * @return std::string Device version number.
 */
std::string GetDeviceStringVersion(int major, int minor);

/**
 * @brief Gets device version numbers from string.
 * @param version Input version string.
 * @param major Major version number output.
 * @param minor Minor version number output.
 * @param build Build version number output.
 * @return true if parsed ok.
 */
bool GetDeviceVersionNumber(std::string version,
                            int* major,
                            int* minor,
                            int* build);

/**
 * @brief Reads binary file content to buffer.
 * @param filepath path to the binary file.
 * @return buffer from reading. Buffer is empty if read failed.
 */
std::vector<uint8_t> ReadBinaryFileContent(std::string filepath);

/**
 * @brief Compares 2 version strings in format major.minor.build or major.minor.
 * @param version1 The version string 1.
 * @param version2 The version string 2.
 * @return 0 equal, -1 if version1 < version2 or 1 if version1 > version2.
 */
int CompareVersions(std::string version1, std::string version2);

/**
 * @brief Splits the string with delimiters into components.
 * @param string String to be split.
 * @param delimiters The delimiter characters.
 * @return vector containing components from string splitting.
 */
std::vector<std::string> SplitString(std::string string,
                                     std::string delimiters);

/**
 * @brief Gets the unit id from guid.
 * @param guid GUID string.
 * @return unit id or -1 if it's not found
 */
int GetUnitID(std::string guid);

/**
 * @brief This method tries to iterate through usb device mount point to look
 * for Logitech device with the usb product id.
 * @param mount_point The usb mount point. For video devices, they are usually
 * mounted at /sys/class/video4linux. For audio devices, it's /sys/class/hidraw.
 * @param device_point The directory it should look for product id and vendor id
 * file. For video devices, device_point is usually at
 * sys/class/video4linux/video{x}/device/../. For audio devices, it's
 * /sys/class/hidraw/hidraw{x}/device/../../ where x is a number created by
 * system when device was mounted. Therefore, pass /dev/.. or
 * device/../.. for this parameter
 * @return The paths to the device. For video devices, the path is usually
 * /dev/video{x}. For audio devices, the path is /dev/hidraw{x}. If there are
 * multiple paths returned, multiple devices are found.
 */
std::vector<std::string> FindDevices(std::string mount_point,
                                     std::string device_point,
                                     std::string usb_pid);

/**
 * @brief Find the usb bus devices from /sys/bus/usb/devices with vendor id and
 * product id.
 * @param vid Vendor id to find the usb device.
 * @param pid Product id to find the usb device.
 * @return path to the devices at /dev/bus/usb/.
 */
std::vector<std::string> FindUsbBus(std::string vid, std::string pid);

/**
 * @brief Check if string a contains string b.
 * @param str String to search.
 * @param substring String to search for in string a.
 * @param case_sensitive True if check with case sensitive.
 * @return true if string a contains string b.
 */
bool ContainString(std::string str, std::string substring, bool case_sensitive);

/**
 * @brief Locks this updater to have only 1 instance running.
 * This is blocking call if another instance already running
 * @return true if there is no other logitech-updater processed running and was
 * able to lock the lock file, false otherwise.
 */
bool LockUpdater();

/**
 * @brief Check if updater instance is already running.
 * @return kLogiErrorNoError if there is no other logitech-updater processed
 * running and was able to acquire the lock file Return
 * kLogiErrorLockFileOpenFailed/kLogiErrorLockFileLockFailed otherwise.
 */
int CheckUpdater();

/**
 * @brief Generates RSA crypto.
 * @param test_mode If true, use test_mode key data, live key data otherwise.
 * @param out_rsa Output RSA crypto.
 * @return true if generated rsa ok, false otherwise.
 */
bool GenerateRSA(bool test_mode, RSA** out_rsa);

/**
 * @brief Converts hex string to uint8_t data.
 * @param hex_string Hex string to convert.
 * @param data Converted data output.
 * @return true if converted ok, false otherwise.
 */
bool ConvertHexStringToData(std::string hex_string, std::vector<uint8_t>* data);

/**
 * @brief Validates signature with RSA crypto.
 * @param test_mode If true, use test_mode key data.
 * @param payload Payload bytes to validate.
 * @param sig Signature bytes to validate the payload.
 * @return true if validated ok, false otherwise.
 */
bool ValidateSignature(bool test_mode,
                       std::vector<uint8_t> payload,
                       std::vector<uint8_t> sig);

/**
 * @brief Validates signature with RSA crypto. Reverse the sig if fail and
 * re-validate it.
 * @param test_mode If true, use test_mode key data.
 * @param payload Payload file path.
 * @param sig Signature file path.
 * @return true if validated ok, false otherwise.
 */
bool ValidateSignature(bool test_mode, std::string payload, std::string sig);

/**
 * @brief Validates signature with RSA crypto. Add ".sig" to payload and call
 * ValidateSignature(bool test_mode, std::string payload, std::string sig) to
 * perform validation.
 * @param test_mode If true, use test_mode key data.
 * @param payload Payload file path.
 * @return true if validated ok, false otherwise.
 */
bool ValidateSignature(bool test_mode, std::string payload);

/**
 * @brief Reverses the bit order in 1 byte only.
 * @param value Value to reverse. For example, if you pass 0101 0010 to this
 * method, it will be reversed to 0100 1010.
 */
uint8_t ReverseBits(uint8_t value);

#endif /* SRC_UTILITIES_H_ */
