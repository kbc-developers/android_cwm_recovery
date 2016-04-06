/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <chrono>

#include <base/file.h>
#include <base/stringprintf.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "ui.h"
#include "screen_ui.h"
#include "device.h"

#include "voldclient.h"

#include "adb_install.h"
#include "adb.h"
#include "fuse_sideload.h"
#include "fuse_sdcard_provider.h"

extern "C" {
#include "recovery_cmds.h"
}

struct selabel_handle *sehandle;

#ifdef HAVE_OEMLOCK

/*
 * liboemlock must supply the following C symbols:
 *
 *   - int oemlock_get()
 *
 *     Returns the current state of the OEM lock, if available.
 *       -1: Not available and/or error
 *        0: Unlocked
 *        1: Locked
 *
 *  - int oemlock_set(int lock)
 *
 *      Sets the state of the OEM lock.  The "lock" parameter will be set
 *      to 0 for unlock and 1 for lock.
 *
 *      Returns 0 on success, -1 on error
 */
extern "C" {
int oemlock_get();
int oemlock_set(int lock);
}

enum OemLockOp {
    OEM_LOCK_NONE,
    OEM_LOCK_UNLOCK
};

static OemLockOp oem_lock = OEM_LOCK_NONE;

#endif

static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 'i' },
  { "update_package", required_argument, NULL, 'u' },
  { "headless", no_argument, NULL, 'h' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "wipe_media", no_argument, NULL, 'm' },
  { "show_text", no_argument, NULL, 't' },
  { "sideload", no_argument, NULL, 's' },
  { "sideload_auto_reboot", no_argument, NULL, 'a' },
  { "just_exit", no_argument, NULL, 'x' },
  { "locale", required_argument, NULL, 'l' },
  { "stages", required_argument, NULL, 'g' },
  { "shutdown_after", no_argument, NULL, 'p' },
  { "reason", required_argument, NULL, 'r' },
  { NULL, 0, NULL, 0 },
};

static const char *CACHE_LOG_DIR = "/cache/recovery";
static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *LOCALE_FILE = "/cache/recovery/last_locale";
static const char *CACHE_ROOT = "/cache";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *LAST_KMSG_FILE = "/cache/recovery/last_kmsg";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const int KEEP_LOG_COUNT = 10;

RecoveryUI* ui = NULL;
char* locale = NULL;
char* stage = NULL;
char* reason = NULL;
bool modified_flash = false;

#include "mtdutils/mounts.h"

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *   --just_exit - do nothing; exit and reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

// open a given path, mounting partitions as necessary
FILE* fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1, sehandle);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

bool is_ro_debuggable() {
    char value[PROPERTY_VALUE_MAX+1];
    return (property_get("ro.debuggable", value, NULL) == 1 && value[0] == '1');
}

static void redirect_stdio(const char* filename) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOGE("pipe failed: %s\n", strerror(errno));

        // Fall back to traditional logging mode without timestamps.
        // If these fail, there's not really anywhere to complain...
        freopen(filename, "a", stdout); setbuf(stdout, NULL);
        freopen(filename, "a", stderr); setbuf(stderr, NULL);

        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOGE("fork failed: %s\n", strerror(errno));

        // Fall back to traditional logging mode without timestamps.
        // If these fail, there's not really anywhere to complain...
        freopen(filename, "a", stdout); setbuf(stdout, NULL);
        freopen(filename, "a", stderr); setbuf(stderr, NULL);

        return;
    }

    if (pid == 0) {
        /// Close the unused write end.
        close(pipefd[1]);

        auto start = std::chrono::steady_clock::now();

        // Child logger to actually write to the log file.
        FILE* log_fp = fopen(filename, "a");
        if (log_fp == nullptr) {
            LOGE("fopen \"%s\" failed: %s\n", filename, strerror(errno));
            close(pipefd[0]);
            _exit(1);
        }

        FILE* pipe_fp = fdopen(pipefd[0], "r");
        if (pipe_fp == nullptr) {
            LOGE("fdopen failed: %s\n", strerror(errno));
            check_and_fclose(log_fp, filename);
            close(pipefd[0]);
            _exit(1);
        }

        char* line = nullptr;
        size_t len = 0;
        while (getline(&line, &len, pipe_fp) != -1) {
            auto now = std::chrono::steady_clock::now();
            double duration = std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - start).count();
            if (line[0] == '\n') {
                fprintf(log_fp, "[%12.6lf]\n", duration);
            } else {
                fprintf(log_fp, "[%12.6lf] %s", duration, line);
            }
            fflush(log_fp);
        }

        LOGE("getline failed: %s\n", strerror(errno));

        free(line);
        check_and_fclose(log_fp, filename);
        close(pipefd[0]);
        _exit(1);
    } else {
        // Redirect stdout/stderr to the logger process.
        // Close the unused read end.
        close(pipefd[0]);

        setbuf(stdout, nullptr);
        setbuf(stderr, nullptr);

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            LOGE("dup2 stdout failed: %s\n", strerror(errno));
        }
        if (dup2(pipefd[1], STDERR_FILENO) == -1) {
            LOGE("dup2 stderr failed: %s\n", strerror(errno));
        }

        close(pipefd[1]);
    }
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure
    stage = strndup(boot.stage, sizeof(boot.stage));

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", (int)sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", (int)sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                // Arguments that may only be passed in BCB
#ifdef HAVE_OEMLOCK
                if (strcmp(arg, "--oemunlock") == 0) {
                    oem_lock = OEM_LOCK_UNLOCK;
                    --*argc;
                    continue;
                }
