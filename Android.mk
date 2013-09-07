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
    multirom_ui_themes.c

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    LOCAL_SRC_FILES += col32cb16blend_neon.S
    LOCAL_CFLAGS += -DHAS_NEON_BLEND
endif

LOCAL_MODULE:= multirom
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libfs_mgr libcutils libc libm


# Defines from device files
# Init default define values
MULTIROM_DEFAULT_ROTATION := 0

ifeq ($(MR_INPUT_TYPE),)
    MR_INPUT_TYPE := type_b
endif
LOCAL_SRC_FILES += input_$(MR_INPUT_TYPE).c

ifeq ($(DEVICE_RESOLUTION),)
    $(error DEVICE_RESOLUTION was not specified)
endif
LOCAL_SRC_FILES += multirom_ui_$(DEVICE_RESOLUTION).c
LOCAL_CFLAGS += -DMULTIROM_THEME_$(DEVICE_RESOLUTION)

ifneq ($(LANDSCAPE_RESOLUTION),)
    LOCAL_SRC_FILES += multirom_ui_$(LANDSCAPE_RESOLUTION).c
    LOCAL_CFLAGS += -DMULTIROM_THEME_$(LANDSCAPE_RESOLUTION)
endif
ifneq ($(TW_DEFAULT_ROTATION),)
    MULTIROM_DEFAULT_ROTATION := $(TW_DEFAULT_ROTATION)
endif
LOCAL_CFLAGS += -DMULTIROM_DEFAULT_ROTATION=$(MULTIROM_DEFAULT_ROTATION)

# TWRP framebuffer flags
ifeq ($(RECOVERY_GRAPHICS_USE_LINELENGTH), true)
    LOCAL_CFLAGS += -DRECOVERY_GRAPHICS_USE_LINELENGTH
endif

ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"RGBX_8888")
    LOCAL_CFLAGS += -DRECOVERY_RGBX
endif
ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"BGRA_8888")
    LOCAL_CFLAGS += -DRECOVERY_BGRA
endif
ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"RGB_565")
    LOCAL_CFLAGS += -DRECOVERY_RGB_565
endif

ifeq ($(MR_DPI),)
    $(error MR_DPI not defined in device files)
else ifeq ($(MR_DPI),hdpi)
    LOCAL_CFLAGS += -DDPI_MUL=1
else ifeq ($(MR_DPI),xhdpi)
    LOCAL_CFLAGS += -DDPI_MUL=1.5
endif
LOCAL_CFLAGS += -DMR_DPI=$(MR_DPI)

include $(BUILD_EXECUTABLE)

# Trampoline
include $(multirom_local_path)/trampoline/Android.mk

# ZIP installer
include $(multirom_local_path)/install_zip/Android.mk
