LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := events.c resources.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui

ifeq ($(TARGET_PRODUCT), cm_d2dcm)
  LOCAL_SRC_FILES += graphics.c
  LOCAL_CFLAGS += -DRECOVERY_RGBX
else ifeq ($(TARGET_PRODUCT), cm_galaxys2)
  LOCAL_SRC_FILES += graphics.c
  LOCAL_CFLAGS += -DRECOVERY_BGRA
else ifeq ($(TARGET_PRODUCT), cm_quincydcm)
  LOCAL_SRC_FILES += graphics.c
  LOCAL_CFLAGS += -DRECOVERY_RGBX
else ifeq ($(TARGET_PRODUCT), cm_celoxdcm)
  LOCAL_SRC_FILES += graphics.c
  LOCAL_CFLAGS += -DRECOVERY_RGBX
else ifeq ($(TARGET_PRODUCT), cm_urushi)
  LOCAL_SRC_FILES += graphics_urushi.c
  LOCAL_CFLAGS += -DRECOVERY_RGB565
#unknown product name yet.
else ifeq ($(TARGET_PRODUCT), cm_galaxynote2)
  LOCAL_SRC_FILES += graphics.c
  LOCAL_CFLAGS += -DRECOVERY_RGBX
endif



ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
endif

include $(BUILD_STATIC_LIBRARY)
