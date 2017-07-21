// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <iostream>
#include "base/process/process_iterator.h"
#include "composite_device.h"
#include "utilities.h"
#include "version.h"

namespace {
const char kLogitechPtzPro2Pid[] = "0x85f";
const char kVideoImagePath[] =
    "/lib/firmware/logitech/ptzpro2/ptzpro2_video.bin";
const char kEepromImagePath[] =
    "/lib/firmware/logitech/ptzpro2/ptzpro2_eeprom.s19";
const char kMcu2ImagePath[] = "/lib/firmware/logitech/ptzpro2/ptzpro2_mcu2.bin";

/**
 * @brief Print the device version info
 * @param infoMap The map containing device versions
 */
void printDevicesVersion(std::map<int, std::string> infoMap) {
  LOG(INFO) << "Video version:  " << infoMap[kLogiDeviceVideo].c_str();
  LOG(INFO) << "Eeprom version: " << infoMap[kLogiDeviceEeprom].c_str();
  LOG(INFO) << "Mcu2 version:   " << infoMap[kLogiDeviceMcu2].c_str();
}

/**
 * @brief Print the image version info
 * @param infoMap The map containing image versions
 */
void printImagesVersion(std::map<int, std::string> infoMap) {
  LOG(INFO) << "Video image version:  " << infoMap[kLogiDeviceVideo].c_str();
  LOG(INFO) << "Eeprom image version: " << infoMap[kLogiDeviceEeprom].c_str();
  LOG(INFO) << "Mcu2 image version:   " << infoMap[kLogiDeviceMcu2].c_str();
}

/**
 * @brief Configures the logging system.
 * @param log_file Specifies the log file to redirect to or stdout for console
 * output.
 */
void configureLogging(std::string log_file) {
  if (log_file.empty()) {
    // Default log to syslog.
    brillo::InitLog(brillo::InitFlags::kLogToSyslog |
                    brillo::InitFlags::kLogToStderrIfTty);
  } else {
    // Logs to file.
    logging::LoggingSettings logging_settings;
    logging_settings.logging_dest = logging::LOG_TO_FILE;
    logging_settings.log_file = log_file.c_str();
    logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
    logging::InitLogging(logging_settings);
  }
}
}  // namespace

/**
 * main
 */
int main(int argc, char** argv) {
  // Parse the flags
  DEFINE_bool(binary_version, false, "Show this binary version.");
  DEFINE_bool(device_version, false, "Show device versions.");
  DEFINE_bool(image_version, false, "Show image versions.");
  DEFINE_bool(update, false, "Perform firmware update.");
  DEFINE_string(log_to, "", "Specify log file to write messages to.");
  brillo::FlagHelper::Init(argc, argv, "logitech-updater");

  // Configures log file.
  configureLogging(FLAGS_log_to);

  // If there is another instance of this executable is running, we want to exit
  // now. We dont want to have multiple executables updating the same device at
  // the same time. This should take care of cases where device is rebooting
  // while updating and udev triggers the updater again.
  if (base::GetProcessCount("logitech-updater", NULL) > 1) {
    LOG(INFO) << "Another logitech-updater process is running. Existing now.";
    return 0;
  }

  int error;
  std::string videoVersion, eepromVersion, mcu2Version;
  std::string videoImageVersion, eepromImageVersion, mcu2ImageVersion;
  std::map<int, std::string> infoMap;  // information map
  std::shared_ptr<CompositeDevice> ptzpro2Device =
      std::make_shared<CompositeDevice>(
          kLogitechPtzPro2Pid, kLogitechPtzPro2Pid, kLogitechPtzPro2Pid);
  ptzpro2Device->videoImageBuffer = ReadBinaryFileContent(kVideoImagePath);
  ptzpro2Device->eepromImageBuffer = ReadBinaryFileContent(kEepromImagePath);
  ptzpro2Device->mcu2ImageBuffer = ReadBinaryFileContent(kMcu2ImagePath);
  if (ptzpro2Device->videoImageBuffer.size() == 0)
    LOG(ERROR) << "Error reading video binary file.";
  if (ptzpro2Device->eepromImageBuffer.size() == 0)
    LOG(ERROR) << "Error reading eeprom binary file.";
  if (ptzpro2Device->mcu2ImageBuffer.size() == 0)
    LOG(ERROR) << "Error reading mcu2 binary file.";

  if (FLAGS_binary_version) {
    LOG(INFO) << kLogiBinaryVersion;
  }
  if (FLAGS_device_version) {
    if (!ptzpro2Device->isDevicePresent()) {
      LOG(ERROR) << "Device not found.";
    } else {
      // get device versions
      ptzpro2Device->getDevicesName(&infoMap);
      std::string name = infoMap[kLogiDeviceVideo];
      error = ptzpro2Device->getDevicesVersion(&infoMap);
      if (error) {
        LOG(ERROR) << "Failed to read device versions.";
      } else {
        LOG(INFO) << "Device name:    " << name.c_str();
        printDevicesVersion(infoMap);
      }
    }
  }
  if (FLAGS_image_version) {
    // get firmware image version
    error = ptzpro2Device->getImagesVersion(&infoMap);
    if (error) {
      LOG(ERROR) << "Failed to read image versions.";
    } else {
      printImagesVersion(infoMap);
    }
  }
  if (FLAGS_update) {
    // check and perform firmware update
    if (!ptzpro2Device->isDevicePresent()) {
      LOG(ERROR) << "Device not found.";
    } else if (ptzpro2Device->isDeviceUpToDate()) {
      LOG(INFO) << "Firmware is up to date.";
    } else {
      LOG(INFO) << "Updating device ... ";
      error = ptzpro2Device->performUpdate();
      if (error) {
        LOG(ERROR) << "Failed to update device.";
      } else {
        // print out new firmware
        LOG(INFO) << "Done. Updated firmwares successfully.";
        ptzpro2Device->getDevicesVersion(&infoMap);
        printDevicesVersion(infoMap);
      }
    }
  }
  return error;
}
