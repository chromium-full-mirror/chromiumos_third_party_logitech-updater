// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/socket.h>
#include <signal.h>  // sigaction()
#include <unistd.h>  // alarm()
#include "base/files/file.h"
#include "composite_device.h"
#include "utilities.h"
#include "audio_device.h"
#include "version.h"

namespace {
// PTZ Pro 2 pid.
const char kLogiPtzPro2Pid[] = "0x85f";

// MeetUp pids
const char kLogiMeetUpAudioPid[] = "0x0867";
const char kLogiMeetUpAudioDfuPid[] = "0x0859";
const char kLogiMeetUpVideoPid[] = "0x0866";

// PTZ Pro pid
const char kLogiPtzProVideoPid[] = "0x0853";

// Tap pids.
const char kLogiTapHdmiPid[] = "0x0876";
const char kLogiTapMcuPid[] = "0x0872";

// Rally pid
const char kLogiRallyTableHubPid[] = "0x088f";
const char kLogiRallyVideoPid[] = "0x0881";
const char kLogiRallyAudioPid[] = "0x0885";
const char kLogiRallyAudioDfuPid[] = "0x0886";

// These device names are used where real device name cannot be queried from
// the device, for example, querying image info, checking if device is present,
// etc...
const char kLogiPtzPro2Name[] = "PTZ Pro 2";
const char kLogiMeetUpName[] = "MeetUp";
const char kLogiPtzProName[] = "PTZ Pro";
const char kLogiTapName[] = "Tap";
const char kLogiRallyName[] = "Rally";

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
    "/lib/firmware/logitech/meetup/meetup_eeprom.s19.bin";
const char kLogiMeetUpAudioImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_audio.bin";
const char kLogiMeetUpCodecImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_codec.bin";
const char kLogiMeetUpBleImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_ble.bin";
const char kLogiMeetUpAudioLogicoolImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_audio_logicool.bin";
const char kLogiMeetUpEepromLogicoolImagePath[] =
    "/lib/firmware/logitech/meetup/meetup_eeprom_logicool.s19.bin";

// PTZ Pro binary image paths.
const char kLogiPtzProVideoImagePath[] =
    "/lib/firmware/logitech/ptzpro/ptzpro_video.bin";
const char kLogiPtzProEepromImagePath[] =
    "/lib/firmware/logitech/ptzpro/ptzpro_eeprom.s19";

// Tap binary image paths.
const char kLogiTapHdmiImagePath[] = "/lib/firmware/logitech/tap/tap_hdmi.bin";
const char kLogiTapHdmiVersionFilePath[] =
    "/lib/firmware/logitech/tap/tap_hdmi_version.bin";

// Rally binaries image path.
const char kLogiRallyImagePath[] = "/lib/firmware/logitech/rally/tablehub.bin";
// Rally binary versions path.
const char kLogiRallyVersionsPath[] =
    "/lib/firmware/logitech/rally/versions.bin";

// Note DEVICES_BEGIN/DEVICES_END are used for range check
// Add new device(s) in-between
enum {
  DEVICES_BEGIN = 0,
  kLogiDeviceTypePTZPro2 = 1,
  kLogiDeviceTypePTZPro = 2,
  kLogiDeviceTypeMeetUpDfu = 3,
  kLogiDeviceTypeMeetUpAudio = 4,
  kLogiDeviceTypeRally = 5,
  kLogiDeviceTypeTapSensor = 6,
  kLogiDeviceTypeTapHDMI = 7,
  DEVICES_END
};

// Timeout for this process to wait, for another instance to finish
constexpr int kLogiProcessWaitMaxTime =
    (kLogiPollingSleep / 1000) * kLogiPollingMaxTimeout;

// Timeout handler function
void TimeoutHandler(int signum) {
  if (signum == SIGALRM) {
    LOG(INFO) << "Checking for logitech-updater running status.";
    int error = CheckUpdater();
    if (error != kLogiErrorNoError) {
      LOG(ERROR)
          << "Another logitech-updater still running. Exiting now. Error="
          << error;
      exit(1);
    }
    // This process has already acquired the file lock
    LOG(INFO) << "logitech-updater running normally. Timeout ignored.";
  }
  return;
}

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
  if (info_map.find(kLogiDeviceHdmi) != info_map.end())
    LOG(INFO) << "HDMI version:   " << info_map[kLogiDeviceHdmi].c_str();
  if (info_map.find(kLogiDeviceVideoBle) != info_map.end())
    LOG(INFO) << "Video BLE version:   "
              << info_map[kLogiDeviceVideoBle].c_str();
  if (info_map.find(kLogiDeviceTableHub) != info_map.end())
    LOG(INFO) << "Table Hub version:   "
              << info_map[kLogiDeviceTableHub].c_str();
  if (info_map.find(kLogiDeviceMicPod) != info_map.end())
    LOG(INFO) << "Mic Pod version:     " << info_map[kLogiDeviceMicPod].c_str();
  if (info_map.find(kLogiDeviceSplitter) != info_map.end())
    LOG(INFO) << "Mic Pod Hub version: "
              << info_map[kLogiDeviceSplitter].c_str();
}

