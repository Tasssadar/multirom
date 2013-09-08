LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	trampoline.c \
	devices.c \
	../util.c \
	adb.c

LOCAL_MODULE:= trampoline
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libfs_mgr libcutils libc

ifeq ($(MR_INIT_DEVICES),)
    $(error MR_INIT_DEVICES was not defined in device files!)
endif
LOCAL_SRC_FILES += ../../../../$(MR_INIT_DEVICES)

# for adb
LOCAL_CFLAGS += -DPRODUCT_MODEL="\"$(PRODUCT_MODEL)\"" -DPRODUCT_MANUFACTURER="\"$(PRODUCT_MANUFACTURER)\""

include $(BUILD_EXECUTABLE)
