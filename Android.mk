# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	builtins.c \
	init.c \
	devices.c \
	property_service.c \
	util.c \
	parser.c \
	bootmgr.c \
	bootmgr_drawing.c \
	bootmgr_input.c \
	tetris.c \
	logo.c

ifeq ($(strip $(INIT_BOOTCHART)),true)
LOCAL_SRC_FILES += bootchart.c
LOCAL_CFLAGS    += -DBOOTCHART=1
endif
ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
    LOCAL_CFLAGS += -DAUDIOV2
endif
LOCAL_MODULE:= init

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc

ifeq ($(QCOM_TARGET_PRODUCT),msm8660_surf)
LOCAL_CFLAGS += -DSLAVE4_IS_FM
endif

#LOCAL_STATIC_LIBRARIES := libcutils libc libminui libpixelflinger_static
#LOCAL_STATIC_LIBRARIES += libminzip libunz libamend libmtdutils libmincrypt
#LOCAL_STATIC_LIBRARIES += libstdc++_static

include $(BUILD_EXECUTABLE)

