LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE:= trampoline_encmnt
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_SHARED_LIBRARIES := libcutils libcryptfslollipop

mr_twrp_path := bootable/recovery
LOCAL_C_INCLUDES += $(multirom_local_path) $(mr_twrp_path) $(mr_twrp_path)/crypto/scrypt/lib/crypto external/openssl/include

LOCAL_SRC_FILES := \
    encmnt.c \
    ../fstab.c \
    ../containers.c \
    ../util.c \

LOCAL_CFLAGS += -DMR_LOG_TAG=\"trampoline_encmnt\"

include $(BUILD_EXECUTABLE)
