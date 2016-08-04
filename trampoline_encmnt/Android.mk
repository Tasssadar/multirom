LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE:= trampoline_encmnt
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_SHARED_LIBRARIES := libcryptfslollipop libcutils
LOCAL_STATIC_LIBRARIES := libmultirom_static
LOCAL_WHOLE_STATIC_LIBRARIES := libm libpng libz libft2_mrom_static

ifneq ($(wildcard bootable/recovery/crypto/lollipop/cryptfs.h),)
    mr_twrp_path := bootable/recovery
else ifneq ($(wildcard bootable/recovery-twrp/crypto/lollipop/cryptfs.h),)
    mr_twrp_path := bootable/recovery-twrp
else
    $(error Failed to find path to TWRP, which is required to build MultiROM with encryption support)
endif

LOCAL_C_INCLUDES += $(multirom_local_path) $(mr_twrp_path) $(mr_twrp_path)/crypto/scrypt/lib/crypto external/openssl/include external/boringssl/include

LOCAL_SRC_FILES := \
    encmnt.c \
    pw_ui.c \
    ../rom_quirks.c \

include $(multirom_local_path)/device_defines.mk

include $(BUILD_EXECUTABLE)


ifeq ($(MR_ENCRYPTION_FAKE_PROPERTIES),true)
    include $(CLEAR_VARS)

    LOCAL_MODULE := libmultirom_fake_properties
    LOCAL_MODULE_TAGS := eng
    LOCAL_C_INCLUDES += $(multirom_local_path)

    LOCAL_SRC_FILES := fake_properties.c

    include $(multirom_local_path)/device_defines.mk

    include $(BUILD_SHARED_LIBRARY)
endif
