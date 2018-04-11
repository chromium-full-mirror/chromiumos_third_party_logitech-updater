// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utilities.h"
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "base/files/dir_reader_posix.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

constexpr unsigned int kLogiVendorID = 0x046D;
// major, minor, build.
constexpr unsigned int kDefaultVersionComponentCount = 3;
constexpr unsigned int kLogiUnitIdCameraVersion = 8;
constexpr unsigned int kLogiUnitIdAccessMmp = 6;
constexpr unsigned int kLogiUnitIdTestDebug = 9;
constexpr unsigned int kLogiUnitIdPeripheralControl = 11;

const char kLogitechLockFile[] = "/tmp/logitech-updater.lock";

bool GetDirectoryContents(std::string directory,
                          std::vector<std::string>* contents) {
  base::DirReaderPosix reader(directory.c_str());
  if (!reader.IsValid())
    return false;

  while (reader.Next())
    (*contents).push_back(std::string(reader.name()));
  return true;
}

bool IsLogitechVendorID(std::string vendor_id) {
  if (vendor_id.empty())
    return false;

  int vendor_id_num;
  if (!ConvertHexStringToInt(vendor_id, &vendor_id_num))
    return false;
  return (vendor_id_num == kLogiVendorID);
}

bool ReadFileContent(std::string filepath, std::string* output) {
  if (filepath.empty())
    return false;

  base::File::Info info;
  base::FilePath input_file_path(filepath.c_str());
  GetFileInfo(input_file_path, &info);
  if (info.size <= 0)
    return false;

  char* buffer = new char[info.size]();
  bool result = ReadFile(input_file_path, buffer, info.size);

  *output = buffer;
  delete[] buffer;
  return result;
}

bool ConvertHexStringToInt(std::string hex_string, int* output_value) {
  if (hex_string.empty())
    return false;
  std::stringstream string_stream;
  string_stream << std::hex << hex_string;
  unsigned int hex_number;
  string_stream >> hex_number;
  if (output_value)
    *output_value = hex_number;
  return true;
}

bool ConvertHexCharToUnsignedInt(const char c, uint8_t* output_value) {
  if (!isxdigit(c))
    return false;
  char value = base::HexDigitToInt(c);
  *output_value = value;
  return true;
}

std::string GetDeviceStringVersion(int major, int minor, int build) {
  std::stringstream string_stream;
  string_stream << major << "." << minor << "." << build;
  return string_stream.str();
}

std::string GetDeviceStringVersion(int major, int minor) {
  std::stringstream string_stream;
  string_stream << major << "." << minor;
  return string_stream.str();
}

bool GetDeviceVersionNumber(std::string version,
                            int* major,
                            int* minor,
                            int* build) {
  std::vector<std::string> tokens = SplitString(version, ".");
  if (tokens.size() == 0)
    return false;

  // Pop all versions.
  if (tokens.size() > 0) {
    base::StringToInt(tokens.front(), major);
    tokens.erase(tokens.begin());
  }
  if (tokens.size() > 0) {
    base::StringToInt(tokens.front(), minor);
    tokens.erase(tokens.begin());
  }
  if (tokens.size() > 0) {
    base::StringToInt(tokens.front(), build);
    tokens.erase(tokens.begin());
  }
  return true;
}

std::vector<uint8_t> ReadBinaryFileContent(std::string filepath) {
  std::vector<uint8_t> contents;
  if (filepath.empty())
    return contents;

  std::ifstream input(filepath.c_str(), std::ios::binary);
  if (!input.is_open())
    return contents;
  std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(input)),
                              (std::istreambuf_iterator<char>()));
  return buffer;
}

int CompareVersions(std::string version1, std::string version2) {
  std::vector<std::string> first_tokens = SplitString(version1, ".");
  std::vector<std::string> second_tokens = SplitString(version2, ".");

  // Case major.minor, set the build to 0 to compare easily.
  while (first_tokens.size() < kDefaultVersionComponentCount)
    first_tokens.push_back("0");
  while (second_tokens.size() < kDefaultVersionComponentCount)
    second_tokens.push_back("0");

  for (int i = 0; i < kDefaultVersionComponentCount; i++) {
    int first_version;
    int second_version;
    base::StringToInt(first_tokens[i], &first_version);
    base::StringToInt(second_tokens[i], &second_version);
    if (first_version > second_version)
      return 1;
    if (first_version < second_version)
      return -1;
  }
  return 0;
}

