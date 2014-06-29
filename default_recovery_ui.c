/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <linux/input.h>

#include "common.h"
#include "extendedcommands.h"
#include "recovery_ui.h"

char* MENU_HEADERS[] = { NULL };

char* MENU_ITEMS[] = { "reboot system now",
                       "install zip",
                       "wipe data/factory reset",
                       "wipe cache partition",
                       "backup and restore",
                       "mounts and storage",
                       "advanced",
                       NULL };

void device_ui_init(UIParameters* ui_parameters) {
}

int device_recovery_start() {
    return 0;
}

// add here any key combo check to reboot device
int device_reboot_now(volatile char* key_pressed, int key_code) {
    return 0;
}

int device_perform_action(int which) {
    return which;
}

int device_wipe_data() {
    return 0;
}

int restore_preinstall() {
#ifdef TARGET_DEVICE_SC02C
    __system("mount -t ext4 /dev/block/mmcblk0p12 /preload");
    mkdir("/data/app", 0771);
    chown("/data/app", 1000, 1000);	
    __system("cp /preload/app/* /data/app/");
    __system("chmod 644 /data/app/*");
    __system("chown system.system /data/app/*");

    __system("cp /preload/pre_video/Color_SuperAMOLEDPlus-30mb.mp4 /sdcard/");
    __system("chmod 644 /sdcard/Color_SuperAMOLEDPlus-30mb.mp4");
    __system("chown system.system /sdcard/Color_SuperAMOLEDPlus-30mb.mp4");
    __system("umount /preload");
#endif
    return 0;
}