#endif
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *token;
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                token = strtok(buf, "\r\n");
                if (token != NULL) {
                    (*argv)[*argc] = strdup(token);  // Strip newline.
                } else {
                    --*argc;
                }
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    set_bootloader_message(&boot);
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

// Read from kernel log into buffer and write out to file.
static void save_kernel_log(const char* destination) {
    int klog_buf_len = klogctl(KLOG_SIZE_BUFFER, 0, 0);
    if (klog_buf_len <= 0) {
        LOGE("Error getting klog size: %s\n", strerror(errno));
        return;
    }

    std::string buffer(klog_buf_len, 0);
    int n = klogctl(KLOG_READ_ALL, &buffer[0], klog_buf_len);
    if (n == -1) {
        LOGE("Error in reading klog: %s\n", strerror(errno));
        return;
    }
    buffer.resize(n);
    android::base::WriteStringToFile(buffer, destination);
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void copy_log_file(const char* source, const char* destination, bool append) {
    FILE* dest_fp = fopen_path(destination, append ? "a" : "w");
    if (dest_fp == nullptr) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE* source_fp = fopen(source, "r");
        if (source_fp != nullptr) {
            if (append) {
                fseek(source_fp, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            size_t bytes;
            while ((bytes = fread(buf, 1, sizeof(buf), source_fp)) != 0) {
                fwrite(buf, 1, bytes, dest_fp);
            }
            if (append) {
                tmplog_offset = ftell(source_fp);
            }
            check_and_fclose(source_fp, source);
        }
        check_and_fclose(dest_fp, destination);
    }
}

// Rename last_log -> last_log.1 -> last_log.2 -> ... -> last_log.$max.
// Similarly rename last_kmsg -> last_kmsg.1 -> ... -> last_kmsg.$max.
// Overwrite any existing last_log.$max and last_kmsg.$max.
static void rotate_logs(int max) {
    // Logs should only be rotated once.
    static bool rotated = false;
    if (rotated) {
        return;
    }
    rotated = true;
    ensure_path_mounted(LAST_LOG_FILE);
    ensure_path_mounted(LAST_KMSG_FILE);

    for (int i = max-1; i >= 0; --i) {
        std::string old_log = android::base::StringPrintf("%s", LAST_LOG_FILE);
        if (i > 0) {
          old_log += "." + std::to_string(i);
        }
        std::string new_log = android::base::StringPrintf("%s.%d", LAST_LOG_FILE, i+1);
        // Ignore errors if old_log doesn't exist.
        rename(old_log.c_str(), new_log.c_str());

        std::string old_kmsg = android::base::StringPrintf("%s", LAST_KMSG_FILE);
        if (i > 0) {
          old_kmsg += "." + std::to_string(i);
        }
        std::string new_kmsg = android::base::StringPrintf("%s.%d", LAST_KMSG_FILE, i+1);
        rename(old_kmsg.c_str(), new_kmsg.c_str());
    }
}

static void copy_logs() {
    // We only rotate and record the log of the current session if there are
    // actual attempts to modify the flash, such as wipes, installs from BCB
    // or menu selections. This is to avoid unnecessary rotation (and
    // possible deletion) of log files, if it does not do anything loggable.
    if (!modified_flash) {
        return;
    }

    rotate_logs(KEEP_LOG_COUNT);

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    save_kernel_log(LAST_KMSG_FILE);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_KMSG_FILE, 0600);
    chown(LAST_KMSG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);
    sync();
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Save the locale to cache, so if recovery is next started up
    // without a --locale argument (eg, directly from the bootloader)
    // it will use the last-known locale.
    if (locale != NULL) {
        LOGI("Saving locale \"%s\"\n", locale);
        FILE* fp = fopen_path(LOCALE_FILE, "w");
        fwrite(locale, 1, strlen(locale), fp);
        fflush(fp);
        fsync(fileno(fp));
        check_and_fclose(fp, LOCALE_FILE);
    }

    copy_logs();

    // Reset to normal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    ensure_path_unmounted(CACHE_ROOT);
    sync();  // For good measure.
}

typedef struct _saved_log_file {
    char* name;
    struct stat st;
    unsigned char* data;
    struct _saved_log_file* next;
} saved_log_file;

static bool erase_volume(const char* volume, bool force = false) {
    bool is_cache = (strcmp(volume, CACHE_ROOT) == 0);

    saved_log_file* head = NULL;

    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);

    if (!force && is_cache) {
        // If we're reformatting /cache, we load any past logs
        // (i.e. "/cache/recovery/last_*") and the current log
        // ("/cache/recovery/log") into memory, so we can restore them after
        // the reformat.

        ensure_path_mounted(volume);

        DIR* d;
        struct dirent* de;
        d = opendir(CACHE_LOG_DIR);
        if (d) {
            char path[PATH_MAX];
            strcpy(path, CACHE_LOG_DIR);
            strcat(path, "/");
            int path_len = strlen(path);
            while ((de = readdir(d)) != NULL) {
                if (strncmp(de->d_name, "last_", 5) == 0 || strcmp(de->d_name, "log") == 0) {
                    saved_log_file* p = (saved_log_file*) malloc(sizeof(saved_log_file));
                    strcpy(path+path_len, de->d_name);
                    p->name = strdup(path);
                    if (stat(path, &(p->st)) == 0) {
                        // truncate files to 512kb
                        if (p->st.st_size > (1 << 19)) {
                            p->st.st_size = 1 << 19;
                        }
                        p->data = (unsigned char*) malloc(p->st.st_size);
                        FILE* f = fopen(path, "rb");
                        fread(p->data, 1, p->st.st_size, f);
                        fclose(f);
                        p->next = head;
                        head = p;
                    } else {
                        free(p);
                    }
                }
            }
            closedir(d);
        } else {
            if (errno != ENOENT) {
                printf("opendir failed: %s\n", strerror(errno));
            }
        }
    }

    ui->Print("Formatting %s...\n", volume);

    if (volume[0] == '/') {
        ensure_path_unmounted(volume);
    }
    int result = format_volume(volume, force);

    if (!force && is_cache) {
        while (head) {
            FILE* f = fopen_path(head->name, "wb");
            if (f) {
                fwrite(head->data, 1, head->st.st_size, f);
                fclose(f);
                chmod(head->name, head->st.st_mode);
                chown(head->name, head->st.st_uid, head->st.st_gid);
            }
            free(head->name);
            free(head->data);
            saved_log_file* temp = head->next;
            free(head);
            head = temp;
        }

        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
        copy_logs();
    }

    ui->SetBackground(RecoveryUI::NONE);
    ui->SetProgressType(RecoveryUI::EMPTY);

    return (result == 0);
}

