LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

install_zip_path := $(multirom_local_path)/install_zip

MULTIROM_ZIP_TARGET := $(PRODUCT_OUT)/multirom
MULTIROM_INST_DIR := $(PRODUCT_OUT)/multirom_installer
multirom_binary := $(TARGET_ROOT_OUT)/multirom
trampoline_binary := $(TARGET_ROOT_OUT)/trampoline

ifeq ($(MR_FSTAB),)
    $(info MR_FSTAB not defined in device files)
endif

multirom_extra_dep :=
ifeq ($(MR_ENCRYPTION),true)
	multirom_extra_dep += trampoline_encmnt linker

	multirom_cp_enc_libs := \
		libcryptfslollipop.so libcrypto.so libc.so libcutils.so \
		libdl.so libhardware.so liblog.so libm.so libstdc++.so \
		libc++.so

	ifeq ($(TARGET_HW_DISK_ENCRYPTION),true)
		multirom_cp_enc_libs += \
			libunwind.so libbase.so libbacktrace.so \
			libutils.so libcryptfs_hw.so
	endif

	ifeq ($(MR_ENCRYPTION_FAKE_PROPERTIES),true)
		multirom_extra_dep += libmultirom_fake_properties
		multirom_cp_enc_libs += libmultirom_fake_properties.so
	endif
else
	MR_ENCRYPTION := false
endif

MR_DEVICES := $(TARGET_DEVICE)
ifneq ($(MR_DEVICE_VARIANTS),)
	MR_DEVICES += $(MR_DEVICE_VARIANTS)
endif

