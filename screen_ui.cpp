/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <vector>

#include <base/strings.h>
#include <base/stringprintf.h>
#include <cutils/properties.h>

#include "common.h"
#include "device.h"
#include "minui/minui.h"
#include "screen_ui.h"
#include "ui.h"
#include "cutils/properties.h"

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI() :
    currentIcon(NONE),
    installingFrame(0),
    locale(nullptr),
    rtl_locale(false),
    progressBarType(EMPTY),
    progressScopeStart(0),
    progressScopeSize(0),
    progress(0),
    pagesIdentical(false),
    log_text_cols_(0),
    log_text_rows_(0),
    text_cols_(0),
    text_rows_(0),
    text_(nullptr),
    text_col_(0),
    text_row_(0),
    text_top_(0),
    show_text(false),
    show_text_ever(false),
    dialog_icon(NONE),
    dialog_text(nullptr),
    dialog_show_log(false),
    menu_(nullptr),
    menu_headers_(nullptr),
    header_items(0),
    show_menu(false),
    menu_items(0),
    menu_sel(0),
    menu_show_start_(0),
    sysbar_state(0),
    file_viewer_text_(nullptr),
    animation_fps(20),
    installing_frames(-1),
    stage(-1),
    max_stage(-1),
    rainbow(false),
    wrap_count(0) {

    headerIcon = nullptr;
    sysbarBackIcon = nullptr;
    sysbarBackHighlightIcon = nullptr;
    sysbarHomeIcon = nullptr;
    sysbarHomeHighlightIcon = nullptr;
    for (int i = 0; i < NR_ICONS; i++) {
        backgroundIcon[i] = nullptr;
    }
    pthread_mutex_init(&updateMutex, nullptr);
    pthread_cond_init(&progressCondition, NULL);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_background_locked(Icon icon) {
    pagesIdentical = false;
    gr_color(0, 0, 0, 255);
    gr_clear();

    if (icon) {
        GRSurface* surface = backgroundIcon[icon];
        if (icon == INSTALLING_UPDATE || icon == ERASING) {
            surface = installation[installingFrame];
        }
        GRSurface* text_surface = backgroundText[icon];

        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int textWidth = gr_get_width(text_surface);
        int textHeight = gr_get_height(text_surface);
        int stageHeight = gr_get_height(stageMarkerEmpty);
        int availableHeight = icon == INSTALLING_UPDATE && !DialogShowing() && show_text
                ? 3 * gr_fb_height() / 4 : gr_fb_height();

        int sh = (max_stage >= 0) ? stageHeight : 0;

        iconX = (gr_fb_width() - iconWidth) / 2;
        iconY = (availableHeight - (iconHeight+textHeight+40+sh)) / 2;

        int textX = (gr_fb_width() - textWidth) / 2;
        int textY = ((availableHeight - (iconHeight+textHeight+40+sh)) / 2) + iconHeight + 40;

        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
        if (stageHeight > 0) {
            int sw = gr_get_width(stageMarkerEmpty);
            int x = (gr_fb_width() - max_stage * gr_get_width(stageMarkerEmpty)) / 2;
            int y = iconY + iconHeight + 20;
            for (int i = 0; i < max_stage; ++i) {
                gr_blit((i < stage) ? stageMarkerFill : stageMarkerEmpty,
                        0, 0, sw, stageHeight, x, y);
                x += sw;
            }
        }

        LOGV("textX=%d textY=%d iconX=%d iconY=%d", textX, textY, iconX, iconY);

        gr_color(255, 255, 255, 255);
        gr_texticon(textX, textY, text_surface);
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_progress_locked() {
    if (currentIcon == D_ERROR) return;

    if (currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) {
        GRSurface* icon = installation[installingFrame];
        gr_blit(icon, 0, 0, gr_get_width(icon), gr_get_height(icon), iconX, iconY);
    }

    if (progressBarType != EMPTY) {
        int iconHeight = gr_get_height(backgroundIcon[INSTALLING_UPDATE]);
        int width = gr_get_width(progressBarEmpty);
        int height = gr_get_height(progressBarEmpty);

        int bottomOfUsableHeight = show_text ? 3 * gr_fb_height() / 4 : gr_fb_height();
        int bottomOfIcon = bottomOfUsableHeight / 2 + iconHeight / 2;

        int dx = (gr_fb_width() - width) / 2;
        int dy = bottomOfIcon + (bottomOfUsableHeight - bottomOfIcon) / 2 - height / 2;

        // Erase behind the progress bar (in case this was a progress-only update)
        gr_color(0, 0, 0, 255);
        gr_fill(dx, dy, width, height);

        if (progressBarType == DETERMINATE) {
            float p = progressScopeStart + progress * progressScopeSize;
            int pos = (int) (p * width);

            if (rtl_locale) {
                // Fill the progress bar from right to left.
                if (pos > 0) {
                    gr_blit(progressBarFill, width-pos, 0, pos, height, dx+width-pos, dy);
                }
                if (pos < width-1) {
                    gr_blit(progressBarEmpty, 0, 0, width-pos, height, dx, dy);
                }
            } else {
                // Fill the progress bar from left to right.
                if (pos > 0) {
                    gr_blit(progressBarFill, 0, 0, pos, height, dx, dy);
                }
                if (pos < width-1) {
                    gr_blit(progressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
                }
            }
        }
    }
}

void ScreenRecoveryUI::SetColor(UIElement e) {
    switch (e) {
        case INFO:
            gr_color(249, 194, 0, 255);
            break;
        case HEADER:
            gr_color(247, 0, 6, 255);
            break;
        case MENU:
        case MENU_SEL_BG:
            gr_color(106, 103, 102, 255);
            break;
        case MENU_SEL_BG_ACTIVE:
            gr_color(138, 135, 134, 255);
            break;
        case MENU_SEL_FG:
            gr_color(0, 177, 229, 255);
            break;
        case LOG:
            gr_color(196, 196, 196, 255);
            break;
        case TEXT_FILL:
            gr_color(0, 0, 0, 160);
            break;
        case ERROR_TEXT:
            gr_color(255, 0, 0, 255);
            break;
        default:
            gr_color(255, 255, 255, 255);
            break;
    }
}

void ScreenRecoveryUI::DrawHorizontalRule(int* y) {
    SetColor(MENU);
    *y += 4;
    gr_fill(0, *y, gr_fb_width(), *y + 2);
    *y += 4;
}

void ScreenRecoveryUI::DrawTextLine(int* y, const char* line, bool bold) {
    gr_text(4, *y, line, bold);
    *y += char_height_ + 4;
}

void ScreenRecoveryUI::DrawTextLines(int* y, const char* const* lines) {
    for (size_t i = 0; lines != nullptr && lines[i] != nullptr; ++i) {
        DrawTextLine(y, lines[i], false);
    }
}

static const char* REGULAR_HELP[] = {
    "Use volume up/down and power.",
    NULL
};

static const char* LONG_PRESS_HELP[] = {
    "Any button cycles highlight.",
    "Long-press activates.",
    NULL
};

int ScreenRecoveryUI::draw_header_icon()
{
    GRSurface* surface = headerIcon;
    int iw = header_width_;
    int ih = header_height_;
    int ix = (gr_fb_width() - iw) / 2;
    int iy = 0;
    gr_blit(surface, 0, 0, iw, ih, ix, iy);
    return ih;
}

void ScreenRecoveryUI::draw_menu_item(int textrow, const char *text, int selected)
{
    if (selected) {
        SetColor(MENU_SEL_BG);
        gr_fill(0, (textrow) * char_height_,
                gr_fb_width(), (textrow+3) * char_height_ - 1);
        SetColor(MENU_SEL_FG);
        gr_text(4, (textrow+1) * char_height_ - 1, text, 0);
        SetColor(MENU);
    }
    else {
        SetColor(MENU);
        gr_text(4, (textrow+1) * char_height_ - 1, text, 0);
    }
}

void ScreenRecoveryUI::draw_sysbar()
{
    GRSurface* surface;
    int sw = gr_fb_width();
    int sh = gr_fb_height();
    int iw;
    int ih;
    SetColor(TEXT_FILL);
    gr_fill(0, sh - sysbar_height_, sw, sh);

    // Left third is back button
    if (!HasBackKey()) {
        if (sysbar_state & SYSBAR_BACK) {
            surface = sysbarBackHighlightIcon;
        }
        else {
            surface = sysbarBackIcon;
        }
        iw = gr_get_width(surface);
        ih = gr_get_height(surface);
        gr_blit(surface, 0, 0, iw, ih,
                1 * (sw / 6) - (iw / 2), sh - ih);
    }

    // Middle third is home button
    if (!HasHomeKey()) {
        if (sysbar_state & SYSBAR_HOME) {
            surface = sysbarHomeHighlightIcon;
        }
        else {
            surface = sysbarHomeIcon;
        }
        iw = gr_get_width(surface);
        ih = gr_get_height(surface);
        gr_blit(surface, 0, 0, iw, ih,
                3 * (sw / 6) - (iw / 2), sh - ih);
    }
}

void ScreenRecoveryUI::draw_dialog()
{
    int x, y, w, h;

   if (dialog_icon == HEADLESS) {
       return;
    }
    draw_header_icon();
    draw_sysbar();

    int iconHeight = gr_get_height(backgroundIcon[dialog_icon]);

    x = (gr_fb_width()/2 - (char_width_ * strlen(dialog_text))/2);
    if (dialog_show_log) {
        y = gr_get_height(headerIcon) + char_height_;
    }
    else {
        y = (gr_fb_height()/2 + iconHeight/2);
    }

    SetColor(ERROR_TEXT);
    gr_text(x, y, dialog_text, 0);
    y += char_height_ + 2;

    if (dialog_show_log) {
        int cx, cy;
        gr_set_font("log");
        gr_font_size(&cx, &cy);

        size_t row;
        for (row = 0; row < log_text_rows_; ++row) {
            gr_text(2, y, text_[row], 0);
            y += cy + 2;
        }
        gr_set_font("menu");
    }

    if (dialog_icon == D_ERROR) {
        /*
         * This could be improved...
         *
         * Draw rect around text "Okay".
         * Text is centered horizontally.
         * Bottom of text is 4 lines from bottom of screen.
         * Rect width 4px
         * Rect padding 8px
         */
        w = char_width_ * 4;
        h = char_height_;
        x = gr_fb_width()/2 - w/2;
        y = gr_fb_height() - h - 4 * char_height_;
        SetColor(HEADER);
        gr_fill(x-(4+8), y-(4+8), x+w+(4+8), y+h+(4+8));
        SetColor(MENU_SEL_BG);
        gr_fill(x-8, y-8, x+w+8, y+h+8);
        SetColor(MENU_SEL_FG);
        gr_text(x, y, "Okay", 0);
    }
}

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_screen_locked() {
    draw_background_locked(currentIcon);

    if (DialogShowing()) {
        draw_dialog();
        return;
    }

    if (show_text) {
        if (currentIcon == ERASING || currentIcon == INSTALLING_UPDATE || currentIcon == VIEWING_LOG) {
            size_t y = currentIcon == INSTALLING_UPDATE ? gr_fb_height() / 4 : header_height_ + 4;

            SetColor(LOG);
            int cx, cy;
            gr_set_font("log");
            gr_font_size(&cx, &cy);
            // display from the bottom up, until we hit the top of the
            // screen or we've displayed the entire text buffer.
            size_t ty, count;
            int row = (text_first_row_ + log_text_rows_ - 1) % log_text_rows_;
            for (ty = gr_fb_height() - cy, count = 0;
                 ty > y + 2 && count < log_text_rows_;
                 ty -= (cy + 2), ++count) {
                gr_text(4, ty, text_[row], 0);
                --row;
                if (row < 0) row = log_text_rows_ - 1;
            }
            gr_set_font("menu");
            return;
        }

        if (show_menu) {
            int i, y;
            draw_header_icon();
            draw_sysbar();

            // Divider
            y = text_first_row_ * char_height_;
            SetColor(MENU_SEL_FG);
            gr_fill(0, y - 1, gr_fb_width(), y);

            if (header_items > 0) {
                for (i = 0; i < header_items; ++i) {
                    draw_menu_item(text_first_row_ + 3*i,
                            menu_headers_[i], false);
                }
                y = (text_first_row_ + 3*header_items) * char_height_;
                SetColor(MENU_SEL_FG);
                gr_fill(0, y - 1, gr_fb_width(), y);
            }
            int nr_items = menu_items - menu_show_start_;
            if (header_items + nr_items > max_menu_rows_)
                nr_items = max_menu_rows_ - header_items;
            for (i = 0; i < nr_items; ++i) {
                const char* text = menu_[menu_show_start_ + i];
                draw_menu_item(text_first_row_ + 3 * (header_items + i),
                        menu_[menu_show_start_ + i],
                        ((menu_show_start_ + i) == menu_sel));
            }
        }
    }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_screen_locked() {
    update_waiting = true;
    pthread_cond_signal(&progressCondition);
    LOGV("%s: %p\n", __func__, __builtin_return_address(0));
}

// Keeps the progress bar updated, even when the process is otherwise busy.
void* ScreenRecoveryUI::ProgressThreadStartRoutine(void* data) {
    reinterpret_cast<ScreenRecoveryUI*>(data)->ProgressThreadLoop();
    return nullptr;
}

void ScreenRecoveryUI::OMGRainbows()
{
    rainbow = rainbow ? false : true;
    set_rainbow_mode(rainbow);
    property_set("sys.rainbow.recovery", rainbow ? "1" : "0");
}

void ScreenRecoveryUI::ProgressThreadLoop() {
    double interval = 1.0 / animation_fps;
    while (true) {
        pthread_mutex_lock(&updateMutex);
        if (progressBarType == EMPTY && !update_waiting)
            pthread_cond_wait(&progressCondition, &updateMutex);

        bool redraw = false;
        double start = now();

        LOGV("loop %f show_text=%d progressBarType=%d waiting=%d\n", start, show_text, progressBarType, update_waiting );

        // update the installation animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if ((currentIcon == INSTALLING_UPDATE || currentIcon == ERASING) &&
            installing_frames > 0) {
            installingFrame = (installingFrame + 1) % installing_frames;
            redraw = true;
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = progressScopeDuration;
        if (progressBarType == DETERMINATE && duration > 0) {
            double elapsed = now() - progressScopeTime;
            float p = 1.0 * elapsed / duration;
            if (p > 1.0) p = 1.0;
            if (p > progress) {
                progress = p;
                redraw = true;
            }
        }

        if (update_waiting || !pagesIdentical) {
            LOGV("call draw_screen_locked\n");
            draw_screen_locked();
            if (!update_waiting)
                pagesIdentical = true;
        }

        if (redraw) {
            LOGV("call draw_progress_locked\n");
            draw_progress_locked();
        }
        gr_flip();

        update_waiting = false;
        pthread_mutex_unlock(&updateMutex);

        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
}

void ScreenRecoveryUI::LoadBitmap(const char* filename, GRSurface** surface) {
    int result = res_create_display_surface(filename, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadBitmapArray(const char* filename, int* frames, GRSurface*** surface) {
    int result = res_create_multi_display_surface(filename, frames, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::LoadLocalizedBitmap(const char* filename, GRSurface** surface) {
    int result = res_create_localized_alpha_surface(filename, locale, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

static char** Alloc2d(size_t rows, size_t cols) {
    char** result = new char*[rows];
    for (size_t i = 0; i < rows; ++i) {
        result[i] = new char[cols];
        memset(result[i], 0, cols);
    }
    return result;
}

void ScreenRecoveryUI::Init() {
    gr_init();

    gr_set_font("log");
    gr_font_size(&log_char_width_, &log_char_height_);
    gr_set_font("menu");
    gr_font_size(&char_width_, &char_height_);

    text_col_ = text_row_ = 0;
    text_top_ = 1;

    LoadBitmap("icon_header", &headerIcon);
    LoadBitmap("icon_sysbar_back", &sysbarBackIcon);
    LoadBitmap("icon_sysbar_back_highlight", &sysbarBackHighlightIcon);
    LoadBitmap("icon_sysbar_home", &sysbarHomeIcon);
    LoadBitmap("icon_sysbar_home_highlight", &sysbarHomeHighlightIcon);

    header_height_ = gr_get_height(headerIcon);
    header_width_ = gr_get_width(headerIcon);

    sysbar_height_ = gr_get_height(sysbarBackIcon);

    text_rows_ = (gr_fb_height() - sysbar_height_) / char_height_;
    text_cols_ = gr_fb_width() / char_width_;

    log_text_rows_ = (gr_fb_height() - sysbar_height_) / log_char_height_;
    log_text_cols_ = gr_fb_width() / log_char_width_;

    text_ = Alloc2d(log_text_rows_, log_text_cols_ + 1);
    file_viewer_text_ = Alloc2d(text_rows_, text_cols_ + 1);
    menu_ = Alloc2d(text_rows_, text_cols_ + 1);

    text_first_row_ = (header_height_ / char_height_) + 1;
    menu_item_start_ = text_first_row_ * char_height_;
    max_menu_rows_ = (text_rows_ - text_first_row_) / 3;

    backgroundIcon[NONE] = nullptr;
    LoadBitmapArray("icon_installing", &installing_frames, &installation);
    backgroundIcon[INSTALLING_UPDATE] = installing_frames ? installation[0] : nullptr;
    backgroundIcon[ERASING] = backgroundIcon[INSTALLING_UPDATE];
    LoadBitmap("icon_info", &backgroundIcon[D_INFO]);
    LoadBitmap("icon_error", &backgroundIcon[D_ERROR]);
    backgroundIcon[NO_COMMAND] = backgroundIcon[D_ERROR];
    LoadBitmap("icon_headless", &backgroundIcon[HEADLESS]);

    LoadBitmap("progress_empty", &progressBarEmpty);
    LoadBitmap("progress_fill", &progressBarFill);
    LoadBitmap("stage_empty", &stageMarkerEmpty);
    LoadBitmap("stage_fill", &stageMarkerFill);

    LoadLocalizedBitmap("installing_text", &backgroundText[INSTALLING_UPDATE]);
    LoadLocalizedBitmap("erasing_text", &backgroundText[ERASING]);
    LoadLocalizedBitmap("no_command_text", &backgroundText[NO_COMMAND]);
    LoadLocalizedBitmap("error_text", &backgroundText[D_ERROR]);

    pthread_create(&progress_thread_, nullptr, ProgressThreadStartRoutine, this);

    RecoveryUI::Init();
}

void ScreenRecoveryUI::SetLocale(const char* new_locale) {
    if (new_locale) {
        this->locale = new_locale;
        char* lang = strdup(locale);
        for (char* p = lang; *p; ++p) {
            if (*p == '_') {
                *p = '\0';
                break;
            }
        }

        // A bit cheesy: keep an explicit list of supported languages
        // that are RTL.
        if (strcmp(lang, "ar") == 0 ||   // Arabic
            strcmp(lang, "fa") == 0 ||   // Persian (Farsi)
            strcmp(lang, "he") == 0 ||   // Hebrew (new language code)
            strcmp(lang, "iw") == 0 ||   // Hebrew (old language code)
            strcmp(lang, "ur") == 0) {   // Urdu
            rtl_locale = true;
        }
        free(lang);
    } else {
        new_locale = nullptr;
    }
}

void ScreenRecoveryUI::SetBackground(Icon icon) {
    pthread_mutex_lock(&updateMutex);

    currentIcon = icon;
    update_screen_locked();

    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgressType(ProgressType type) {
    pthread_mutex_lock(&updateMutex);
    if (progressBarType != type) {
        progressBarType = type;
    }
    progressScopeStart = 0;
    progressScopeSize = 0;
    progress = 0;

    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowProgress(float portion, float seconds) {
    pthread_mutex_lock(&updateMutex);
    progressBarType = DETERMINATE;
    progressScopeStart += progressScopeSize;
    progressScopeSize = portion;
    progressScopeTime = now();
    progressScopeDuration = seconds;
    progress = 0;

    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgress(float fraction) {
    pthread_mutex_lock(&updateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (progressBarType == DETERMINATE && fraction > progress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(progressBarEmpty);
        float scale = width * progressScopeSize;
        if ((int) (progress * scale) != (int) (fraction * scale)) {
            progress = fraction;
            update_screen_locked();
        }
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetStage(int current, int max) {
    pthread_mutex_lock(&updateMutex);
    stage = current;
    max_stage = max;
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::PrintV(const char* fmt, bool copy_to_stdout, va_list ap) {
    std::string str;
    android::base::StringAppendV(&str, fmt, ap);

    if (copy_to_stdout) {
        fputs(str.c_str(), stdout);
    }

    pthread_mutex_lock(&updateMutex);
    if (log_text_rows_ > 0 && log_text_cols_ > 0) {
        for (const char* ptr = str.c_str(); *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col_ >= log_text_cols_) {
                text_[text_row_][text_col_] = '\0';
                text_col_ = 0;
                text_row_ = (text_row_ + 1) % log_text_rows_;
                if (text_row_ == text_top_) text_top_ = (text_top_ + 1) % log_text_rows_;
            }
            if (*ptr != '\n') text_[text_row_][text_col_++] = *ptr;
        }
        text_[text_row_][text_col_] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    PrintV(fmt, true, ap);
    va_end(ap);
}

void ScreenRecoveryUI::PrintOnScreenOnly(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    PrintV(fmt, false, ap);
    va_end(ap);
}

void ScreenRecoveryUI::PutChar(char ch) {
    pthread_mutex_lock(&updateMutex);
    if (ch != '\n') text_[text_row_][text_col_++] = ch;
    if (ch == '\n' || text_col_ >= text_cols_) {
        text_col_ = 0;
        ++text_row_;

        if (text_row_ == text_top_) text_top_ = (text_top_ + 1) % text_rows_;
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ClearText() {
    pthread_mutex_lock(&updateMutex);
    text_col_ = 0;
    text_row_ = 0;
    text_top_ = 1;
    for (size_t i = 0; i < log_text_rows_; ++i) {
        memset(text_[i], 0, log_text_cols_ + 1);
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowFile(FILE* fp) {
    std::vector<long> offsets;
    offsets.push_back(ftell(fp));
    ClearText();

    SetBackground(RecoveryUI::VIEWING_LOG);

    struct stat sb;
    fstat(fileno(fp), &sb);

    bool show_prompt = false;
    while (true) {
        if (show_prompt) {
            PrintOnScreenOnly("--(%d%% of %d bytes)--",
                  static_cast<int>(100 * (double(ftell(fp)) / double(sb.st_size))),
                  static_cast<int>(sb.st_size));
            Redraw();
            while (show_prompt) {
                show_prompt = false;
                int key = WaitKey();
                if (key == KEY_POWER || key == KEY_ENTER) {
                    return;
                } else if (key == KEY_UP || key == KEY_VOLUMEUP) {
                    if (offsets.size() <= 1) {
                        show_prompt = true;
                    } else {
                        offsets.pop_back();
                        fseek(fp, offsets.back(), SEEK_SET);
                    }
                } else {
                    if (feof(fp)) {
                        return;
                    }
                    offsets.push_back(ftell(fp));
                }
            }
            ClearText();
        }

        int ch = getc(fp);
        if (ch == EOF) {
            while (text_row_ < text_rows_ - 1) PutChar('\n');
            show_prompt = true;
        } else {
            PutChar(ch);
            if (text_col_ == 0 && text_row_ >= text_rows_ - 1) {
                show_prompt = true;
            }
        }
    }
}

void ScreenRecoveryUI::ShowFile(const char* filename) {
    FILE* fp = fopen_path(filename, "re");
    if (fp == nullptr) {
        Print("  Unable to open %s: %s\n", filename, strerror(errno));
        return;
    }

    char** old_text = text_;
    size_t old_text_col = text_col_;
    size_t old_text_row = text_row_;
    size_t old_text_top = text_top_;

    // Swap in the alternate screen and clear it.
    text_ = file_viewer_text_;
    ClearText();

    ShowFile(fp);
    fclose(fp);

    text_ = old_text;
    text_col_ = old_text_col;
    text_row_ = old_text_row;
    text_top_ = old_text_top;
}

void ScreenRecoveryUI::DialogShowInfo(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = false;
    dialog_icon = D_INFO;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogShowError(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = false;
    dialog_icon = D_ERROR;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogShowErrorLog(const char* text)
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup(text);
    dialog_show_log = true;
    dialog_icon = D_ERROR;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::DialogDismiss()
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = NULL;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetHeadlessMode()
{
    pthread_mutex_lock(&updateMutex);
    free(dialog_text);
    dialog_text = strdup("");
    dialog_show_log = false;
    dialog_icon = HEADLESS;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetSysbarState(int state)
{
    if (HasBackKey()) {
        state &= ~SYSBAR_BACK;
    }
    if (HasHomeKey()) {
        state &= ~SYSBAR_HOME;
    }
    sysbar_state = state;
    Redraw();
}

void ScreenRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                                 int initial_selection) {
    pthread_mutex_lock(&updateMutex);
    if (text_rows_ > 0 && text_cols_ > 0) {
        header_items = 0;
        menu_headers_ = headers;
        if (menu_headers_) {
            while (menu_headers_[header_items]) {
                ++header_items;
            }
        }
        size_t i = 0;
        for (; i < text_rows_ && items[i] != nullptr; ++i) {
            strncpy(menu_[i], items[i], text_cols_ - 1);
            menu_[i][text_cols_ - 1] = '\0';
        }
        menu_items = i;
        show_menu = true;
        menu_sel = initial_selection;
        if (menu_show_start_ <= menu_sel - max_menu_rows_ ||
                menu_show_start_ > menu_sel) {
            menu_show_start_ = menu_sel;
        }
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

int ScreenRecoveryUI::SelectMenu(int sel, bool abs /* = false */) {
    int wrapped = 0;
    pthread_mutex_lock(&updateMutex);
    if (abs) {
        sel += menu_show_start_ - header_items;
    }
    if (show_menu) {
        int old_sel = menu_sel;
        menu_sel = sel;

        // Wrap at top and bottom.
        if (rainbow) {
            if (menu_sel > old_sel) {
                move_rainbow(1);
            } else if (menu_sel < old_sel) {
                move_rainbow(-1);
            }
        }
        if (menu_sel < 0) {
            wrapped = -1;
            menu_sel = menu_items + menu_sel;
        }
        if (menu_sel >= menu_items) {
            wrapped = 1;
            menu_sel = menu_sel - menu_items;
        }
        if (menu_sel < menu_show_start_ && menu_show_start_ > 0) {
            menu_show_start_ = menu_sel;
        }
        if (menu_sel - menu_show_start_ >= max_menu_rows_ - header_items) {
            menu_show_start_ = menu_sel - (max_menu_rows_ - header_items) + 1;
        }
        sel = menu_sel;
        if (wrapped != 0) {
            if (wrap_count / wrapped > 0) {
                wrap_count += wrapped;
            } else {
                wrap_count = wrapped;
            }
            if (wrap_count / wrapped >= 5) {
                wrap_count = 0;
                OMGRainbows();
            }
        }
        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
    return sel;
}

void ScreenRecoveryUI::EndMenu() {
    pthread_mutex_lock(&updateMutex);
    if (show_menu && text_rows_ > 0 && text_cols_ > 0) {
        show_menu = false;
    }
    pthread_mutex_unlock(&updateMutex);
}

bool ScreenRecoveryUI::IsTextVisible() {
    pthread_mutex_lock(&updateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&updateMutex);
    return visible;
}

bool ScreenRecoveryUI::WasTextEverVisible() {
    pthread_mutex_lock(&updateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&updateMutex);
    return ever_visible;
}

void ScreenRecoveryUI::ShowText(bool visible) {
    pthread_mutex_lock(&updateMutex);
    show_text = visible;
    if (show_text) show_text_ever = true;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Redraw() {
    pthread_mutex_lock(&updateMutex);
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::KeyLongPress(int) {
    // Redraw so that if we're in the menu, the highlight
    // will change color to indicate a successful long press.
    Redraw();
}
