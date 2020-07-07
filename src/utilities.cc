// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utilities.h"
#include "usb_device.h"
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include "base/files/dir_reader_posix.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "crypto/libcrypto-compat.h"
#include <openssl/md5.h>
#include <openssl/evp.h>

constexpr unsigned int kLogiVendorID = 0x046D;
// major, minor, build.
constexpr unsigned int kDefaultVersionComponentCount = 3;
constexpr unsigned int kLogiUnitIdCameraVersion = 8;
constexpr unsigned int kLogiUnitIdAccessMmp = 6;
constexpr unsigned int kLogiUnitIdTestDebug = 9;
constexpr unsigned int kLogiUnitIdPeripheralControl = 11;
constexpr int kVersionStringMinMajorMinorLength = 2;
constexpr int kVersionStringMinMajorMinorBuildLength = 3;

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

std::string GetDeviceStringVersion(uint8_t major,
                                   uint8_t minor,
                                   uint8_t build) {
  std::string major_str = base::NumberToString(major);
  std::string minor_str = base::NumberToString(minor);
  std::string build_str = base::NumberToString(build);
  std::stringstream str_stream;
  str_stream << major_str << "." << minor_str << "." << build_str;
  return str_stream.str();
}

bool GetDeviceVersionFromInt(int num, std::string* version_str) {
  if (num < 0)
    return false;
  std::string num_string = std::to_string(num);
  if (num_string.length() <= 0)
    return false;
  std::string major_string = num_string.substr(0, 1);
  int major;
  if (!base::StringToInt(major_string, &major)) {
    return false;
  }

  std::string minor_string = "0";
  if (num_string.length() >= kVersionStringMinMajorMinorLength) {
    minor_string = num_string.substr(1, 1);
  }
  int minor;
  if (!base::StringToInt(minor_string, &minor)) {
    return false;
  }

  std::string build_string = "0";
  if (num_string.length() >= kVersionStringMinMajorMinorBuildLength) {
    build_string = num_string.substr(2);
  }
  int build;
  if (!base::StringToInt(build_string, &build)) {
    return false;
  }
  std::string major2;
  *version_str = base::NumberToString(major) + "." +
                 base::NumberToString(minor) + "." +
                 base::NumberToString(build);
  return true;
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

// Blocking call. Leverage alarm/SIGALRM for termination
bool LockUpdater() {
  int lockfile = open(kLogitechLockFile, O_WRONLY | O_CREAT, 0664);
  if (lockfile < 0)
    return false;

  struct flock fl;
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  if (fcntl(lockfile, F_SETLKW, &fl) < 0)
    return false;
  return true;
}

// Invoked as part of SIGALRM signal processing
// No action if Process already running normally
int CheckUpdater() {
  int lockfile = open(kLogitechLockFile, O_RDWR);
  if (lockfile < 0)
    return kLogiErrorLockFileOpenFailed;

  struct flock fl;
  fl.l_type = F_RDLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  fl.l_pid = 0;

  if (fcntl(lockfile, F_SETLK, &fl) < 0)
    return kLogiErrorLockFileLockFailed;
  return kLogiErrorNoError;
}

bool GenerateRSA(bool test_mode, RSA** out_rsa) {
  const uint8_t test_modulus[] = {
      0xbe, 0x8d, 0x09, 0x90, 0x5d, 0x44, 0x8b, 0x7d, 0x4c, 0x4b, 0x60,
      0x68, 0xca, 0xb7, 0x1e, 0x0b, 0x6c, 0xab, 0x05, 0x16, 0x9f, 0xac,
      0x5a, 0x1e, 0x5a, 0x32, 0x9e, 0x85, 0x9a, 0xb8, 0x11, 0x52, 0x2b,
      0x92, 0x3f, 0x58, 0xa4, 0x89, 0xb7, 0x8c, 0xf7, 0x75, 0x9e, 0xb7,
      0x02, 0x95, 0xee, 0x3c, 0x7b, 0x88, 0x26, 0x49, 0x83, 0x3a, 0xb0,
      0xbf, 0xe6, 0xfd, 0x9b, 0x14, 0xd6, 0x9e, 0x7e, 0xe9};

  const uint8_t modulus[] = {
      0xd5, 0x4b, 0x8a, 0xff, 0xb1, 0xaf, 0x5d, 0x83, 0xc6, 0x74, 0xde,
      0x6f, 0xd2, 0x93, 0x60, 0x1b, 0xb8, 0xdf, 0xc2, 0x0b, 0x20, 0xd5,
      0x35, 0x87, 0x04, 0x2e, 0xaa, 0x92, 0x58, 0x62, 0xcb, 0x83, 0x68,
      0xfd, 0xf4, 0x59, 0x81, 0x1f, 0xb9, 0x89, 0x45, 0x69, 0x60, 0xd3,
      0x36, 0xd4, 0x52, 0x02, 0xfd, 0x6c, 0xf4, 0x4f, 0x3c, 0xaa, 0x98,
      0xc8, 0xd9, 0x4c, 0x30, 0xb0, 0xba, 0x1d, 0x7e, 0x9d};

  const uint8_t exponent_key[] = {1, 0, 1};
  BIGNUM* mod = NULL;
  BIGNUM* exponent = NULL;
  if (test_mode) {
    mod = BN_bin2bn(test_modulus, sizeof(test_modulus), NULL);
  } else {
    mod = BN_bin2bn(modulus, sizeof(modulus), NULL);
  }
  if (!mod)
    return false;
  exponent = BN_bin2bn(exponent_key, sizeof(exponent_key), NULL);
  if (!exponent)
    return false;

  RSA_set0_key(*out_rsa, mod, exponent, NULL);
  return true;
}

bool ConvertHexStringToData(std::string hex_string,
                            std::vector<uint8_t>* data) {
  std::string hex = hex_string;
  if (hex_string.length() % 2 != 0)
    hex = "0" + hex_string;
  for (int i = 0; i < hex.size(); i = i + 2) {
    std::string substring = hex.substr(i, 2);
    int value;
    bool result = ConvertHexStringToInt(substring, &value);
    if (result == false)
      return false;
    (*data).push_back(static_cast<uint8_t>(value));
  }
  return true;
}

bool ValidateSignature(bool test_mode,
                       std::vector<uint8_t> payload,
                       std::vector<uint8_t> sig) {
  if (payload.empty() || sig.empty())
    return false;
  RSA* rsa = RSA_new();

  if (!GenerateRSA(test_mode, &rsa)) {
    if (rsa)
      RSA_free(rsa);
    return false;
  }

  // Decrypt sig to MD5 sum.
  std::vector<uint8_t> decrypted_sig(RSA_size(rsa));
  int decrypted_sig_len = RSA_public_decrypt(
      sig.size(), sig.data(), decrypted_sig.data(), rsa, RSA_PKCS1_PADDING);

  // MD5 payload.
  std::vector<uint8_t> digest_message(MD5_DIGEST_LENGTH);
  MD5(payload.data(), payload.size(), digest_message.data());

  if (decrypted_sig_len <= 0) {
    if (rsa)
      RSA_free(rsa);
    return false;
  }

  if (decrypted_sig_len < MD5_DIGEST_LENGTH) {
    if (rsa)
      RSA_free(rsa);
    return false;
  }

  if (decrypted_sig_len == MD5_DIGEST_LENGTH) {
    if (rsa)
      RSA_free(rsa);
    return (std::equal(decrypted_sig.begin(),
                       decrypted_sig.begin() + MD5_DIGEST_LENGTH - 1,
                       digest_message.begin()));
  }

  // Decrypted sig is possibly ANS1 header encoded.
  // ANS1 header is 3020300c06082a864886f70d020505000410.
  const std::vector<uint8_t> ans1_header = {0x30, 0x20, 0x30, 0x0C, 0x06, 0x08,
                                            0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D,
                                            0x02, 0x05, 0x05, 0x00, 0x04, 0x10};
  if (decrypted_sig_len < ans1_header.size()) {
    if (rsa)
      RSA_free(rsa);
    return false;
  }
  if (!std::equal(decrypted_sig.begin(),
                  decrypted_sig.begin() + ans1_header.size() - 1,
                  ans1_header.begin())) {
    if (rsa)
      RSA_free(rsa);
    return false;
  }
  int result = RSA_verify(NID_md5, digest_message.data(), MD5_DIGEST_LENGTH,
                          sig.data(), sig.size(), rsa);
  if (rsa)
    RSA_free(rsa);
  return (result == 1) ? true : false;
}

bool ValidateSignature(bool test_mode, std::string payload, std::string sig) {
  std::vector<uint8_t> payload_data = ReadBinaryFileContent(payload);
  if (payload_data.empty())
    return false;

  std::string sig_string;
  base::FilePath sig_path(sig.c_str());
  if (!ReadFileToString(sig_path, &sig_string))
    return false;

  std::vector<uint8_t> sig_data;
  if (!ConvertHexStringToData(sig_string, &sig_data))
    return false;
  bool validated = ValidateSignature(test_mode, payload_data, sig_data);
  if (!validated) {
    // Sig signed by live server needs to be reverted.
    std::reverse(std::begin(sig_data), std::end(sig_data));
    validated = ValidateSignature(test_mode, payload_data, sig_data);
  }
  return validated;
}

bool ValidateSignature(bool test_mode, std::string payload) {
  std::string sig = payload + ".sig";
  return ValidateSignature(test_mode, payload, sig);
}

uint8_t ReverseBits(uint8_t value) {
  int bitcount = sizeof(value) * 8;
  uint8_t reverse = 0;
  for (int i = 0; i < bitcount; i++) {
    // Loops through the bit from right to left. If the bit is 0, just ignore
    // it. If the bit is 1, set its opposite bit to 1.
    if ((value & (1 << i)))
      reverse |= 1 << ((bitcount - 1) - i);
  }
  return reverse;
}
