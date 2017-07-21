// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UTILITIES_H_
#define SRC_UTILITIES_H_

#include <stdio.h>
#include <string>
#include <vector>

const char kLogiGuidDeviceInfo[] = "69678ee4-410f-40db-a850-7420d7d8240e";
const char kLogiGuidAITCustom[] = "23e49ed0-1178-4f31-ae52-d2fb8a8d3b48";
const char kLogiGuidTestDebug[] = "1f5d4ca9-de11-4487-840d-50933c8ec8d1";
const char kLogiGuidPeripheralControl[] =
    "ffe52d21-8030-4e2c-82d9-f587d00540bd";

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
 * @param vendorID The vendorID string to check against.
 * @return true if it's Logitech, false otherwise.
 */
bool IsLogitechVendorID(std::string vendorID);

/**
 * @brief Reads file contents.
 * @param filepath The file path to read.
 * @param output Output string from reading.
 * @return true if read ok, false otherwise.
 */
bool ReadFileContent(std::string filepath, std::string* output);

/**
 * @brief Converts hex string to int.
 * @param hexString The input hex string.
 * @param outputValue The output int value.
 * @return false if convert failed, true otherwise.
 */
bool ConvertHexStringToInt(std::string hexString, int* outputValue);

/**
 * @brief Converts hex char to unsigned int.
 * @param c The hex char to be converted.
 * @param outputValue Output unsigned int value.
 * @return false if convert failed, true otherwise.
 */
bool ConvertHexCharToUnsignedInt(const char c, uint8_t* outputValue);

/**
 * @brief Gets the device string version.
 * @param major major version number.
 * @param minor minor version number.
 * @param build build version number.
 * @reutrn std::string device version number.
 */
std::string GetDeviceStringVersion(int major, int minor, int build);

/**
 * @brief Gets the device string version
 * @param major major version number.
 * @param minor minor version number.
 * @reutrn std::string device version number.
 */
std::string GetDeviceStringVersion(int major, int minor);

/**
 * @brief Reads binary file content to buffer.
 * @param filepath Path to the binary file.
 * @return buffer from reading. Buffer is empty if read failed.
 */
std::vector<char> ReadBinaryFileContent(std::string filepath);

/**
 * @brief Compares 2 version strings in format major.minor.build or major.minor.
 * @param version1 The version string 1.
 * @param version2 The version string 2.
 * @return 0 equal, -1 if version1 < version2 or 1 if version1 > version2.
 */
int CompareVersions(std::string version1, std::string version2);

/**
 * @brief Splits the string with delimiter into vector.
 * @param string String to be split.
 * @param delimiters The delimiter characters.
 * @return split string vector.
 */
std::vector<std::string> SplitString(std::string string,
                                     std::string delimiters);

/**
 * @brief Gets the unit id from guid.
 * @param guid GUID string.
 * @return unit id or -1 if cannot find.
 */
int GetUnitID(std::string guid);

#endif /* SRC_UTILITIES_H_ */
