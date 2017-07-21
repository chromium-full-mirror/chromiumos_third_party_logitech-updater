// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utilities.h"
#include <fstream>
#include <sstream>
#include "base/files/dir_reader_posix.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

constexpr unsigned int kLogiVendorID = 0x046D;
constexpr unsigned int kDefaultVersionComponentCount =
    3;  // major, minor, build
constexpr unsigned int kLogiUnitIdCameraVersion = 8;
constexpr unsigned int kLogiUnitIdAccessMmp = 6;
constexpr unsigned int kLogiUnitIdTestDebug = 9;
constexpr unsigned int kLogiUnitIdPeripheralControl = 11;

bool GetDirectoryContents(std::string directory,
                          std::vector<std::string>* contents) {
  base::DirReaderPosix reader(directory.c_str());
  if (!reader.IsValid())
    return false;

  while (reader.Next())
    (*contents).push_back(std::string(reader.name()));
  return true;
}

bool IsLogitechVendorID(std::string vendorID) {
  if (vendorID.empty())
    return false;

  int vidNum;

  if (!ConvertHexStringToInt(vendorID, &vidNum))
    return false;
  return (vidNum == kLogiVendorID);
}

bool ReadFileContent(std::string filepath, std::string* output) {
  if (filepath.empty())
    return false;

  base::File::Info info;
  base::FilePath inputFilePath(filepath.c_str());
  GetFileInfo(inputFilePath, &info);
  if (info.size <= 0)
    return false;

  char* buffer = new char[info.size];
  bool result = ReadFile(inputFilePath, buffer, info.size);
  *output = buffer;
  delete[] buffer;
  return result;
}

bool ConvertHexStringToInt(std::string hexString, int* outputValue) {
  if (hexString.empty())
    return false;
  std::stringstream stringStream;
  stringStream << std::hex << hexString;
  unsigned int hexNumber;
  stringStream >> hexNumber;
  if (outputValue)
    *outputValue = hexNumber;
  return true;
}

bool ConvertHexCharToUnsignedInt(const char c, uint8_t* outputValue) {
  if (!isxdigit(c))
    return false;
  char value = base::HexDigitToInt(c);
  *outputValue = value;
  return true;
}

std::string GetDeviceStringVersion(int major, int minor, int build) {
  std::stringstream stringStream;
  stringStream << major << "." << minor << "." << build;
  return stringStream.str();
}

std::string GetDeviceStringVersion(int major, int minor) {
  std::stringstream stringStream;
  stringStream << major << "." << minor;
  return stringStream.str();
}

std::vector<char> ReadBinaryFileContent(std::string filepath) {
  std::vector<char> contents;
  if (filepath.empty())
    return contents;

  std::ifstream input(filepath.c_str(), std::ios::binary);
  if (!input.is_open())
    return contents;

  std::vector<char> buffer((std::istreambuf_iterator<char>(input)),
                           (std::istreambuf_iterator<char>()));
  return buffer;
}

int CompareVersions(std::string version1, std::string version2) {
  std::vector<std::string> firstTokens = SplitString(version1, ".");
  std::vector<std::string> secondTokens = SplitString(version2, ".");

  // case major.minor, set the build to 0 so we can compare easily
  while (firstTokens.size() < kDefaultVersionComponentCount)
    firstTokens.push_back("0");
  while (secondTokens.size() < kDefaultVersionComponentCount)
    secondTokens.push_back("0");

  for (int i = 0; i < kDefaultVersionComponentCount; i++) {
    int firstVersion;
    int secondVersion;
    base::StringToInt(firstTokens[i], &firstVersion);
    base::StringToInt(secondTokens[i], &secondVersion);
    if (firstVersion > secondVersion)
      return 1;
    if (firstVersion < secondVersion)
      return -1;
  }
  return 0;
}

std::vector<std::string> SplitString(std::string string,
                                     std::string delimiters) {
  return base::SplitString(
      string, delimiters, base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
}

int GetUnitID(std::string guid) {
  if (guid.compare(kLogiGuidDeviceInfo) == 0)
    return kLogiUnitIdCameraVersion;
  if (guid.compare(kLogiGuidAITCustom) == 0)
    return kLogiUnitIdAccessMmp;
  if (guid.compare(kLogiGuidTestDebug) == 0)
    return kLogiUnitIdTestDebug;
  if (guid.compare(kLogiGuidPeripheralControl) == 0)
    return kLogiUnitIdPeripheralControl;
  return -1;
}