$(MULTIROM_ZIP_TARGET): multirom trampoline signapk bbootimg mrom_kexec_static mrom_adbd $(multirom_extra_dep)
	@echo
	@echo
	@echo "A crowdfunding campaign for MultiROM took place in 2013. These people got perk 'The Tenth':"
	@echo "    * Bibi"
	@echo "    * flash5000"
	@echo "Thank you. See DONORS.md in MultiROM's folder for more informations."
	@echo
	@echo

	@echo ----- Making MultiROM ZIP installer ------
	@rm -rf $(MULTIROM_INST_DIR)
	@mkdir -p $(MULTIROM_INST_DIR)
	@echo Copying primary files
	@cp -a $(install_zip_path)/prebuilt-installer/* $(MULTIROM_INST_DIR)/
	@cp -a $(TARGET_ROOT_OUT)/multirom $(MULTIROM_INST_DIR)/multirom/
	@cp -a $(TARGET_ROOT_OUT)/trampoline $(MULTIROM_INST_DIR)/multirom/
	@cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/mrom_kexec_static $(MULTIROM_INST_DIR)/multirom/kexec
	@cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/mrom_adbd $(MULTIROM_INST_DIR)/multirom/adbd

	@if $(MR_ENCRYPTION); then \
		echo "Copying decryption files"; \
		mkdir -p $(MULTIROM_INST_DIR)/multirom/enc/res; \
		cp -a $(TARGET_ROOT_OUT)/trampoline_encmnt $(MULTIROM_INST_DIR)/multirom/enc/; \
		if [ "$(TARGET_IS_64_BIT)" == "true" ]; then \
			cp -a $(TARGET_OUT_EXECUTABLES)/linker64 $(MULTIROM_INST_DIR)/multirom/enc/linker; \
		else \
			cp -a $(TARGET_OUT_EXECUTABLES)/linker $(MULTIROM_INST_DIR)/multirom/enc/; \
		fi; \
		cp -a $(install_zip_path)/prebuilt-installer/multirom/res/Roboto-Regular.ttf $(MULTIROM_INST_DIR)/multirom/enc/res/; \
		\
		for f in $(multirom_cp_enc_libs); do \
			if [ -f "$(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/$$f" ]; then \
				cp -a $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/$$f $(MULTIROM_INST_DIR)/multirom/enc/; \
			else \
				cp -a $(TARGET_OUT_SHARED_LIBRARIES)/$$f $(MULTIROM_INST_DIR)/multirom/enc/; \
			fi; \
		done; \
		if [ -n "$(MR_ENCRYPTION_SETUP_SCRIPT)" ]; then sh "$(ANDROID_BUILD_TOP)/$(MR_ENCRYPTION_SETUP_SCRIPT)" "$(ANDROID_BUILD_TOP)" "$(MULTIROM_INST_DIR)/multirom/enc"; fi; \
	fi

	@echo Copying info files
	@mkdir $(MULTIROM_INST_DIR)/multirom/infos
	@if [ -n "$(MR_INFOS)" ]; then cp -vr $(PWD)/$(MR_INFOS)/* $(MULTIROM_INST_DIR)/multirom/infos/; fi
	@echo Copying scripts
	@cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/bbootimg $(MULTIROM_INST_DIR)/scripts/
	@cp $(PWD)/$(MR_FSTAB) $(MULTIROM_INST_DIR)/multirom/mrom.fstab
	@echo Preparing installer script
	@$(install_zip_path)/extract_boot_dev.sh $(PWD)/$(MR_FSTAB) $(MULTIROM_INST_DIR)/scripts/bootdev
	@$(install_zip_path)/make_updater_script.sh "$(MR_DEVICES)" $(MULTIROM_INST_DIR)/META-INF/com/google/android "Installing MultiROM for"
	@echo Signing flashable zip
	@rm -f $(MULTIROM_ZIP_TARGET).zip $(MULTIROM_ZIP_TARGET)-unsigned.zip
	@cd $(MULTIROM_INST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	@java -Djava.library.path=$(SIGNAPK_JNI_LIBRARY_PATH) -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_ZIP_TARGET)-unsigned.zip $(MULTIROM_ZIP_TARGET).zip
	@$(install_zip_path)/rename_zip.sh $(MULTIROM_ZIP_TARGET) $(TARGET_DEVICE) $(PWD)/$(multirom_local_path)/version.h $(MR_DEVICE_SPECIFIC_VERSION)
	@echo ----- Made MultiROM ZIP installer -------- $@.zip

.PHONY: multirom_zip
multirom_zip: $(MULTIROM_ZIP_TARGET)



MULTIROM_UNINST_TARGET := $(PRODUCT_OUT)/multirom_uninstaller
MULTIROM_UNINST_DIR := $(PRODUCT_OUT)/multirom_uninstaller

$(MULTIROM_UNINST_TARGET): signapk bbootimg
	@echo ----- Making MultiROM uninstaller ------
	@rm -rf $(MULTIROM_UNINST_DIR)
	@mkdir -p $(MULTIROM_UNINST_DIR)
	@echo Copying files
	@cp -a $(install_zip_path)/prebuilt-uninstaller/* $(MULTIROM_UNINST_DIR)/
	@cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/bbootimg $(MULTIROM_UNINST_DIR)/scripts/
	@echo Preparing installer script
	@$(install_zip_path)/extract_boot_dev.sh $(PWD)/$(MR_FSTAB) $(MULTIROM_UNINST_DIR)/scripts/bootdev
	@echo $(MR_RD_ADDR) > $(MULTIROM_UNINST_DIR)/scripts/rd_addr
	@$(install_zip_path)/make_updater_script.sh "$(MR_DEVICES)" $(MULTIROM_UNINST_DIR)/META-INF/com/google/android "MultiROM uninstaller -"
	@echo Signing flashable zip
	@rm -f $(MULTIROM_UNINST_TARGET).zip $(MULTIROM_UNINST_TARGET)-unsigned.zip
	@cd $(MULTIROM_UNINST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	@java -Djava.library.path=$(SIGNAPK_JNI_LIBRARY_PATH) -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_UNINST_TARGET)-unsigned.zip $(MULTIROM_UNINST_TARGET).zip
	@echo ----- Made MultiROM uninstaller -------- $@.zip

.PHONY: multirom_uninstaller
multirom_uninstaller: $(MULTIROM_UNINST_TARGET)



KERNEL_ZIP_TARGET := $(PRODUCT_OUT)/multirom_kernel
KERNEL_INST_DIR := $(PRODUCT_OUT)/multirom_kernel_installer

$(KERNEL_ZIP_TARGET): $(PRODUCT_OUT)/kernel kernel_inject signapk
	@echo ----- Making MultiROM Kernel ZIP installer ------
	rm -rf $(KERNEL_INST_DIR)
	mkdir -p $(KERNEL_INST_DIR)
	cp -a $(install_zip_path)/prebuilt-kernel-installer/* $(KERNEL_INST_DIR)/
	cp -a $(TARGET_ROOT_OUT)/kernel_inject $(KERNEL_INST_DIR)/scripts/
	cp -a $(PRODUCT_OUT)/kernel $(KERNEL_INST_DIR)/scripts/kernel
	mkdir -p $(KERNEL_INST_DIR)/system/lib
	cp -a $(PRODUCT_OUT)/system/lib/modules $(KERNEL_INST_DIR)/system/lib/
	$(install_zip_path)/extract_boot_dev.sh $(PWD)/$(MR_FSTAB) $(KERNEL_INST_DIR)/scripts/bootdev
	$(install_zip_path)/make_updater_script.sh "$(MR_DEVICES)" $(KERNEL_INST_DIR)/META-INF/com/google/android "Installing Kernel for"
	rm -f $(KERNEL_ZIP_TARGET).zip $(KERNEL_ZIP_TARGET)-unsigned.zip
	cd $(KERNEL_INST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	java -Djava.library.path=$(SIGNAPK_JNI_LIBRARY_PATH) -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(KERNEL_ZIP_TARGET)-unsigned.zip $(KERNEL_ZIP_TARGET).zip
	$(install_zip_path)/rename_zip.sh $(KERNEL_ZIP_TARGET) $(TARGET_DEVICE) $(PWD)/$(multirom_local_path)/version.h
	@echo ----- Made MultiROM Kernel ZIP installer -------- $@.zip

.PHONY: multirom_kernel_zip
multirom_kernel_zip: $(KERNEL_ZIP_TARGET)
