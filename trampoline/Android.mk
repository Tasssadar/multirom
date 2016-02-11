LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(multirom_local_path) $(multirom_local_path)/lib
LOCAL_SRC_FILES:= \
    trampoline.c \
    devices.c \
    adb.c \

LOCAL_MODULE:= trampoline
LOCAL_MODULE_TAGS := eng

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_STATIC_LIBRARIES := libcutils libc libmultirom_static libbootimg
LOCAL_FORCE_STATIC_EXECUTABLE := true

ifeq ($(MR_INIT_DEVICES),)
    $(info MR_INIT_DEVICES was not defined in device files!)
endif
LOCAL_SRC_FILES += ../../../../$(MR_INIT_DEVICES)

# for adb
LOCAL_CFLAGS += -DPRODUCT_MODEL="\"$(PRODUCT_MODEL)\"" -DPRODUCT_MANUFACTURER="\"$(PRODUCT_MANUFACTURER)\""

# to find fstab
LOCAL_CFLAGS += -DTARGET_DEVICE="\"$(TARGET_DEVICE)\""

# create by-name symlinks for kernels that don't have it (eg older HTC One M7)
# specify MR_POPULATE_BY_NAME_PATH := "/dev/block/platform/msm_sdcc.1/by-name"
# or similar in BoardConfig
ifneq ($(MR_POPULATE_BY_NAME_PATH),)
    LOCAL_CFLAGS += -DMR_POPULATE_BY_NAME_PATH=\"$(MR_POPULATE_BY_NAME_PATH)\"
endif

# also add /dev/block/bootdevice symlinks
ifeq ($(MR_DEV_BLOCK_BOOTDEVICE),true)
    LOCAL_CFLAGS += -DMR_DEV_BLOCK_BOOTDEVICE
endif

ifneq ($(MR_DEVICE_HOOKS),)
ifeq ($(MR_DEVICE_HOOKS_VER),)
    $(info MR_DEVICE_HOOKS is set but MR_DEVICE_HOOKS_VER is not specified!)
else
    LOCAL_CFLAGS += -DMR_DEVICE_HOOKS=$(MR_DEVICE_HOOKS_VER)
    LOCAL_SRC_FILES += ../../../../$(MR_DEVICE_HOOKS)
endif
endif

ifeq ($(MR_ENCRYPTION),true)
    LOCAL_CFLAGS += -DMR_ENCRYPTION
    LOCAL_SRC_FILES += encryption.c
endif

ifeq ($(MR_ENCRYPTION_FAKE_PROPERTIES),true)
    LOCAL_CFLAGS += -DMR_ENCRYPTION_FAKE_PROPERTIES
endif

include $(BUILD_EXECUTABLE)