std::vector<std::string> SplitString(std::string string,
                                     std::string delimiters) {
  return base::SplitString(string, delimiters, base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
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

std::vector<std::string> FindDevices(std::string mount_point,
                                     std::string device_point,
                                     std::string usb_pid) {
  std::vector<std::string> device_paths;
  if (usb_pid.empty() || mount_point.empty() || device_point.empty())
    return device_paths;

  std::vector<std::string> contents;
  bool getOk = GetDirectoryContents(mount_point, &contents);
  if (!getOk)
    return device_paths;  // Failed to get contents of mount point.

  for (auto const& content : contents) {
    if (content.compare(".") == 0 || content.compare("..") == 0)
      continue;  // Ignores . and .. directory.

    std::string device_dir = std::string(mount_point) + "/" + content;

    // Reads mount point device vendor id & product id.
    std::string product_id_path = device_dir + device_point + "/idProduct";
    std::string vendor_id_path = device_dir + device_point + "/idVendor";
    std::string vendor_id;
    std::string product_id;
    if (!ReadFileContent(vendor_id_path, &vendor_id))
      continue;  // Failed to read vendor ID.

    if (!IsLogitechVendorID(vendor_id))
      continue;  // This is not a Logitech device.
    if (!ReadFileContent(product_id_path, &product_id))
      continue;  // Failed to read product ID.

    int pidnum = 0;
    if (!ConvertHexStringToInt(product_id, &pidnum))
      continue;  // Failed to convert product ID to int.

    int usb_pid_num = 0;
    if (!ConvertHexStringToInt(usb_pid, &usb_pid_num))
      continue;
    if (pidnum == usb_pid_num) {
      std::string dev_path = "/dev/" + content;
      device_paths.push_back(dev_path);
    }
  }
  return device_paths;
}

std::vector<std::string> FindUsbBus(std::string vid, std::string pid) {
  std::vector<std::string> bus_paths;
  if (vid.empty() || pid.empty())
    return bus_paths;
  std::vector<std::string> contents;
  std::string mount_point = "/sys/bus/usb/devices";
  bool getOk = GetDirectoryContents(mount_point, &contents);
  if (!getOk)
    return bus_paths;

  std::vector<std::pair<int, int>> bus;
  for (auto const& content : contents) {
    if (content.compare(".") == 0 || content.compare("..") == 0)
      continue;

    std::string device_dir = std::string(mount_point) + "/" + content;
    // Reads mount point device vendor id & product id.
    std::string product_id_path = device_dir + "/idProduct";
    std::string vendor_id_path = device_dir + "/idVendor";
    std::string file_vid;
    std::string file_pid;
    int file_vidnum = 0;
    int file_pidnum = 0;
    int vidnum = 0;
    int pidnum = 0;
    if (!ReadFileContent(vendor_id_path, &file_vid))
      continue;  // Failed to read vendor ID.
    if (!ReadFileContent(product_id_path, &file_pid))
      continue;  // Failed to read product ID.
    if (!ConvertHexStringToInt(file_vid, &file_vidnum))
      continue;  // Failed to convert product ID to int.
    if (!ConvertHexStringToInt(file_pid, &file_pidnum))
      continue;  // Failed to convert product ID to int.
    if (!ConvertHexStringToInt(vid, &vidnum))
      continue;  // Failed to convert product ID to int.
    if (!ConvertHexStringToInt(pid, &pidnum))
      continue;  // Failed to convert product ID to int.
    if (file_vidnum != vidnum || file_pidnum != pidnum)
      continue;  // Not this vendor/product id.

    std::string busnum_file = device_dir + "/busnum";
    std::string devnum_file = device_dir + "/devnum";
    std::string busnum_string, devnum_string;
    if (!ReadFileContent(busnum_file, &busnum_string))
      continue;  // Failed to read busnum file.
    if (!ReadFileContent(devnum_file, &devnum_string))
      continue;  // Failed to read devnum file.
    int busnum, devnum;

    base::TrimWhitespaceASCII(busnum_string, base::TRIM_ALL, &busnum_string);
    base::TrimWhitespaceASCII(devnum_string, base::TRIM_ALL, &devnum_string);
    if (!base::StringToInt(busnum_string.c_str(), &busnum))
      continue;
    if (!base::StringToInt(devnum_string.c_str(), &devnum))
      continue;
    std::stringstream bus_ss;
    std::stringstream dev_ss;
    bus_ss << std::setw(3) << std::setfill('0') << busnum;
    dev_ss << std::setw(3) << std::setfill('0') << devnum;
    std::string path = "/dev/bus/usb/" + bus_ss.str() + "/" + dev_ss.str();
    bus_paths.push_back(path);
  }
  return bus_paths;
}

bool ContainString(std::string str,
                   std::string substring,
                   bool case_sensitive) {
  std::string tmp_str = str;
  std::string tmp_substring = substring;
  if (!case_sensitive) {
    tmp_str = base::ToLowerASCII(str);
    tmp_substring = base::ToLowerASCII(substring);
  }
  if (tmp_str.find(tmp_substring) != std::string::npos)
    return true;
  return false;
}

bool LockUpdater() {
  int lockfile = open(kLogitechLockFile, O_WRONLY | O_CREAT, 0664);
  if (lockfile < 0)
    return false;

  struct flock fl;
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  if (fcntl(lockfile, F_SETLK, &fl) < 0)
    return false;
  return true;
}
