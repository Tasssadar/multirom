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

ifeq ($(HAVE_SELINUX),true)
LOCAL_STATIC_LIBRARIES += libselinux
LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_CFLAGS += -DHAVE_SELINUX
endif

include $(BUILD_EXECUTABLE)
