# MultiROM
ifeq ($(TARGET_RECOVERY_IS_MULTIROM),true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

multirom_local_path := $(LOCAL_PATH)

LOCAL_C_INCLUDES += $(multirom_local_path) \
    external/libpng \
    external/zlib \
    external/freetype/include \
    $(multirom_local_path)/lib

LOCAL_SRC_FILES:= \
    kexec.c \
    main.c \
    multirom.c \
    multirom_ui.c \
    multirom_ui_landscape.c \
    multirom_ui_portrait.c \
    multirom_ui_themes.c \
    pong.c \
    rcadditions.c \
    rom_quirks.c \

# With these, GCC optimizes aggressively enough so full-screen alpha blending
# is quick enough to be done in an animation
LOCAL_CFLAGS += -O3 -funsafe-math-optimizations

#LOCAL_CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-all -O0 -g -fno-omit-frame-pointer -Wall

LOCAL_MODULE:= multirom
LOCAL_MODULE_TAGS := eng

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libcutils libc libmultirom_static
LOCAL_WHOLE_STATIC_LIBRARIES := libm libcutils libpng libz libft2_mrom_static

# clone libbootimg to /system/extras/ from
# https://github.com/Tasssadar/libbootimg.git
LOCAL_STATIC_LIBRARIES += libbootimg
LOCAL_C_INCLUDES += system/extras/libbootimg/include

include $(multirom_local_path)/device_defines.mk

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

# Kexec-tools
include $(multirom_local_path)/kexec-tools/Android.mk

# adbd
include $(multirom_local_path)/adbd/Android.mk

# trampoline_encmnt
ifeq ($(MR_ENCRYPTION),true)
include $(multirom_local_path)/trampoline_encmnt/Android.mk
endif

# libmultirom
include $(multirom_local_path)/lib/Android.mk

endif
