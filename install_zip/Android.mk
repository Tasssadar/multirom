LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

install_zip_path := $(multirom_local_path)/install_zip

MULTIROM_ZIP_TARGET := $(PRODUCT_OUT)/multirom
MULTIROM_INST_DIR := $(PRODUCT_OUT)/multirom_installer
multirom_binary := $(TARGET_ROOT_OUT)/multirom
trampoline_binary := $(TARGET_ROOT_OUT)/trampoline

# include device file
-include $(multirom_local_path)/device_$(TARGET_DEVICE).mk

$(MULTIROM_ZIP_TARGET): multirom trampoline signapk
	@echo ----- Making MultiROM ZIP installer ------
	rm -rf $(MULTIROM_INST_DIR)
	mkdir -p $(MULTIROM_INST_DIR)
	cp -a $(install_zip_path)/prebuilt-installer/* $(MULTIROM_INST_DIR)/
	cp -a $(TARGET_ROOT_OUT)/multirom $(MULTIROM_INST_DIR)/multirom/
	cp -a $(TARGET_ROOT_OUT)/trampoline $(MULTIROM_INST_DIR)/multirom/
	echo $(BOOT_DEV) > $(MULTIROM_INST_DIR)/scripts/bootdev
	$(install_zip_path)/make_updater_script.sh $(TARGET_DEVICE) $(MULTIROM_INST_DIR)/META-INF/com/google/android "Installing MultiROM for"
	rm -f $(MULTIROM_ZIP_TARGET).zip $(MULTIROM_ZIP_TARGET)-unsigned.zip
	cd $(MULTIROM_INST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	java -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_ZIP_TARGET)-unsigned.zip $(MULTIROM_ZIP_TARGET).zip
	@echo ----- Made MultiROM ZIP installer -------- $@.zip

.PHONY: multirom_zip
multirom_zip: $(MULTIROM_ZIP_TARGET)



MULTIROM_UNINST_TARGET := $(PRODUCT_OUT)/multirom_uninstaller
MULTIROM_UNINST_DIR := $(PRODUCT_OUT)/multirom_uninstaller

$(MULTIROM_UNINST_TARGET): signapk
	@echo ----- Making MultiROM uninstaller ------
	rm -rf $(MULTIROM_UNINST_DIR)
	mkdir -p $(MULTIROM_UNINST_DIR)
	cp -a $(install_zip_path)/prebuilt-uninstaller/* $(MULTIROM_UNINST_DIR)/
	echo $(BOOT_DEV) > $(MULTIROM_UNINST_DIR)/scripts/bootdev
	$(install_zip_path)/make_updater_script.sh $(TARGET_DEVICE) $(MULTIROM_UNINST_DIR)/META-INF/com/google/android "MultiROM uninstaller -"
	rm -f $(MULTIROM_UNINST_TARGET).zip $(MULTIROM_UNINST_TARGET)-unsigned.zip
	cd $(MULTIROM_UNINST_DIR) && zip -qr ../$(notdir $@)-unsigned.zip *
	java -jar $(HOST_OUT_JAVA_LIBRARIES)/signapk.jar $(DEFAULT_SYSTEM_DEV_CERTIFICATE).x509.pem $(DEFAULT_SYSTEM_DEV_CERTIFICATE).pk8 $(MULTIROM_UNINST_TARGET)-unsigned.zip $(MULTIROM_UNINST_TARGET).zip
	@echo ----- Made MultiROM uninstaller -------- $@.zip

.PHONY: multirom_uninstaller
multirom_uninstaller: $(MULTIROM_UNINST_TARGET)
