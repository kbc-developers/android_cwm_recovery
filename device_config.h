/*
 * Copyright (C) 2011-2012 sakuramilk <c.sakuramilk@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __RECOVERY_DEVICE_CONFIG_H__
#define __RECOVERY_DEVICE_CONFIG_H__

// COMMON
#define RECOVERY_TZ_OFFSET (60 * 60 * 9); // 9 hours


// SC06D --------------------------------------------------
#ifdef TARGET_DEVICE_SC06D
// ums
#define BOARD_UMS_LUNFILE     "/sys/devices/platform/msm_hsusb/gadget/lun/file"
#define BOARD_UMS_LUNFILE0    "/sys/devices/platform/msm_hsusb/gadget/lun0/file"
#define BOARD_UMS_LUNFILE1    "/sys/devices/platform/msm_hsusb/gadget/lun1/file"

// mmcblk
#define MMCBLK_BOOT           "mmcblk0p7"
#define MMCBLK_SYSTEM         "mmcblk0p14"
#define MMCBLK_DATA           "mmcblk0p15"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOMEPAGE

// gesture
#define GESTURE_UD_SWIPE_THRED       (80)
#define GESTURE_BACK_SWIPE_THRED     (200)
#define GESTURE_TOUCH_THRED          (3)

// strings
#define DEVICE_NAME "SC06D"
#define REBOOT_BOOTLOADER_CMD "download"

// option
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

// SC02C --------------------------------------------------
#ifdef TARGET_DEVICE_SC02C
// ums
#define BOARD_UMS_LUNFILE     "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"
#define BOARD_UMS_LUNFILE0    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun0/file"
#define BOARD_UMS_LUNFILE1    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun1/file"

// mmcblk
#define MMCBLK_EFS            "mmcblk0p1"
#define MMCBLK_BOOT           "mmcblk0p5"
#define MMCBLK_SYSTEM         "mmcblk0p9"
#define MMCBLK_DATA           "mmcblk0p10"
#define MMCBLK_SDCARD         "mmcblk0p11"
#define MMCBLK_HIDDEN         "mmcblk0p12"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOME

// gesture
#define GESTURE_UD_SWIPE_THRED       (20)
#define GESTURE_BACK_SWIPE_THRED     (100)
#define GESTURE_TOUCH_THRED          (1)

// strings
#define DEVICE_NAME "SC02C"
#define REBOOT_BOOTLOADER_CMD "download"

// option
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

// SC05D --------------------------------------------------
#ifdef TARGET_DEVICE_SC05D
// ums
#define BOARD_UMS_LUNFILE     "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"
#define BOARD_UMS_LUNFILE0    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun0/file"
#define BOARD_UMS_LUNFILE1    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun1/file"

// mmcblk
#define MMCBLK_EFS            "mmcblk0p21"
#define MMCBLK_BOOT           "mmcblk0p8"
#define MMCBLK_SYSTEM         "mmcblk0p24"
#define MMCBLK_DATA           "mmcblk0p25"
#define MMCBLK_SDCARD         "mmcblk0p29"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOME

// gesture
#define GESTURE_UD_SWIPE_THRED       (80)
#define GESTURE_BACK_SWIPE_THRED     (200)
#define GESTURE_TOUCH_THRED          (3)

// strings
#define DEVICE_NAME "SC05D"
#define REBOOT_BOOTLOADER_CMD "download"

// option
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

// SC03D --------------------------------------------------
#ifdef TARGET_DEVICE_SC03D
// ums
#define BOARD_UMS_LUNFILE     "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"
#define BOARD_UMS_LUNFILE0    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun0/file"
#define BOARD_UMS_LUNFILE1    "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun1/file"

// mmcblk
#define MMCBLK_EFS            "mmcblk0p21"
#define MMCBLK_BOOT           "mmcblk0p8"
#define MMCBLK_SYSTEM         "mmcblk0p24"
#define MMCBLK_DATA           "mmcblk0p25"
#define MMCBLK_SDCARD         "mmcblk0p29"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOME

// gesture
#define GESTURE_UD_SWIPE_THRED       (80)
#define GESTURE_BACK_SWIPE_THRED     (200)
#define GESTURE_TOUCH_THRED          (3)

// strings
#define DEVICE_NAME "SC03D"
#define REBOOT_BOOTLOADER_CMD "download"

// option
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

// SO03C --------------------------------------------------
#ifdef TARGET_DEVICE_SO03C
// ums
#define BOARD_UMS_LUNFILE     "/sys/devices/virtual/android_usb/android0/f_mass_storage/lun/file"

// mmcblk
#define MMCBLK_BOOT           "mtdblock4"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOME

// gesture
#define GESTURE_UD_SWIPE_THRED       (30)
#define GESTURE_BACK_SWIPE_THRED     (100)
#define GESTURE_TOUCH_THRED          (3)

// strings
#define DEVICE_NAME "SO03C"
#define REBOOT_BOOTLOADER_CMD "bootloader"

// option
#define RECOVERY_USE_POST_RECOVERY_BOOT
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

// ISW13HT ------------------------------------------------
#ifdef TARGET_DEVICE_ISW13HT
// ums
#define BOARD_UMS_LUNFILE0     "/sys/devices/platform/msm_hsusb/gadget/lun0/file"
#define BOARD_UMS_LUNFILE1     "/sys/devices/platform/msm_hsusb/gadget/lun1/file"
#define BOARD_UMS_LUNFILE2     "/sys/devices/platform/msm_hsusb/gadget/lun2/file"

// mmcblk
#define MMCBLK_BOOT           "mmcblk0p21"
#define MMCBLK_SYSTEM         "mmcblk0p36"
#define MMCBLK_DATA           "mmcblk0p38"

// path
#define UPDATER_BIN_PATH      "/sbin/updater"

// key
#define DEVICE_KEY_HOME       KEY_HOME

// gesture
#define GESTURE_UD_SWIPE_THRED       (70)
#define GESTURE_BACK_SWIPE_THRED     (200)
#define GESTURE_TOUCH_THRED          (3)

// strings
#define DEVICE_NAME "ISW13HT"
#define REBOOT_BOOTLOADER_CMD "bootloader"

// option
#define BOARD_HAS_SDCARD_EXTERNAL

#endif
// --------------------------------------------------------

#endif /* __RECOVERY_DEVICE_CONFIG_H__ */