/**
 * @brief Checks and prints the device version info.
 * @param device The composite device to check.
 */
void CheckDeviceVersion(std::shared_ptr<CompositeDevice> device) {
  if (!device->IsDevicePresent())
    return;

  std::string name;
  device->GetDeviceName(name);
  std::map<int, std::string> info_map;
  int error;
  if (name.find(kLogiRallyName) != std::string::npos) {
    error = device->GetDevicesVersionFromAudio(info_map);
    name = name.substr(0, 10);
  } else {
    error = device->GetDevicesVersion(info_map);
  }
  if (error) {
    logging::SystemErrorCode err = logging::GetLastSystemErrorCode();
    LOG(ERROR) << "Failed to read " << name.c_str() << " versions: error code "
               << logging::SystemErrorCodeToString(err) << "::" << error;
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
  int error;
  if (strcmp(image_name, kLogiRallyName) == 0) {
    error = device->GetImagesVersionFromFile(info_map);
  } else {
    error = device->GetImagesVersion(info_map);
  }
  if (error) {
    logging::SystemErrorCode err = logging::GetLastSystemErrorCode();
    LOG(ERROR) << "Failed to read " << image_name
               << " image versions: error code "
               << logging::SystemErrorCodeToString(err) << "::" << error;
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
    int error = device->IsLogicool(true, false, logicool);
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
 * @brief Applies PTZ Pro firmwares.
 * @param device PTZ Pro composite device.
 */
int ApplyPTZProFirmware(std::shared_ptr<CompositeDevice> device,
                        bool test_sig) {
  // Validates signatures.
  bool validated = ValidateSignature(test_sig, kLogiPtzProVideoImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiPtzProName
               << " video firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiPtzProEepromImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiPtzProName
               << " eeprom firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  device->SetImageBuffer(kLogiDeviceVideo,
                         ReadBinaryFileContent(kLogiPtzProVideoImagePath));
  device->SetImageBuffer(kLogiDeviceEeprom,
                         ReadBinaryFileContent(kLogiPtzProEepromImagePath));
  return kLogiErrorNoError;
}

/**
 * @brief Applies Tap firmwares.
 * @param device Tap composite device.
 * @param test_sig True if using test signature.
 */
int ApplyTapFirmware(std::shared_ptr<CompositeDevice> device, bool test_sig) {
  // Validates signatures.
  std::vector<std::string> binpaths = {kLogiTapHdmiImagePath,
                                       kLogiTapHdmiVersionFilePath};
  for (const auto& binpath : binpaths) {
    if (!ValidateSignature(test_sig, binpath)) {
      LOG(ERROR) << "Failed to validate " << kLogiTapName
                 << " binary signature " << binpath.c_str();
      return kLogiErrorSignatureValidationFailed;
    }
  }
  device->SetImageBuffer(kLogiDeviceHdmi,
                         ReadBinaryFileContent(kLogiTapHdmiImagePath));
  device->SetVersionFile(kLogiDeviceHdmi, kLogiTapHdmiVersionFilePath);
  return kLogiErrorNoError;
}

/**
 * @brief Applies Rally firmwares.
 * @param device Rally composite device.
 */
int ApplyRallyFirmware(std::shared_ptr<CompositeDevice> device, bool test_sig) {
  bool validated = ValidateSignature(test_sig, kLogiRallyImagePath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiRallyName
               << " firmware signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  validated = ValidateSignature(test_sig, kLogiRallyVersionsPath);
  if (!validated) {
    LOG(ERROR) << "Failed to validate " << kLogiRallyName
               << " firmware versions signature.";
    return kLogiErrorSignatureValidationFailed;
  }
  device->SetImageBuffer(kLogiDeviceTableHub,
                         ReadBinaryFileContent(kLogiRallyImagePath));
  device->SetImageBuffer(kLogiDeviceAudio,
                         ReadBinaryFileContent(kLogiRallyImagePath));
  device->SetImageBuffer(kLogiDeviceVideo,
                         ReadBinaryFileContent(kLogiRallyImagePath));
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
    logging_settings.log_file_path = log_file.c_str();
    logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
    logging::InitLogging(logging_settings);
  }
}

/**
 * @brief Ensure image(s) exist, check version if requested
 * @param device The composite device to check
 * @param name Name of composite device
 * @param check_version Check and print version
 * @param set_force_update Setup device for force upgrade
 */
int CheckForImage(std::shared_ptr<CompositeDevice> device,
                  const char* name,
                  bool check_version,
                  bool set_force_update) {
  if (!device->AreImagesPresent()) {
    LOG(ERROR) << name << " image not found.";
    return kLogiErrorImageVersionNotFound;
  }
  if (check_version) {
    CheckImageVersion(device, name);
    LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
    return kLogiErrorNoError;  // Done here. Rest is N/A
  }
  device->SetForceUpdate(set_force_update);
  return kLogiErrorNoError;
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

  // Process specified device only
  DEFINE_uint64(id, 0,
                "Device type. PTZPro2=1, PtzPro=2, MeetUpDfu=3, "
                "MeetUpAudio=4, Rally=5, TapSensor=6, TapHDMI=7.");

  DEFINE_string(log_to, "", "Specify log file to write messages to.");
  brillo::FlagHelper::Init(argc, argv, "logitech-updater");

  // Configures log file.
  ConfigureLogging(FLAGS_log_to);
  LOG(INFO) << "Logitech updater instance invoked. ID=" << FLAGS_id;
  if (FLAGS_lock) {
    // LockUpdater is blocking call.
    // Setup alarm/SIGALRM to terminate this instance,
    // if timeout occurs and other instance still running
    struct sigaction action, old_action;
    unsigned int old_timeout;

    // Cancel any existing alarm
    old_timeout = alarm(0);

    // Establish signal handler
    action.sa_handler = TimeoutHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &action, &old_action);

    alarm(kLogiProcessWaitMaxTime);

    if (!LockUpdater()) {
      LOG(ERROR)
          << "There is another logitech-updater running. Exiting now. ID="
          << FLAGS_id;
      return kLogiErrorProcessAlreadyRunning;
    }

    // Reset the signal handler and alarm
    sigaction(SIGALRM, &old_action, NULL);
    alarm(old_timeout);
  }
  if (FLAGS_delay > 0) {
    LOG(INFO) << "Delaying " << FLAGS_delay << " seconds. ID=" << FLAGS_id;
    sleep(FLAGS_delay);
  }
  int error = kLogiErrorNoError;

  if (FLAGS_binary_version) {
    LOG(INFO) << "Binary version: " << kLogiBinaryVersion;
    LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
    return kLogiErrorNoError;  // Done here. Rest is N/A
  }

  if ((FLAGS_id <= DEVICES_BEGIN) || ((FLAGS_id >= DEVICES_END))) {
    LOG(ERROR) << "Unsupported device type: " << FLAGS_id;
    return kLogiErrorDeviceNotPresent;
  }

  std::shared_ptr<CompositeDevice> composite_device = nullptr;
  bool did_update = false;
  switch (FLAGS_id) {
    // PTZ Pro 2 device.
    case kLogiDeviceTypePTZPro2:
      composite_device = std::make_shared<CompositeDevice>(
          kLogiPtzPro2Pid, kLogiPtzPro2Pid, kLogiPtzPro2Pid);

      CheckDeviceVersion(composite_device);
      if (FLAGS_device_version) {
        LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
        return kLogiErrorNoError;  // Done here. Rest is N/A
      }

      // Images must exist for subsequent operations
      error = ApplyPTZPro2Firmware(composite_device, FLAGS_test_sig);
      if (error) {
        LOG(ERROR) << "Exiting updater with code: " << error;
        return error;
      }

      // Ensure image(s) exist, setup for upgrade, if requested
      error = CheckForImage(composite_device, kLogiPtzPro2Name,
                            FLAGS_image_version, FLAGS_force);
      if (error || FLAGS_image_version)
        return error;  // Done here. Rest is N/A

      if (FLAGS_update_components && composite_device->IsDevicePresent())
        error = composite_device->PerformComponentUpdate(&did_update);
      LOG(INFO) << "Exiting updater with code: " << error;
      break;

    case kLogiDeviceTypePTZPro:
      // PTZ Pro device.
      composite_device = std::make_shared<CompositeDevice>();
      composite_device->AddDevice(kLogiDeviceVideo, kLogiPtzProVideoPid);
      composite_device->AddDevice(kLogiDeviceEeprom, kLogiPtzProVideoPid);

      CheckDeviceVersion(composite_device);
      if (FLAGS_device_version) {
        LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
        return kLogiErrorNoError;  // Done here. Rest is N/A
      }

      // Images must exist for subsequent operations
      error = ApplyPTZProFirmware(composite_device, FLAGS_test_sig);
      if (error) {
        LOG(ERROR) << "Exiting updater with code: " << error;
        return error;
      }

      // Ensure image(s) exist, setup for upgrade, if requested
      error = CheckForImage(composite_device, kLogiPtzProName,
                            FLAGS_image_version, FLAGS_force);
      if (error || FLAGS_image_version)
        return error;  // Done here. Rest is N/A

      if (FLAGS_update_components && composite_device->IsDevicePresent())
        error = composite_device->PerformComponentUpdate(&did_update);
      LOG(INFO) << "Exiting updater with code: " << error;
      break;

    case kLogiDeviceTypeMeetUpDfu:
    case kLogiDeviceTypeMeetUpAudio:
      // MeetUp device.
      composite_device = std::make_shared<CompositeDevice>();
      composite_device->AddDevice(kLogiDeviceVideo, kLogiMeetUpVideoPid);
      composite_device->AddDevice(kLogiDeviceEeprom, kLogiMeetUpVideoPid);
      composite_device->AddDevice(kLogiDeviceAudio, kLogiMeetUpAudioPid,
                                  kLogiMeetUpAudioDfuPid);
      composite_device->AddDevice(kLogiDeviceCodec, kLogiMeetUpAudioPid);
      composite_device->AddDevice(kLogiDeviceBle, kLogiMeetUpAudioPid);
      composite_device->SetSupportBle(true);

      CheckDeviceVersion(composite_device);
      if (FLAGS_device_version) {
        LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
        return kLogiErrorNoError;  // Done here. Rest is N/A
      }

      // Images must exist for subsequent operations
      error = ApplyMeetUpFirmware(composite_device, FLAGS_test_sig);
      if (error) {
        LOG(ERROR) << "Exiting updater with code: " << error;
        return error;
      }
      // Ensure image(s) exist, setup for upgrade, if requested
      error = CheckForImage(composite_device, kLogiMeetUpName,
                            FLAGS_image_version, FLAGS_force);
      if (error || FLAGS_image_version)
        return error;  // Done here. Rest is N/A

      if (FLAGS_update_components || FLAGS_update_dfu_audio) {
        if (composite_device->IsDevicePresent()) {
          if (FLAGS_update_components)
            error = composite_device->PerformComponentUpdate(&did_update);
          if (FLAGS_update_dfu_audio)
            error = composite_device->PerformDfuAudioUpdate();
        }
      }
      LOG(INFO) << "Exiting updater with code: " << error;
      break;

    case kLogiDeviceTypeRally:
      // Rally system.
      composite_device = std::make_shared<CompositeDevice>();
      composite_device->AddDevice(kLogiDeviceAudio, kLogiRallyAudioPid,
                                  kLogiRallyAudioDfuPid);
      composite_device->AddDevice(kLogiDeviceVideo, kLogiRallyVideoPid);
      composite_device->AddDevice(kLogiDeviceTableHub, kLogiRallyTableHubPid);

      CheckDeviceVersion(composite_device);
      if (FLAGS_device_version) {
        LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
        return kLogiErrorNoError;  // Done here. Rest is N/A
      }

      // Images must exist for subsequent operations
      error = ApplyRallyFirmware(composite_device, FLAGS_test_sig);
      if (error) {
        LOG(ERROR) << "Exiting updater with code: " << error;
        return error;
      }

      // Ensure image(s) exist, setup for upgrade, if requested
      error = CheckForImage(composite_device, kLogiRallyName,
                            FLAGS_image_version, FLAGS_force);
      if (error || FLAGS_image_version)
        return error;  // Done here. Rest is N/A

      if (FLAGS_update_components || FLAGS_update_dfu_audio) {
        if (composite_device->IsDevicePresent(kLogiDeviceTableHub))
          error = composite_device->PerformSystemUpdate();
      }
      LOG(INFO) << "Exiting updater with code: " << error;
      break;

    case kLogiDeviceTypeTapSensor:
    case kLogiDeviceTypeTapHDMI:
      // Tap device.
      composite_device = std::make_shared<CompositeDevice>();
      composite_device->AddDevice(kLogiDeviceHdmi, kLogiTapHdmiPid);

      CheckDeviceVersion(composite_device);
      if (FLAGS_device_version) {
        LOG(INFO) << "Exiting updater with code: " << kLogiErrorNoError;
        return kLogiErrorNoError;  // Done here. Rest is N/A
      }

      // Images must exist for subsequent operations
      error = ApplyTapFirmware(composite_device, FLAGS_test_sig);
      if (error) {
        LOG(ERROR) << "Exiting updater with code: " << error;
        return error;
      }

      // Ensure image(s) exist, setup for upgrade, if requested
      error = CheckForImage(composite_device, kLogiTapName, FLAGS_image_version,
                            FLAGS_force);
      if (error || FLAGS_image_version)
        return error;  // Done here. Rest is N/A

      if (FLAGS_update_components || FLAGS_update_dfu_audio) {
        error = composite_device->PerformComponentUpdate(&did_update);
        if (did_update)
          composite_device->PowerCycleTDEMode(kLogiTapMcuPid);
      }
      break;
    default:
      LOG(ERROR) << "Unsupported device type:  " << FLAGS_id;
      break;
  }
  return error;
}
