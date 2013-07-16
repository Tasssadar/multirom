# MultiROM
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

multirom_local_path := $(LOCAL_PATH)

LOCAL_SRC_FILES:= \
	main.c \
	util.c \
	framebuffer.c \
	multirom.c \
	input.c \
	multirom_ui.c \
	listview.c \
	checkbox.c \
	button.c \
	pong.c \
	progressdots.c \
	adb.c \
	multirom_ui_themes.c

# Init default define values
MULTIROM_THEMES :=
MULTIROM_DEFAULT_ROTATION := 0

# include device file
include $(multirom_local_path)/device_$(TARGET_DEVICE).mk

# Set defines and add theme files
LOCAL_CFLAGS += -DMULTIROM_DEFAULT_ROTATION=$(MULTIROM_DEFAULT_ROTATION)
$(foreach res,$(MULTIROM_THEMES), \
    $(eval LOCAL_SRC_FILES += multirom_ui_$(res).c) \
    $(eval LOCAL_CFLAGS += -DMULTIROM_THEME_$(res)) \
)


LOCAL_MODULE:= multirom
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libfs_mgr libcutils libc libm

ifeq ($(HAVE_SELINUX),true)
LOCAL_STATIC_LIBRARIES += libselinux
LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_CFLAGS += -DHAVE_SELINUX
endif

include $(BUILD_EXECUTABLE)

# Trampoline
include $(multirom_local_path)/trampoline/Android.mk
