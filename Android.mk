# MultiROM
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

multirom_local_path := $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(multirom_local_path)
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
    multirom_ui_themes.c \
    themes/multirom_ui_landscape.c \
    themes/multirom_ui_portrait.c \
    fstab.c \
    workers.c \
    containers.c

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    LOCAL_SRC_FILES += col32cb16blend_neon.S
    LOCAL_CFLAGS += -DHAS_NEON_BLEND
endif

LOCAL_MODULE:= multirom
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc libm

# clone libbootimg to /system/extras/ from
# https://github.com/Tasssadar/libbootimg.git
LOCAL_STATIC_LIBRARIES += libbootimg
LOCAL_C_INCLUDES += system/extras/libbootimg/include

# Defines from device files
# Init default define values
MULTIROM_DEFAULT_ROTATION := 0

# This value is used to have different folders on USB drives
# for different devices. Grouper didn't have that, hence the hack
LOCAL_CFLAGS += -DTARGET_DEVICE="\"$(TARGET_DEVICE)\""
ifeq ($(TARGET_DEVICE),grouper)
    LOCAL_CFLAGS += -DMR_MOVE_USB_DIR
endif

# Flo's bootloader removes first 26 characters from boot.img's cmdline
# because of reasons. On unmodified boot.img, those 26 characters are
# "console=ttyHSL0,115200,n8 "
ifeq ($(TARGET_DEVICE),flo)
    LOCAL_CFLAGS += -DFLO_CMDLINE_HACK
endif

ifeq ($(MR_INPUT_TYPE),)
    MR_INPUT_TYPE := type_b
endif
LOCAL_SRC_FILES += input_$(MR_INPUT_TYPE).c

ifeq ($(DEVICE_RESOLUTION),)
    $(info DEVICE_RESOLUTION was not specified)
else ifneq ($(wildcard $(multirom_local_path)/themes/multirom_ui_$(DEVICE_RESOLUTION).c),)
    LOCAL_SRC_FILES += themes/multirom_ui_$(DEVICE_RESOLUTION).c
    LOCAL_CFLAGS += -DMULTIROM_THEME_$(DEVICE_RESOLUTION)
endif

ifneq ($(LANDSCAPE_RESOLUTION),)
ifneq ($(wildcard $(multirom_local_path)/themes/multirom_ui_$(LANDSCAPE_RESOLUTION).c),)
    LOCAL_SRC_FILES += themes/multirom_ui_$(LANDSCAPE_RESOLUTION).c
    LOCAL_CFLAGS += -DMULTIROM_THEME_$(LANDSCAPE_RESOLUTION)
endif
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
    $(info MR_DPI not defined in device files)
else ifeq ($(MR_DPI),hdpi)
ifeq ($(MR_DPI_MUL),)
    MR_DPI_MUL := 1
endif
    LOCAL_CFLAGS += -DMR_HDPI
else ifeq ($(MR_DPI),xhdpi)
ifeq ($(MR_DPI_MUL),)
    MR_DPI_MUL := 1.5
endif
    LOCAL_CFLAGS += -DMR_XHDPI
endif

ifneq ($(MR_DPI_MUL),)
    LOCAL_CFLAGS += -DDPI_MUL=$(MR_DPI_MUL)
else
    $(info MR_DPI_MUL not defined!)
endif

ifeq ($(MR_DISABLE_ALPHA),true)
    LOCAL_CFLAGS += -DMR_DISABLE_ALPHA
endif

ifneq ($(TW_BRIGHTNESS_PATH),)
    LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=\"$(TW_BRIGHTNESS_PATH)\"
endif

ifneq ($(MR_KEXEC_MEM_MIN),)
    LOCAL_CFLAGS += -DMR_KEXEC_MEM_MIN=\"$(MR_KEXEC_MEM_MIN)\"
else
    $(info MR_KEXEC_MEM_MIN was not defined in device files!)
endif

ifeq ($(MR_KEXEC_DTB),true)
    LOCAL_CFLAGS += -DMR_KEXEC_DTB
endif

ifneq ($(MR_DEVICE_HOOKS),)
ifeq ($(MR_DEVICE_HOOKS_VER),)
    $(info MR_DEVICE_HOOKS is set but MR_DEVICE_HOOKS_VER is not specified!)
else
    LOCAL_CFLAGS += -DMR_DEVICE_HOOKS=$(MR_DEVICE_HOOKS_VER)
    LOCAL_SRC_FILES += ../../../$(MR_DEVICE_HOOKS)
endif
endif

include $(BUILD_EXECUTABLE)

# Trampoline
include $(multirom_local_path)/trampoline/Android.mk

# ZIP installer
include $(multirom_local_path)/install_zip/Android.mk
