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

ifeq ($(DEVICE_RESOLUTION),)
    $(info DEVICE_RESOLUTION was not specified)
# FIXME
#else ifneq ($(wildcard $(multirom_local_path)/themes/multirom_ui_$(DEVICE_RESOLUTION).c),)
#    LOCAL_SRC_FILES += themes/multirom_ui_$(DEVICE_RESOLUTION).c
#    LOCAL_CFLAGS += -DMULTIROM_THEME_$(DEVICE_RESOLUTION)
endif

ifneq ($(LANDSCAPE_RESOLUTION),)
# FIXME
#ifneq ($(wildcard $(multirom_local_path)/themes/multirom_ui_$(LANDSCAPE_RESOLUTION).c),)
#    LOCAL_SRC_FILES += themes/multirom_ui_$(LANDSCAPE_RESOLUTION).c
#    LOCAL_CFLAGS += -DMULTIROM_THEME_$(LANDSCAPE_RESOLUTION)
#endif
endif

ifneq ($(TW_DEFAULT_ROTATION),)
    MULTIROM_DEFAULT_ROTATION := $(TW_DEFAULT_ROTATION)
endif
LOCAL_CFLAGS += -DMULTIROM_DEFAULT_ROTATION=$(MULTIROM_DEFAULT_ROTATION)

# TWRP framebuffer flags
ifeq ($(RECOVERY_GRAPHICS_USE_LINELENGTH), true)
    LOCAL_CFLAGS += -DRECOVERY_GRAPHICS_USE_LINELENGTH
endif

ifeq ($(MR_PIXEL_FORMAT),)
    MR_PIXEL_FORMAT := $(TARGET_RECOVERY_PIXEL_FORMAT)
endif

ifeq ($(MR_PIXEL_FORMAT),"RGBX_8888")
    LOCAL_CFLAGS += -DRECOVERY_RGBX
else ifeq ($(MR_PIXEL_FORMAT),"BGRA_8888")
    LOCAL_CFLAGS += -DRECOVERY_BGRA
else ifeq ($(MR_PIXEL_FORMAT),"RGB_565")
    LOCAL_CFLAGS += -DRECOVERY_RGB_565
else
    $(info TARGET_RECOVERY_PIXEL_FORMAT or MR_PIXEL_FORMAT not set or have invalid value)
endif

ifeq ($(MR_DPI),)
    $(info MR_DPI not defined in device files)
else ifeq ($(MR_DPI),hdpi)
ifeq ($(MR_DPI_MUL),)
    MR_DPI_MUL := 1
endif
else ifeq ($(MR_DPI),xhdpi)
ifeq ($(MR_DPI_MUL),)
    MR_DPI_MUL := 1.5
endif
endif

ifeq ($(MR_DPI_FONT),)
    MR_DPI_FONT := 96
endif

LOCAL_CFLAGS += -DMR_DPI_FONT=$(MR_DPI_FONT)

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

ifneq ($(MR_DEFAULT_BRIGHTNESS),)
    LOCAL_CFLAGS += -DMULTIROM_DEFAULT_BRIGHTNESS=\"$(MR_DEFAULT_BRIGHTNESS)\"
else
    LOCAL_CFLAGS += -DMULTIROM_DEFAULT_BRIGHTNESS=40
endif

ifneq ($(MR_KEXEC_MEM_MIN),)
    LOCAL_CFLAGS += -DMR_KEXEC_MEM_MIN=\"$(MR_KEXEC_MEM_MIN)\"
else
    $(info MR_KEXEC_MEM_MIN was not defined in device files!)
endif

ifeq ($(MR_KEXEC_DTB),true)
    LOCAL_CFLAGS += -DMR_KEXEC_DTB
endif

ifeq ($(MR_CONTINUOUS_FB_UPDATE),true)
    LOCAL_CFLAGS += -DMR_CONTINUOUS_FB_UPDATE
endif

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
