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

$(MULTIROM_ZIP_TARGET): multirom trampoline fw_mounter signapk bbootimg
	@echo
	@echo
	@echo "A crowdfunding campaign for MultiROM took place in 2013. These people got perk 'The Tenth':"
	@echo "    * Bibi"
	@echo "    * flash5000"
	@echo "Thank you. See DONORS.md in MultiROM's folder for more informations."
	@echo
	@echo

	@echo ----- Making MultiROM ZIP installer ------
	rm -rf $(MULTIROM_INST_DIR)
	mkdir -p $(MULTIROM_INST_DIR)
	cp -a $(install_zip_path)/prebuilt-installer/* $(MULTIROM_INST_DIR)/
	cp -a $(TARGET_ROOT_OUT)/multirom $(MULTIROM_INST_DIR)/multirom/
	cp -a $(TARGET_ROOT_OUT)/trampoline $(MULTIROM_INST_DIR)/multirom/
	cp -a $(TARGET_ROOT_OUT)/fw_mounter $(MULTIROM_INST_DIR)/multirom/
	mkdir $(MULTIROM_INST_DIR)/multirom/infos
	if [ -n "$(MR_INFOS)" ]; then cp -r $(PWD)/$(MR_INFOS)/* $(MULTIROM_INST_DIR)/multirom/infos/; fi
	cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/bbootimg $(MULTIROM_INST_DIR)/scripts/
	cp $(PWD)/$(MR_FSTAB) $(MULTIROM_INST_DIR)/multirom/mrom.fstab
	$(install_zip_path)/extract_boot_dev.sh $(PWD)/$(MR_FSTAB) $(MULTIROM_INST_DIR)/scripts/bootdev
	echo $(MR_RD_ADDR) > $(MULTIROM_INST_DIR)/scripts/rd_addr
	echo "$(MR_USE_MROM_FSTAB)" > $(MULTIROM_INST_DIR)/scripts/use_mrom_fstab
	$(install_zip_path)/make_updater_script.sh $(TARGET_DEVICE) $(MULTIROM_INST_DIR)/META-INF/com/google/android "Installing MultiROM for"
	rm -f $(MULTIROM_ZIP_TARGET).zip $(MULTIROM_ZIP_TARGET)-unsigned.zip
	cd $(MULTIROM_INST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	java -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_ZIP_TARGET)-unsigned.zip $(MULTIROM_ZIP_TARGET).zip
	$(install_zip_path)/rename_zip.sh $(MULTIROM_ZIP_TARGET) $(TARGET_DEVICE) $(PWD)/$(multirom_local_path)/version.h
	@echo ----- Made MultiROM ZIP installer -------- $@.zip

.PHONY: multirom_zip
multirom_zip: $(MULTIROM_ZIP_TARGET)



MULTIROM_UNINST_TARGET := $(PRODUCT_OUT)/multirom_uninstaller
MULTIROM_UNINST_DIR := $(PRODUCT_OUT)/multirom_uninstaller

$(MULTIROM_UNINST_TARGET): signapk bbootimg
	@echo ----- Making MultiROM uninstaller ------
	rm -rf $(MULTIROM_UNINST_DIR)
	mkdir -p $(MULTIROM_UNINST_DIR)
	cp -a $(install_zip_path)/prebuilt-uninstaller/* $(MULTIROM_UNINST_DIR)/
	cp -a $(TARGET_OUT_OPTIONAL_EXECUTABLES)/bbootimg $(MULTIROM_UNINST_DIR)/scripts/
	$(install_zip_path)/extract_boot_dev.sh $(PWD)/$(MR_FSTAB) $(MULTIROM_UNINST_DIR)/scripts/bootdev
	echo $(MR_RD_ADDR) > $(MULTIROM_UNINST_DIR)/scripts/rd_addr
	$(install_zip_path)/make_updater_script.sh $(TARGET_DEVICE) $(MULTIROM_UNINST_DIR)/META-INF/com/google/android "MultiROM uninstaller -"
	rm -f $(MULTIROM_UNINST_TARGET).zip $(MULTIROM_UNINST_TARGET)-unsigned.zip
	cd $(MULTIROM_UNINST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	java -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_UNINST_TARGET)-unsigned.zip $(MULTIROM_UNINST_TARGET).zip
	@echo ----- Made MultiROM uninstaller -------- $@.zip

.PHONY: multirom_uninstaller
multirom_uninstaller: $(MULTIROM_UNINST_TARGET)
