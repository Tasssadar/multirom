LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(multirom_local_path)
LOCAL_SRC_FILES:= \
    fw_mounter.c \

LOCAL_MODULE := fw_mounter
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc libmultirom_static

include $(BUILD_EXECUTABLE)
