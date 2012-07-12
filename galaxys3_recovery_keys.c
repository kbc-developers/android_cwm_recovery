#include <linux/input.h>

#include "recovery_ui.h"
#include "common.h"
#include "extendedcommands.h"


int device_toggle_display(volatile char* key_pressed, int key_code) {
    return ( (get_allow_toggle_display()) && (key_code == KEY_POWER) );
}

int device_handle_key(int key_code, int visible) {
    if (visible) {
        switch (key_code) {
            case KEY_VOLUMEDOWN:
                return HIGHLIGHT_DOWN;
            case KEY_VOLUMEUP:
                return HIGHLIGHT_UP;
            case KEY_HOME:
                return SELECT_ITEM;
            case KEY_BACK:
                return GO_BACK;
            case KEY_POWER:
                break;
        }
    }

    return NO_ACTION;
}
