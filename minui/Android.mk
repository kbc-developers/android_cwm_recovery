LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := events.c resources.c graphics.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui

LOCAL_CFLAGS += -DRECOVERY_RGBX

include $(BUILD_STATIC_LIBRARY)