int
get_menu_selection(const char* const * headers, const char* const * items,
                   int menu_only, int initial_selection, Device* device) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui->FlushKeys();

    // Count items to detect valid values for absolute selection
    int header_count = 0;
    if (headers) {
        while (headers[header_count] != NULL)
            ++header_count;
    }
    int item_count = 0;
    while (items[item_count] != NULL)
        ++item_count;

    ui->StartMenu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;

    while (chosen_item < 0 &&
            chosen_item != Device::kGoBack &&
            chosen_item != Device::kGoHome &&
            chosen_item != Device::kRefresh) {
        int key = ui->WaitKey();
        int visible = ui->IsTextVisible();

        if (key == -1) {   // ui_wait_key() timed out
            if (ui->WasTextEverVisible()) {
                continue;
            } else {
                LOGI("timed out waiting for key input; rebooting.\n");
                ui->EndMenu();
                return 0; // XXX fixme
            }
        } else if (key == -2) { // we are returning from ui_cancel_wait_key(): no action
            return Device::kNoAction;
        }
        else if (key == -6) {
            return Device::kRefresh;
        }

        int action = device->HandleMenuKey(key, visible);

        if (action >= 0) {
            action &= ~KEY_FLAG_ABS;
            if (action < header_count || action >= header_count + item_count) {
                action = Device::kNoAction;
            }
            else {
                // Absolute selection.  Update selected item and give some
                // feedback in the UI by selecting the item for a short time.
                selected = ui->SelectMenu(action, true);
                action = Device::kInvokeItem;
                usleep(50*1000);
            }
        }

        if (action < 0) {
            switch (action) {
                case Device::kHighlightUp:
                    selected = ui->SelectMenu(--selected);
                    break;
                case Device::kHighlightDown:
                    selected = ui->SelectMenu(++selected);
                    break;
                case Device::kInvokeItem:
                    chosen_item = selected;
                    break;
                case Device::kNoAction:
                    break;
                case Device::kGoBack:
                    chosen_item = Device::kGoBack;
                    break;
                case Device::kGoHome:
                    chosen_item = Device::kGoHome;
                    break;
                case Device::kRefresh:
                    chosen_item = Device::kRefresh;
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui->EndMenu();
    if (chosen_item == Device::kGoHome) {
        device->GoHome();
    }
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

// Returns a malloc'd path, or NULL.
static char* browse_directory(const char* path, Device* device) {
    DIR* d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        return NULL;
    }

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = (char**)malloc(d_alloc * sizeof(char*));
    int z_size = 1;
    int z_alloc = 10;
    char** zips = (char**)malloc(z_alloc * sizeof(char*));
    zips[0] = strdup("../");

    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char*));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char*));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    const char* headers[] = { path, NULL };

    char* result;
    int chosen_item = 0;
    while (true) {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item, device);
        if (chosen_item == Device::kGoHome) {
            // go up and stop browsing
            result = strdup("");
            break;
        }
        if (chosen_item == 0 || chosen_item == Device::kGoBack) {
            // go up but continue browsing (if the caller is update_directory)
            result = NULL;
            break;
        }

        char* item = zips[chosen_item];
        int item_len = strlen(item);

        char new_path[PATH_MAX];
        strlcpy(new_path, path, PATH_MAX);
        strlcat(new_path, "/", PATH_MAX);
        strlcat(new_path, item, PATH_MAX);

        if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = browse_directory(new_path, device);
            if (result) break;
        } else {
            // selected a zip file: return the malloc'd path to the caller.
            result = strdup(new_path);
            break;
        }
    }

    for (int i = 0; i < z_size; ++i) free(zips[i]);
    free(zips);

    return result;
}

