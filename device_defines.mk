# Defines from device files
# Init default define values
MULTIROM_DEFAULT_ROTATION := 0

# This value is used to have different folders on USB drives
# for different devices. Grouper didn't have that, hence the hack
LOCAL_CFLAGS += -DTARGET_DEVICE="\"$(TARGET_DEVICE)\""
ifeq ($(TARGET_DEVICE),grouper)
    LOCAL_CFLAGS += -DMR_MOVE_USB_DIR
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
else ifeq ($(MR_PIXEL_FORMAT),"RGBA_8888")
    LOCAL_CFLAGS += -DRECOVERY_RGBA
else ifeq ($(MR_PIXEL_FORMAT),"BGRA_8888")
    LOCAL_CFLAGS += -DRECOVERY_BGRA
else ifeq ($(MR_PIXEL_FORMAT),"RGB_565")
    LOCAL_CFLAGS += -DRECOVERY_RGB_565
else ifeq ($(MR_PIXEL_FORMAT),"ABGR_8888")
	LOCAL_CFLAGS += -DRECOVERY_ABGR
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
else ifeq ($(MR_DPI),xxhdpi)
ifeq ($(MR_DPI_MUL),)
    MR_DPI_MUL := 2.0
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

ifneq ($(TW_SECONDARY_BRIGHTNESS_PATH),)
    LOCAL_CFLAGS += -DTW_SECONDARY_BRIGHTNESS_PATH=\"$(TW_SECONDARY_BRIGHTNESS_PATH)\"
endif

ifeq ($(TW_SCREEN_BLANK_ON_BOOT), true)
    LOCAL_CFLAGS += -DTW_SCREEN_BLANK_ON_BOOT
endif

ifneq ($(MR_DEFAULT_BRIGHTNESS),)
    LOCAL_CFLAGS += -DMULTIROM_DEFAULT_BRIGHTNESS=\"$(MR_DEFAULT_BRIGHTNESS)\"
else
    LOCAL_CFLAGS += -DMULTIROM_DEFAULT_BRIGHTNESS=40
endif

ifneq ($(MR_INPUT_ROTATION),)
    LOCAL_CFLAGS += -DMR_INPUT_ROTATION=$(MR_INPUT_ROTATION)
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

ifneq ($(BOARD_BOOTIMAGE_PARTITION_SIZE),)
    LOCAL_CFLAGS += -DBOARD_BOOTIMAGE_PARTITION_SIZE=$(BOARD_BOOTIMAGE_PARTITION_SIZE)
endif

ifeq ($(MR_USE_MROM_FSTAB),true)
    LOCAL_CFLAGS += -DMR_USE_MROM_FSTAB
endif

ifeq ($(MR_ENCRYPTION),true)
    LOCAL_CFLAGS += -DMR_ENCRYPTION
endif

ifneq ($(MR_RD_ADDR),)
    LOCAL_CFLAGS += -DMR_RD_ADDR=$(MR_RD_ADDR)
endif

MR_NO_KEXEC_MK_OPTIONS := true 1 allowed 2 enabled 3 ui_confirm 4 ui_choice 5 forced
ifneq (,$(filter $(MR_NO_KEXEC), $(MR_NO_KEXEC_MK_OPTIONS)))
    ifneq (,$(filter $(MR_NO_KEXEC), true 1 allowed))
        # NO_KEXEC_DISABLED    =  0x00,   // no-kexec workaround disabled
        LOCAL_CFLAGS += -DMR_NO_KEXEC=0x00
    else ifneq (,$(filter $(MR_NO_KEXEC), 2 enabled))
        # NO_KEXEC_ALLOWED     =  0x01,   // "Use no-kexec only when needed"
        LOCAL_CFLAGS += -DMR_NO_KEXEC=0x01
    else ifneq (,$(filter $(MR_NO_KEXEC), 3 ui_confirm))
        # NO_KEXEC_CONFIRM     =  0x02,   // "..... but also ask for confirmation"
        LOCAL_CFLAGS += -DMR_NO_KEXEC=0x02
    else ifneq (,$(filter $(MR_NO_KEXEC), 4 ui_choice))
        # NO_KEXEC_CHOICE      =  0x04,   // "Ask whether to kexec or use no-kexec"
        LOCAL_CFLAGS += -DMR_NO_KEXEC=0x04
    else ifneq (,$(filter $(MR_NO_KEXEC), 5 forced))
        # NO_KEXEC_FORCED      =  0x08,   // "Always force using no-kexec workaround"
        LOCAL_CFLAGS += -DMR_NO_KEXEC=0x08
    endif
endif

ifneq ($(MR_DEVICE_SPECIFIC_VERSION),)
    LOCAL_CFLAGS += -DMR_DEVICE_SPECIFIC_VERSION=\"$(MR_DEVICE_SPECIFIC_VERSION)\"
endif
