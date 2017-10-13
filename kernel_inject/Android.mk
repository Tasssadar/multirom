LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += \
    $(multirom_local_path) \
    $(multirom_local_path)/lib \
    system/extras/libbootimg/include \

LOCAL_SRC_FILES:= \
    kernel_inject.c \

LOCAL_MODULE:= kernel_inject
LOCAL_MODULE_TAGS := eng

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_STATIC_LIBRARIES := libcutils libc libmultirom_static libbootimg
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
