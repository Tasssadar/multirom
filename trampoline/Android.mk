LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(multirom_local_path)
LOCAL_SRC_FILES:= \
    trampoline.c \
    devices.c \
    ../util.c \
    adb.c \
    ../fstab.c \
    ../containers.c

LOCAL_MODULE:= trampoline
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc

LOCAL_CFLAGS += -DMR_LOG_TAG=\"trampoline\"

ifeq ($(MR_INIT_DEVICES),)
    $(info MR_INIT_DEVICES was not defined in device files!)
endif
LOCAL_SRC_FILES += ../../../../$(MR_INIT_DEVICES)

# for adb
LOCAL_CFLAGS += -DPRODUCT_MODEL="\"$(PRODUCT_MODEL)\"" -DPRODUCT_MANUFACTURER="\"$(PRODUCT_MANUFACTURER)\""

# to find fstab
LOCAL_CFLAGS += -DTARGET_DEVICE="\"$(TARGET_DEVICE)\""

ifneq ($(MR_DEVICE_HOOKS),)
ifeq ($(MR_DEVICE_HOOKS_VER),)
    $(info MR_DEVICE_HOOKS is set but MR_DEVICE_HOOKS_VER is not specified!)
else
    LOCAL_CFLAGS += -DMR_DEVICE_HOOKS=$(MR_DEVICE_HOOKS_VER)
    LOCAL_SRC_FILES += ../../../../$(MR_DEVICE_HOOKS)
endif
endif

include $(BUILD_EXECUTABLE)
