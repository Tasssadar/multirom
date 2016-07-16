LOCAL_PATH:= $(call my-dir)

common_SRC_FILES := \
    animation.c \
    button.c \
    colors.c \
    containers.c \
    framebuffer.c \
    framebuffer_generic.c \
    framebuffer_png.c \
    framebuffer_truetype.c \
    fstab.c \
    inject.c \
    input.c \
    listview.c \
    keyboard.c \
    mrom_data.c \
    notification_card.c \
    progressdots.c \
    tabview.c \
    touch_tracker.c \
    util.c \
    workers.c \

common_C_INCLUDES := $(multirom_local_path)/lib \
    external/libpng \
    external/zlib \
    external/freetype/include \
    system/extras/libbootimg/include \

# With these, GCC optimizes aggressively enough so full-screen alpha blending
# is quick enough to be done in an animation
common_C_FLAGS := -O3 -funsafe-math-optimizations

ifeq ($(MR_INPUT_TYPE),)
    MR_INPUT_TYPE := type_b
endif
common_SRC_FILES += input_$(MR_INPUT_TYPE).c

ifeq ($(MR_USE_QCOM_OVERLAY),true)
    common_C_FLAGS += -DMR_USE_QCOM_OVERLAY
    common_SRC_FILES += framebuffer_qcom_overlay.c
ifneq ($(MR_QCOM_OVERLAY_HEADER),)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_HEADER=\"../../../../$(MR_QCOM_OVERLAY_HEADER)\"
else
    $(error MR_USE_QCOM_OVERLAY is true but MR_QCOM_OVERLAY_HEADER was not specified!)
endif
ifneq ($(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT),)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT=$(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT)
endif
ifeq ($(MR_QCOM_OVERLAY_USE_VSYNC),true)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_USE_VSYNC
endif
ifneq ($(MR_QCOM_OVERLAY_HEAP_ID_MASK),)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_HEAP_ID_MASK=$(MR_QCOM_OVERLAY_HEAP_ID_MASK)
endif
endif



include $(CLEAR_VARS)

LOCAL_MODULE := libmultirom_static
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_CFLAGS += $(common_C_FLAGS)
LOCAL_C_INCLUDES += $(common_C_INCLUDES)
LOCAL_SRC_FILES := $(common_SRC_FILES)

include $(multirom_local_path)/device_defines.mk

include $(BUILD_STATIC_LIBRARY)



include $(CLEAR_VARS)

LOCAL_MODULE := libmultirom
LOCAL_MODULE_TAGS := eng
LOCAL_SHARED_LIBRARIES := libcutils libc libm libpng libz libft2
LOCAL_CFLAGS += $(common_C_FLAGS)
LOCAL_SRC_FILES := $(common_SRC_FILES)
LOCAL_C_INCLUDES += $(common_C_INCLUDES)

include $(multirom_local_path)/device_defines.mk

include $(BUILD_SHARED_LIBRARY)



# We need static libtruetype but it isn't in standard android makefile :(
LOCAL_PATH := external/freetype/
include $(CLEAR_VARS)

# compile in ARM mode, since the glyph loader/renderer is a hotspot
# when loading complex pages in the browser
#
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
    src/base/ftbbox.c \
    src/base/ftbitmap.c \
    src/base/ftfstype.c \
    src/base/ftglyph.c \
    src/base/ftlcdfil.c \
    src/base/ftstroke.c \
    src/base/fttype1.c \
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

ifeq ($(shell if [ -e "$(ANDROID_BUILD_TOP)/external/freetype/src/base/ftxf86.c" ]; then echo "found"; fi),found)
    LOCAL_SRC_FILES += src/base/ftxf86.c
else
    LOCAL_SRC_FILES += \
        src/base/ftfntfmt.c \
        src/base/ftmm.c
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
