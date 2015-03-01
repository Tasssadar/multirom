LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE:= trampoline_encmnt
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_SHARED_LIBRARIES := libcryptfslollipop libcutils
LOCAL_STATIC_LIBRARIES := libmultirom_static
LOCAL_WHOLE_STATIC_LIBRARIES := libm libpng libz libft2_mrom_static

mr_twrp_path := bootable/recovery
LOCAL_C_INCLUDES += $(multirom_local_path) $(mr_twrp_path) $(mr_twrp_path)/crypto/scrypt/lib/crypto external/openssl/include

LOCAL_SRC_FILES := \
    encmnt.c \
    pw_ui.c \

include $(multirom_local_path)/device_defines.mk

include $(BUILD_EXECUTABLE)
