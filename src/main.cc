// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <iostream>
#include "base/files/file.h"
#include "composite_device.h"
#include "utilities.h"
#include "version.h"

namespace {
// PTZ Pro 2 pid.
const char kLogiPtzPro2Pid[] = "0x85f";

// MeetUp pids
const char kLogiMeetUpAudioPid[] = "0x0867";
const char kLogiMeetUpAudioDfuPid[] = "0x0859";
const char kLogiMeetUpVideoPid[] = "0x0866";

// These device names are used where real device name cannot be queried from
// the device, for example, querying image info, checking if device is present,
// etc...
const char kLogiPtzPro2Name[] = "PTZ Pro 2";
const char kLogiMeetUpName[] = "MeetUp";

// PTZ Pro 2 binary image paths.
const char kLogiPtzPro2VideoImagePath[] =
    "/lib/firmware/logitech/ptzpro2/ptzpro2_video.bin";
const char kLogiPtzPro2EepromImagePath[] =
    "/lib/firmware/logitech/ptzpro2/ptzpro2_eeprom.s19";
const char kLogiPtzPro2Mcu2ImagePath[] =
    "/lib/firmware/logitech/ptzpro2/ptzpro2_mcu2.bin";

// MeetUp binary image paths.
const char kLogiMeetUpVideoImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_video.bin";
const char kLogiMeetUpEepromImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_eeprom.s19";
const char kLogiMeetUpAudioImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_audio.bin";
const char kLogiMeetUpCodecImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_codec.bin";
const char kLogiMeetUpBleImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_ble.bin";
const char kLogiMeetUpAudioLogicoolImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_audio_logicool.bin";
const char kLogiMeetUpEepromLogicoolImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_eeprom_logicool.s19";

/**
 * @brief Prints version info from info map.
 * @param info_map The map containing versions.
 */
void PrintVersions(std::map<int, std::string> info_map) {
  if (info_map.find(kLogiDeviceVideo) != info_map.end())
    LOG(INFO) << "Video version:  " << info_map[kLogiDeviceVideo].c_str();
  if (info_map.find(kLogiDeviceEeprom) != info_map.end())
    LOG(INFO) << "Eeprom version: " << info_map[kLogiDeviceEeprom].c_str();
  if (info_map.find(kLogiDeviceMcu2) != info_map.end())
    LOG(INFO) << "Mcu2 version:   " << info_map[kLogiDeviceMcu2].c_str();
  if (info_map.find(kLogiDeviceAudio) != info_map.end())
    LOG(INFO) << "Audio version:  " << info_map[kLogiDeviceAudio].c_str();
  if (info_map.find(kLogiDeviceCodec) != info_map.end())
    LOG(INFO) << "Codec version:  " << info_map[kLogiDeviceCodec].c_str();
  if (info_map.find(kLogiDeviceBle) != info_map.end())
    LOG(INFO) << "BLE version:    " << info_map[kLogiDeviceBle].c_str();
  std::cout << std::endl;
}

/**
 * @brief Checks and prints the device version info.
 * @param device The composite device to check.
 */
void CheckDeviceVersion(std::shared_ptr<CompositeDevice> device) {
  if (!device->IsDevicePresent())
    return;

  std::string name;
  device->GetDeviceName(&name);
  std::map<int, std::string> info_map;
  int error = device->GetDevicesVersion(&info_map);
  if (error) {
    LOG(ERROR) << "Failed to read " << name.c_str() << " versions.";
  } else {
    LOG(INFO) << "Device name:    " << name.c_str();
    PrintVersions(info_map);
  }
}

/**
 * @brief Checks and prints the image version info.
 * @param device The composite device to check image for.
 * @param image_name Name of the image to print for clearer log message.
 */
void CheckImageVersion(std::shared_ptr<CompositeDevice> device,
                       const char* image_name) {
  std::map<int, std::string> info_map;
  int error = device->GetImagesVersion(&info_map);
  if (error) {
    LOG(ERROR) << "Failed to read " << image_name << " image versions.";
  } else {
    LOG(INFO) << image_name << " Versions:";
    PrintVersions(info_map);
  }
}

/**
 * @brief Applies PTZ Pro 2 firmwares.
 * @param test_sig If true, validate payload with test key and test sig,
 * otherwise validate it with live key and live sig.
 * @param device PTZ Pro 2 composite device.
 */
int ApplyPTZPro2Firmware(std::shared_ptr<CompositeDevice> device,
                         bool test_sig) {
  // Validate signatures.
  bool validated = ValidateSignature(test_sig, kLogiPtzPro2VideoImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiPtzPro2Name
               << " video firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiPtzPro2EepromImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiPtzPro2Name
               << " eeprom firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiPtzPro2Mcu2ImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiPtzPro2Name
               << " mcu2 firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  device->SetImageBuffer(kLogiDeviceVideo,
                         ReadBinaryFileContent(kLogiPtzPro2VideoImagePath));
  device->SetImageBuffer(kLogiDeviceEeprom,
                         ReadBinaryFileContent(kLogiPtzPro2EepromImagePath));
  device->SetImageBuffer(kLogiDeviceMcu2,
                         ReadBinaryFileContent(kLogiPtzPro2Mcu2ImagePath));
  return kLogiErrorNoError;
}

/**
 * @brief Checks brand and applies firmware for MeetUp device.
 * @param device MeetUp device to check and apply firmware.
 * @return kLogiErrorNoError if checked ok, error code otherwise.
 */
int ApplyMeetUpFirmware(std::shared_ptr<CompositeDevice> device,
                        bool test_sig) {
  // Validates signatures.
  bool validated = ValidateSignature(test_sig, kLogiMeetUpVideoImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " video firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpCodecImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " codec firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpBleImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " ble firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpEepromImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " eeprom firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpEepromLogicoolImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " Logicool eeprom firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpAudioImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " audio firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiMeetUpAudioLogicoolImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiMeetUpName
               << " Logicool audio firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }

  bool logicool = false;
  if (device->IsDevicePresent()) {
    int error = device->IsLogicool(true, false, &logicool);
    if (error)
      return error;
  }
  device->SetImageBuffer(kLogiDeviceVideo,
                         ReadBinaryFileContent(kLogiMeetUpVideoImagePath));
  device->SetImageBuffer(kLogiDeviceCodec,
                         ReadBinaryFileContent(kLogiMeetUpCodecImagePath));
  device->SetImageBuffer(kLogiDeviceBle,
                         ReadBinaryFileContent(kLogiMeetUpBleImagePath));
  std::string eeprom_image_path = kLogiMeetUpEepromImagePath;
  std::string audio_image_path = kLogiMeetUpAudioImagePath;
  if (logicool) {
    eeprom_image_path = kLogiMeetUpEepromLogicoolImagePath;
    audio_image_path = kLogiMeetUpAudioLogicoolImagePath;
  }
  device->SetImageBuffer(kLogiDeviceEeprom,
                         ReadBinaryFileContent(eeprom_image_path));
  device->SetImageBuffer(kLogiDeviceAudio,
                         ReadBinaryFileContent(audio_image_path));

  return kLogiErrorNoError;
}

/**
 * @brief Configures the logging system.
 * @param log_file Specifies the log file to redirect to or stdout for console
 * output.
 */
void ConfigureLogging(std::string log_file) {
  if (log_file.empty()) {
    // Default log to syslog.
    brillo::InitLog(brillo::InitFlags::kLogToSyslog |
                    brillo::InitFlags::kLogToStderrIfTty);
  } else if (log_file == "stdout") {
    logging::LoggingSettings logging_settings;
    logging::InitLogging(logging_settings);
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

  // Performs composite device components one at a time. This method is
  // to integrate with Udev since updating audio devices requires reboot and
  // causing udev issues on changing user and group that prevents read and write
  // to device. This method updates component device, then reboots device. Udev
  // can keep calling the updater with this method to update all components when
  // device reboots and mounts back.
  DEFINE_bool(update_components, false,
              "Perform firmware update on one component at a time. This "
              "option is used to integrate with Udev.");

  // Performs firmware update on audio device when it is in DFU mode.
  DEFINE_bool(update_dfu_audio, false,
              "Perform audio firmware update when audio device is in DFU mode. "
              "This option is used to integrate with Udev.");

  DEFINE_bool(lock, false,
              "Check and lock the firmware updater with lock file at "
              "/tmp/logitech-updater.lock. When run with this option, only 1 "
              "instance of updater can be run at the time during the updating "
              "process. This option is used to integrate with Udev.");

  DEFINE_bool(force, false, "Force firmware update.");
  DEFINE_bool(
      test_sig, false,
      "If set, validate firmware payload with test key and test sig. "
      "Otherwise, validate firmware payload with live key and live sig.");

  // When run under Udev, device can still be booting up or calibrating and
  // fails to update until it is ready. Use this to delay the update process.
  DEFINE_uint64(delay, 0,
                "Delay this executable when it starts. This option is used to "
                "integrate Udev.");

  DEFINE_string(log_to, "", "Specify log file to write messages to.");
  brillo::FlagHelper::Init(argc, argv, "logitech-updater");

  // Configures log file.
  ConfigureLogging(FLAGS_log_to);

  if (FLAGS_lock && !LockUpdater()) {
    LOG(ERROR) << "There is another logitech-updater running. Exiting now...";
    return 0;
  }
  if (FLAGS_delay > 0) {
    LOG(INFO) << "Delaying " << FLAGS_delay << " seconds.";
    sleep(FLAGS_delay);
  }
  int error = kLogiErrorNoError;

  // PTZ Pro 2 device.
  std::shared_ptr<CompositeDevice> ptzpro2_device =
      std::make_shared<CompositeDevice>(kLogiPtzPro2Pid, kLogiPtzPro2Pid,
                                        kLogiPtzPro2Pid);
  ApplyPTZPro2Firmware(ptzpro2_device, FLAGS_test_sig);
  if (!ptzpro2_device->AreImagesPresent())
    LOG(ERROR) << kLogiPtzPro2Name << " image not found.";

  // MeetUp device.
  std::shared_ptr<CompositeDevice> meetup_device =
      std::make_shared<CompositeDevice>();
  meetup_device->AddDevice(kLogiDeviceVideo, kLogiMeetUpVideoPid);
  meetup_device->AddDevice(kLogiDeviceEeprom, kLogiMeetUpVideoPid);
  meetup_device->AddDevice(kLogiDeviceAudio, kLogiMeetUpAudioPid,
                           kLogiMeetUpAudioDfuPid);
  meetup_device->AddDevice(kLogiDeviceCodec, kLogiMeetUpAudioPid);
  meetup_device->AddDevice(kLogiDeviceBle, kLogiMeetUpAudioPid);
  meetup_device->SetSupportBle(true);
  ApplyMeetUpFirmware(meetup_device, FLAGS_test_sig);
  if (!meetup_device->AreImagesPresent())
    LOG(ERROR) << kLogiMeetUpName << " image not found.";

  if (FLAGS_binary_version)
    std::cout << kLogiBinaryVersion << std::endl;

  if (FLAGS_device_version) {
    CheckDeviceVersion(ptzpro2_device);
    CheckDeviceVersion(meetup_device);
  }

  if (FLAGS_image_version) {
    ApplyPTZPro2Firmware(ptzpro2_device, FLAGS_test_sig);
    CheckImageVersion(ptzpro2_device, kLogiPtzPro2Name);

    ApplyMeetUpFirmware(meetup_device, FLAGS_test_sig);
    CheckImageVersion(meetup_device, kLogiMeetUpName);
  }

  ptzpro2_device->SetForceUpdate(FLAGS_force);
  meetup_device->SetForceUpdate(FLAGS_force);

  if (FLAGS_update_components || FLAGS_update_dfu_audio) {
    if (FLAGS_update_components && ptzpro2_device->IsDevicePresent()) {
      error = ApplyPTZPro2Firmware(ptzpro2_device, FLAGS_test_sig);
      if (!error)
        error = ptzpro2_device->PerformComponentUpdate();
    }

    if (meetup_device->IsDevicePresent()) {
      error = ApplyMeetUpFirmware(meetup_device, FLAGS_test_sig);
      if (!error) {
        if (FLAGS_update_components)
          error = meetup_device->PerformComponentUpdate();
        if (FLAGS_update_dfu_audio)
          error = meetup_device->PerformDfuAudioUpdate();
      }
    }
  }
  return error;
}
