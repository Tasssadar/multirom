# MultiROM
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

multirom_local_path := $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(multirom_local_path) \
    external/libpng \
    external/zlib \
    external/freetype/include

LOCAL_SRC_FILES:= \
    animation.c \
    button.c \
    containers.c \
    framebuffer.c \
    framebuffer_generic.c \
    framebuffer_png.c \
    framebuffer_truetype.c \
    fstab.c \
    input.c \
    kexec.c \
    listview.c \
    main.c \
    multirom.c \
    multirom_ui.c \
    multirom_ui_themes.c \
    themes/multirom_ui_landscape.c \
    themes/multirom_ui_portrait.c \
    notification_card.c \
    pong.c \
    progressdots.c \
    rom_quirks.c \
    tabview.c \
    touch_tracker.c \
    util.c \
    workers.c

# With these, GCC optimizes aggressively enough so full-screen alpha blending
# is quick enough to be done in an animation
LOCAL_CFLAGS += -O3 -funsafe-math-optimizations

#LOCAL_CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-all -O0 -g -fno-omit-frame-pointer -Wall

LOCAL_MODULE:= multirom
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc libm libpng libz libft2_mrom_static

# clone libbootimg to /system/extras/ from
# https://github.com/Tasssadar/libbootimg.git
LOCAL_STATIC_LIBRARIES += libbootimg
LOCAL_C_INCLUDES += system/extras/libbootimg/include

# Defines from device files
# Init default define values
MULTIROM_DEFAULT_ROTATION := 0

LOCAL_CFLAGS += -DMR_LOG_TAG=\"multirom\"

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

ifneq ($(MR_DEVICE_HOOKS),)
ifeq ($(MR_DEVICE_HOOKS_VER),)
    $(info MR_DEVICE_HOOKS is set but MR_DEVICE_HOOKS_VER is not specified!)
else
    LOCAL_CFLAGS += -DMR_DEVICE_HOOKS=$(MR_DEVICE_HOOKS_VER)
    LOCAL_SRC_FILES += ../../../$(MR_DEVICE_HOOKS)
endif
endif

ifeq ($(MR_USE_QCOM_OVERLAY),true)
    LOCAL_CFLAGS += -DMR_USE_QCOM_OVERLAY
    LOCAL_SRC_FILES += framebuffer_qcom_overlay.c
ifneq ($(MR_QCOM_OVERLAY_HEADER),)
    LOCAL_CFLAGS += -DMR_QCOM_OVERLAY_HEADER=\"../../../$(MR_QCOM_OVERLAY_HEADER)\"
else
    $(error MR_USE_QCOM_OVERLAY is true but MR_QCOM_OVERLAY_HEADER was not specified!)
endif
ifneq ($(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT),)
    LOCAL_CFLAGS += -DMR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT=$(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT)
endif
ifeq ($(MR_QCOM_OVERLAY_USE_VSYNC),true)
    LOCAL_CFLAGS += -DMR_QCOM_OVERLAY_USE_VSYNC
endif
endif

ifeq ($(MR_CONTINUOUS_FB_UPDATE),true)
    LOCAL_CFLAGS += -DMR_CONTINUOUS_FB_UPDATE
endif

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

include $(BUILD_EXECUTABLE)

# Trampoline
include $(multirom_local_path)/trampoline/Android.mk

# fw_mounter
include $(multirom_local_path)/fw_mounter/Android.mk

# ZIP installer
include $(multirom_local_path)/install_zip/Android.mk

# Kexec-tools
include $(multirom_local_path)/kexec-tools/Android.mk

# adbd
include $(multirom_local_path)/adbd/Android.mk

# We need static libtruetype but it isn't in standard android makefile :(
LOCAL_PATH := external/freetype/
include $(CLEAR_VARS)

# compile in ARM mode, since the glyph loader/renderer is a hotspot
# when loading complex pages in the browser
#
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES:= \
    src/base/ftbbox.c \
    src/base/ftbitmap.c \
    src/base/ftfstype.c \
    src/base/ftglyph.c \
    src/base/ftlcdfil.c \
    src/base/ftstroke.c \
    src/base/fttype1.c \
    src/base/ftxf86.c \
    src/base/ftbase.c \
    src/base/ftsystem.c \
    src/base/ftinit.c \
    src/base/ftgasp.c \
    src/raster/raster.c \
    src/sfnt/sfnt.c \
    src/smooth/smooth.c \
    src/autofit/autofit.c \
    src/truetype/truetype.c \
    src/cff/cff.c \
    src/psnames/psnames.c \
    src/pshinter/pshinter.c

ifeq ($(shell if [ -e "$(ANDROID_BUILD_TOP)/external/freetype/src/gzip/ftgzip.c" ]; then echo "hasgzip"; fi),hasgzip)
LOCAL_SRC_FILES += src/gzip/ftgzip.c
endif

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/builds \
    $(LOCAL_PATH)/include \
    external/libpng \
    external/zlib

LOCAL_CFLAGS += -W -Wall
LOCAL_CFLAGS += -fPIC -DPIC
LOCAL_CFLAGS += "-DDARWIN_NO_CARBON"
LOCAL_CFLAGS += "-DFT2_BUILD_LIBRARY"

LOCAL_STATIC_LIBRARIES += libpng libz

# the following is for testing only, and should not be used in final builds
# of the product
#LOCAL_CFLAGS += "-DTT_CONFIG_OPTION_BYTECODE_INTERPRETER"

LOCAL_CFLAGS += -O2

LOCAL_MODULE:= libft2_mrom_static
include $(BUILD_STATIC_LIBRARY)