static bool yes_no(Device* device, const char* question1, const char* question2) {
    const char* headers[] = { question1, question2, NULL };
    const char* items[] = { " No", " Yes", NULL };

    int chosen_item = get_menu_selection(headers, items, 1, 0, device);
    return (chosen_item == 1);
}

// Return true on success.
static bool wipe_data(int should_confirm, Device* device, bool force = false) {
    if (should_confirm && !yes_no(device, "Wipe all user data?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping data...\n");
    bool success;
retry:
    success =
        device->PreWipeData() &&
        erase_volume("/data", force) &&
        erase_volume("/cache") &&
        device->PostWipeData();
    if (!success && !force) {
        if (!should_confirm || yes_no(device, "Wipe failed, format instead?", "  THIS CAN NOT BE UNDONE!")) {
            force = true;
            goto retry;
        }
    }
    ui->Print("Data wipe %s.\n", success ? "complete" : "failed");
    return success;
}

static bool wipe_media(int should_confirm, Device* device) {
    if (should_confirm && !yes_no(device, "Wipe all user media?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping media...\n");
    bool success =
        device->PreWipeMedia() &&
        erase_volume("media") &&
        device->PostWipeMedia();
    ui->Print("Media wipe %s.\n", success ? "complete" : "failed");
    return success;
}

// Return true on success.
static bool wipe_cache(bool should_confirm, Device* device) {
    if (should_confirm && !yes_no(device, "Wipe cache?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping cache...\n");
    bool success = erase_volume("/cache");
    ui->Print("Cache wipe %s.\n", success ? "complete" : "failed");
    return success;
}

// Return true on success.
static bool wipe_system(Device* device) {
    if (!yes_no(device, "Wipe system?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping system...\n");
    bool success = erase_volume("/system");
    ui->Print("System wipe %s.\n", success ? "complete" : "failed");
    return success;
}

static void choose_recovery_file(Device* device) {
    // "Back" + KEEP_LOG_COUNT * 2 + terminating nullptr entry
    char* entries[1 + KEEP_LOG_COUNT * 2 + 1];
    memset(entries, 0, sizeof(entries));

    unsigned int n = 0;

    // Add LAST_LOG_FILE + LAST_LOG_FILE.x
    // Add LAST_KMSG_FILE + LAST_KMSG_FILE.x
    for (int i = 0; i < KEEP_LOG_COUNT; i++) {
        char* log_file;
        int ret;
        ret = (i == 0) ? asprintf(&log_file, "%s", LAST_LOG_FILE) :
                asprintf(&log_file, "%s.%d", LAST_LOG_FILE, i);
        if (ret == -1) {
            // memory allocation failure - return early. Should never happen.
            return;
        }
        if ((ensure_path_mounted(log_file) != 0) || (access(log_file, R_OK) == -1)) {
            free(log_file);
        } else {
            entries[n++] = log_file;
        }

        char* kmsg_file;
        ret = (i == 0) ? asprintf(&kmsg_file, "%s", LAST_KMSG_FILE) :
                asprintf(&kmsg_file, "%s.%d", LAST_KMSG_FILE, i);
        if (ret == -1) {
            // memory allocation failure - return early. Should never happen.
            return;
        }
        if ((ensure_path_mounted(kmsg_file) != 0) || (access(kmsg_file, R_OK) == -1)) {
            free(kmsg_file);
        } else {
            entries[n++] = kmsg_file;
        }
    }

    entries[n++] = strdup("Back");

    const char* headers[] = { "Select file to view", nullptr };

    while (true) {
        int chosen_item = get_menu_selection(headers, entries, 1, 0, device);
        if (chosen_item == Device::kGoHome) break;
        if (chosen_item == Device::kGoBack) break;
        if (chosen_item >= 0 && strcmp(entries[chosen_item], "Back") == 0) break;

        ui->ShowFile(entries[chosen_item]);
    }

    for (size_t i = 0; i < (sizeof(entries) / sizeof(*entries)); i++) {
        free(entries[i]);
    }
}

static int apply_from_storage(Device* device, const std::string& id, bool* wipe_cache) {
    modified_flash = true;

    int status;

    if (!vdc->volumeMount(id)) {
        return INSTALL_ERROR;
    }

    VolumeInfo vi = vdc->getVolume(id);

    char* path = browse_directory(vi.mInternalPath.c_str(), device);
    if (path == NULL || *path == '\0') {
        ui->Print("\n-- No package file selected.\n");
        vdc->volumeUnmount(vi.mId);
        free(path);
        return INSTALL_NONE;
    }

    ui->ClearText();
    ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);

    ui->Print("\n-- Install %s ...\n", path);
    set_sdcard_update_bootloader_message();
    void* token = start_sdcard_fuse(path);

    vdc->volumeUnmount(vi.mId, true);

    status = install_package(FUSE_SIDELOAD_HOST_PATHNAME, wipe_cache,
                                 TEMPORARY_INSTALL_FILE, false);

    finish_sdcard_fuse(token);
    free(path);
    return status;
}

static int
show_apply_update_menu(Device* device) {
    static const char* headers[] = { "Apply update", NULL };
    char* menu_items[MAX_NUM_MANAGED_VOLUMES + 1 + 1];
    std::vector<VolumeInfo> volumes = vdc->getVolumes();

    const int item_sideload = 0;
    int n, i;
    std::vector<VolumeInfo>::iterator vitr;

refresh:
    menu_items[item_sideload] = strdup("Apply from ADB");

    n = item_sideload + 1;
    for (vitr = volumes.begin(); vitr != volumes.end(); ++vitr) {
        menu_items[n] = (char*)malloc(256);
        sprintf(menu_items[n], "Choose from %s", vitr->mLabel.c_str());
        ++n;
    }
    menu_items[n] = NULL;

    bool wipe_cache;
    int status = INSTALL_ERROR;

    int chosen = get_menu_selection(headers, menu_items, 0, 0, device);
    for (i = 0; i < n; ++i) {
        free(menu_items[i]);
    }
    if (chosen == Device::kRefresh) {
        goto refresh;
    }
    if (chosen == Device::kGoHome) {
        return INSTALL_NONE;
    }
    if (chosen == Device::kGoBack) {
        return INSTALL_NONE;
    }
    if (chosen == item_sideload) {
        static const char* headers[] = {  "ADB Sideload",
                                    NULL
        };
        static const char* list[] = { "Cancel sideload", NULL };

        start_sideload(ui, &wipe_cache, TEMPORARY_INSTALL_FILE);
        int item = get_menu_selection(headers, list, 0, 0, device);
        if (item != Device::kNoAction) {
            stop_sideload();
        }
        status = wait_sideload();
    }
    else {
        std::string id = volumes[chosen - 1].mId;
        status = apply_from_storage(device, id, &wipe_cache);
    }

    if (status != INSTALL_SUCCESS && status != INSTALL_NONE) {
        ui->DialogShowErrorLog("Install failed");
    }

    return status;
}

// Return REBOOT, SHUTDOWN, or REBOOT_BOOTLOADER.  Returning NO_ACTION
// means to take the default, which is to reboot or shutdown depending
// on if the --shutdown_after flag was passed to recovery.
static Device::BuiltinAction
prompt_and_wait(Device* device, int status) {
    for (;;) {
        finish_recovery(NULL);
        switch (status) {
            case INSTALL_SUCCESS:
            case INSTALL_NONE:
                ui->SetBackground(RecoveryUI::NONE);
                break;

            case INSTALL_ERROR:
            case INSTALL_CORRUPT:
                ui->SetBackground(RecoveryUI::D_ERROR);
                break;
        }
        ui->SetProgressType(RecoveryUI::EMPTY);

        int chosen_item = get_menu_selection(nullptr, device->GetMenuItems(), 0, 0, device);

        // device-specific code may take some action here.  It may
        // return one of the core actions handled in the switch
        // statement below.
        Device::BuiltinAction chosen_action = device->InvokeMenuItem(chosen_item);

        bool should_wipe_cache = false;
        for (;;) {
            switch (chosen_action) {
                case Device::NO_ACTION:
                    break;

                case Device::REBOOT:
                case Device::SHUTDOWN:
                case Device::REBOOT_RECOVERY:
                case Device::REBOOT_BOOTLOADER:
                    return chosen_action;

                case Device::WIPE_DATA:
                    wipe_data(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return Device::NO_ACTION;
                    break;

                case Device::WIPE_FULL:
                    wipe_data(ui->IsTextVisible(), device, true);
                    if (!ui->IsTextVisible()) return Device::NO_ACTION;
                    break;

                case Device::WIPE_CACHE:
                    wipe_cache(ui->IsTextVisible(), device);
                    if (!ui->IsTextVisible()) return Device::NO_ACTION;
                    break;

                case Device::APPLY_UPDATE:
                    {
                        status = show_apply_update_menu(device);

                        if (status == INSTALL_SUCCESS && should_wipe_cache) {
                            if (!wipe_cache(false, device)) {
                                status = INSTALL_ERROR;
                            }
                        }

                        if (status >= 0 && status != INSTALL_NONE) {
                            if (status != INSTALL_SUCCESS) {
                                ui->SetBackground(RecoveryUI::D_ERROR);
                                ui->Print("Installation aborted.\n");
                                copy_logs();
                            } else if (!ui->IsTextVisible()) {
                                return Device::NO_ACTION;  // reboot if logs aren't visible
                            } else {
                                ui->Print("\nInstall complete.\n");
                        }
                    }
                    break;
                }
                    break;

                case Device::VIEW_RECOVERY_LOGS:
                    choose_recovery_file(device);
                    break;

                case Device::MOUNT_SYSTEM:
                    char system_root_image[PROPERTY_VALUE_MAX];
                    property_get("ro.build.system_root_image", system_root_image, "");

                    // For a system image built with the root directory (i.e.
                    // system_root_image == "true"), we mount it to /system_root, and symlink /system
                    // to /system_root/system to make adb shell work (the symlink is created through
                    // the build system).
                    // Bug: 22855115
                    if (strcmp(system_root_image, "true") == 0) {
                        if (ensure_path_mounted_at("/", "/system_root") != -1) {
                            ui->Print("Mounted /system.\n");
                        }
                    } else {
                        if (ensure_path_mounted("/system") != -1) {
                            ui->Print("Mounted /system.\n");
                        }
                    }

                    break;

                case Device::WIPE_SYSTEM:
                    wipe_system(device);
                    break;
            }
            if (status == Device::kRefresh) {
                status = 0;
                continue;
            }
            break;
        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}

static void
load_locale_from_cache() {
    FILE* fp = fopen_path(LOCALE_FILE, "r");
    char buffer[80];
    if (fp != NULL) {
        fgets(buffer, sizeof(buffer), fp);
        int j = 0;
        unsigned int i;
        for (i = 0; i < sizeof(buffer) && buffer[i]; ++i) {
            if (!isspace(buffer[i])) {
                buffer[j++] = buffer[i];
            }
        }
        buffer[j] = 0;
        locale = strdup(buffer);
        check_and_fclose(fp, LOCALE_FILE);
    }
}

static const char *key_src = "/data/misc/adb/adb_keys";
static const char *key_dest = "/adb_keys";


static void
setup_adbd() {
    struct stat f;
    // Mount /data and copy adb_keys to root if it exists
    ensure_path_mounted("/data");
    if (stat(key_src, &f) == 0) {
        FILE *file_src = fopen(key_src, "r");
        if (file_src == NULL) {
            LOGE("Can't open %s\n", key_src);
        } else {
            FILE *file_dest = fopen(key_dest, "w");
            if (file_dest == NULL) {
                LOGE("Can't open %s\n", key_dest);
            } else {
                char buf[4096];
                while (fgets(buf, sizeof(buf), file_src)) fputs(buf, file_dest);
                check_and_fclose(file_dest, key_dest);

                // Enable secure adbd
                property_set("ro.adb.secure", "1");
            }
            check_and_fclose(file_src, key_src);
        }
    }
    ensure_path_unmounted("/data");

    // Trigger (re)start of adb daemon
    property_set("service.adb.root", "1");
}

static RecoveryUI* gCurrentUI = NULL;

void
ui_print(const char* format, ...) {
    char buffer[256];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    if (gCurrentUI != NULL) {
        gCurrentUI->Print("%s", buffer);
    } else {
        fputs(buffer, stdout);
    }
}

extern "C" int toybox_driver(int argc, char **argv);

static int write_file(const char *path, const char *value)
{
    int fd, ret, len;

    fd = open(path, O_WRONLY|O_CREAT, 0622);
    if (fd < 0)
        return -errno;

    len = strlen(value);

    do {
        ret = write(fd, value, len);
    } while (ret < 0 && errno == EINTR);

    close(fd);
    if (ret < 0) {
        return -errno;
    } else {
        return 0;
    }
}

int
main(int argc, char **argv) {
    // If this binary is started with the single argument "--adbd",
    // instead of being the normal recovery binary, it turns into kind
    // of a stripped-down version of adbd that only supports the
    // 'sideload' command.  Note this must be a real argument, not
    // anything in the command file or bootloader control block; the
    // only way recovery should be run with this argument is when it
    // starts a copy of itself from the apply_from_adb() function.
    if (argc == 2 && strcmp(argv[1], "--adbd") == 0) {
        adb_main(0, DEFAULT_ADB_PORT);
        return 0;
    }

    // Handle alternative invocations
    char* command = argv[0];
    char* stripped = strrchr(argv[0], '/');
    if (stripped)
        command = stripped + 1;

    if (strcmp(command, "recovery") != 0) {
        struct recovery_cmd cmd = get_command(command);
        if (cmd.name)
            return cmd.main_func(argc, argv);

        if (!strcmp(command, "setup_adbd")) {
            load_volume_table();
            setup_adbd();
            return 0;
        }
        if (strstr(argv[0], "start")) {
            property_set("ctl.start", argv[1]);
            return 0;
        }
        if (strstr(argv[0], "stop")) {
            property_set("ctl.stop", argv[1]);
            return 0;
        }
        return toybox_driver(argc, argv);
    }

    // Clear umask for packages that copy files out to /tmp and then over
    // to /system without properly setting all permissions (eg. gapps).
    umask(0);

    time_t start = time(NULL);

    // redirect_stdio should be called only in non-sideload mode. Otherwise
    // we may have two logger instances with different timestamps.
    redirect_stdio(TEMPORARY_LOG_FILE);

    printf("Starting recovery (pid %d) on %s", getpid(), ctime(&start));

    load_volume_table();
    get_args(&argc, &argv);

    const char *send_intent = NULL;
    const char *update_package = NULL;
    bool should_wipe_data = false;
    bool should_wipe_cache = false;
    bool should_wipe_media = false;
    bool show_text = false;
    bool sideload = false;
    bool sideload_auto_reboot = false;
    bool headless = false;
    bool just_exit = false;
    bool shutdown_after = false;

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'i': send_intent = optarg; break;
        case 'u': update_package = optarg; break;
        case 'h': headless = true; break;
        case 'w': should_wipe_data = true; break;
        case 'c': should_wipe_cache = true; break;
        case 'm': should_wipe_media = true; break;
        case 't': show_text = true; break;
        case 's': sideload = true; break;
        case 'a': sideload = true; sideload_auto_reboot = true; break;
        case 'x': just_exit = true; break;
        case 'l': locale = optarg; break;
        case 'g': {
            if (stage == NULL || *stage == '\0') {
                char buffer[20] = "1/";
                strncat(buffer, optarg, sizeof(buffer)-3);
                stage = strdup(buffer);
            }
            break;
        }
        case 'p': shutdown_after = true; break;
        case 'r': reason = optarg; break;
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }

    if (locale == NULL) {
        load_locale_from_cache();
    }
    printf("locale is [%s]\n", locale);
    printf("stage is [%s]\n", stage);
    printf("reason is [%s]\n", reason);

    Device* device = make_device();
    ui = device->GetUI();
    gCurrentUI = ui;

    vdc = new VoldClient(device);
    vdc->start();

    ui->SetLocale(locale);
    ui->Init();

    int st_cur, st_max;
    if (stage != NULL && sscanf(stage, "%d/%d", &st_cur, &st_max) == 2) {
        ui->SetStage(st_cur, st_max);
    }

    ui->SetBackground(RecoveryUI::NONE);
    if (show_text) ui->ShowText(true);

    /*enable the backlight*/
    write_file("/sys/class/leds/lcd-backlight/brightness", "128");

    struct selinux_opt seopts[] = {
      { SELABEL_OPT_PATH, "/file_contexts" }
    };

    sehandle = selabel_open(SELABEL_CTX_FILE, seopts, 1);

    if (!sehandle) {
        ui->Print("Warning: No file_contexts\n");
    }

    device->StartRecovery();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = (char*)malloc(len);
            if (modified_path) {
                strlcpy(modified_path, "/cache/", len);
                strlcat(modified_path, update_package+6, len);
                printf("(replacing path \"%s\" with \"%s\")\n",
                       update_package, modified_path);
                update_package = modified_path;
            }
            else
                printf("modified_path allocation failed\n");
        }
    }
    printf("\n");

    property_list(print_property, NULL);
    printf("\n");

    int status = INSTALL_SUCCESS;

#ifdef HAVE_OEMLOCK
    if (oem_lock == OEM_LOCK_UNLOCK) {
        device->PreWipeData();
        if (erase_volume("/data", true)) status = INSTALL_ERROR;
        if (should_wipe_cache && erase_volume("/cache", true)) status = INSTALL_ERROR;
        device->PostWipeData();
        if (status != INSTALL_SUCCESS) ui->Print("Data wipe failed.\n");
        if (oemlock_set(0)) status = INSTALL_ERROR;
        // Force reboot regardless of actual status
        status = INSTALL_SUCCESS;
    } else
#endif
    if (update_package != NULL) {
        status = install_package(update_package, &should_wipe_cache, TEMPORARY_INSTALL_FILE, true);
        if (status == INSTALL_SUCCESS && should_wipe_cache) {
            wipe_cache(false, device);
        }
        if (status != INSTALL_SUCCESS) {
            ui->Print("Installation aborted.\n");

            // If this is an eng or userdebug build, then automatically
            // turn the text display on if the script fails so the error
            // message is visible.
            if (is_ro_debuggable()) {
                ui->ShowText(true);
            }
        }
    } else if (should_wipe_data) {
        if (!wipe_data(false, device, should_wipe_media)) {
            status = INSTALL_ERROR;
        }
    } else if (should_wipe_cache) {
        if (!wipe_cache(false, device)) {
            status = INSTALL_ERROR;
        }
    } else if (should_wipe_media) {
        if (!wipe_media(false, device)) {
            status = INSTALL_ERROR;
        }
    } else if (sideload) {
        // 'adb reboot sideload' acts the same as user presses key combinations
        // to enter the sideload mode. When 'sideload-auto-reboot' is used, text
        // display will NOT be turned on by default. And it will reboot after
        // sideload finishes even if there are errors. Unless one turns on the
        // text display during the installation. This is to enable automated
        // testing.
        if (!sideload_auto_reboot) {
            ui->ShowText(true);
        }
        start_sideload(ui, &should_wipe_cache, TEMPORARY_INSTALL_FILE);
        status = wait_sideload();
        if (status == INSTALL_SUCCESS && should_wipe_cache) {
            if (!wipe_cache(false, device)) {
                status = INSTALL_ERROR;
            }
        }
        ui->Print("\nInstall from ADB complete (status: %d).\n", status);
        if (sideload_auto_reboot) {
            ui->Print("Rebooting automatically.\n");
        }
    } else if (!just_exit) {
        status = INSTALL_NONE;  // No command specified
        ui->SetBackground(RecoveryUI::NO_COMMAND);

        // Always show menu if no command is specified
        ui->ShowText(true);
    }

    if (!sideload_auto_reboot && (status == INSTALL_ERROR || status == INSTALL_CORRUPT)) {
        copy_logs();
        ui->SetBackground(RecoveryUI::D_ERROR);
    }

    Device::BuiltinAction after = shutdown_after ? Device::SHUTDOWN : Device::REBOOT;
    if (headless) {
        ui->ShowText(true);
        ui->SetHeadlessMode();
        finish_recovery(NULL);
        for (;;) {
            pause();
        }
    }
    else if ((status != INSTALL_SUCCESS && !sideload_auto_reboot) || ui->IsTextVisible()) {
        Device::BuiltinAction temp = prompt_and_wait(device, status);
        if (temp != Device::NO_ACTION) {
            after = temp;
        }
    }

    // Save logs and clean up before rebooting or shutting down.
    finish_recovery(send_intent);

    vdc->unmountAll();
    vdc->stop();

    sync();

    write_file("/sys/class/leds/lcd-backlight/brightness", "0");
    gr_fb_blank(true);

    switch (after) {
        case Device::SHUTDOWN:
            ui->Print("Shutting down...\n");
            property_set(ANDROID_RB_PROPERTY, "shutdown,");
            break;

        case Device::REBOOT_RECOVERY:
            ui->Print("Rebooting recovery...\n");
            property_set(ANDROID_RB_PROPERTY, "reboot,recovery");
            break;

        case Device::REBOOT_BOOTLOADER:
#ifdef DOWNLOAD_MODE
            ui->Print("Rebooting to download mode...\n");
            property_set(ANDROID_RB_PROPERTY, "reboot,download");
#else
            ui->Print("Rebooting to bootloader...\n");
            property_set(ANDROID_RB_PROPERTY, "reboot,bootloader");
#endif
            break;

        default:
            char reason[PROPERTY_VALUE_MAX];
            snprintf(reason, PROPERTY_VALUE_MAX, "reboot,%s", device->GetRebootReason());
            ui->Print("Rebooting...\n");
            property_set(ANDROID_RB_PROPERTY, reason);
            break;
    }
    sleep(5); // should reboot before this finishes
    return EXIT_SUCCESS;
}
