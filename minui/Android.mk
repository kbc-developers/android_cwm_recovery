LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := events.c resources.c graphics.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui

ifeq ($(TARGET_PRODUCT), cm_d2dcm)
  LOCAL_CFLAGS += -DRECOVERY_RGBX
else ifeq ($(TARGET_PRODUCT), cm_galaxys2)
  LOCAL_CFLAGS += -DRECOVERY_BGRA
endif

ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
endif

include $(BUILD_STATIC_LIBRARY)
