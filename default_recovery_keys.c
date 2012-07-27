#include <linux/input.h>

#include "recovery_ui.h"
#include "common.h"
#include "extendedcommands.h"


int device_toggle_display(volatile char* key_pressed, int key_code) {
#ifdef TARGET_DEVICE_SC06D
    return ( (get_allow_toggle_display()) && (key_code == KEY_POWER) );
#endif
#ifdef TARGET_DEVICE_SC02C
    return ( (get_allow_toggle_display()) && (key_code == KEY_POWER) );
#endif
}

int device_handle_key(int key_code, int visible) {
    if (visible) {
#ifdef TARGET_DEVICE_SC06D
        switch (key_code) {
            case KEY_VOLUMEDOWN:
                return HIGHLIGHT_DOWN;
            case KEY_VOLUMEUP:
                return HIGHLIGHT_UP;
            case KEY_HOMEPAGE:
            case KEY_POWER:
                return SELECT_ITEM;
            case KEY_BACK:
                if (!ui_root_menu) {
                    return GO_BACK;
                }
#endif
#ifdef TARGET_DEVICE_SC02C
            case KEY_VOLUMEDOWN:
                return HIGHLIGHT_DOWN;
            case KEY_VOLUMEUP:
                return HIGHLIGHT_UP;
            case KEY_HOME:
                return SELECT_ITEM;
            case KEY_BACK:
                if (!ui_root_menu) {
                    return GO_BACK;
                }
            case KEY_POWER:
                break;
#endif
        }
    }

    return NO_ACTION;
}
