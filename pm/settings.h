/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PM_SETTINGS_H
#define PM_SETTINGS_H

#include <libxml/hash.h>
#include <libxml/dict.h>

#include "../containers.h"

struct pkg_key_set_data
{
    int64_t *signingKeySets;
    int signingKeySetsSize;
    int64_t *definedKeySets;
    int definedKeySetsSize;
    xmlHashTable keySetAliases;
};

struct pkg_key_set_data *pkg_key_set_data_create(void);
void pkg_key_set_data_destroy(struct pkg_key_set_data *pksd);
struct pkg_key_set_data *pkg_key_set_data_clone(struct pkg_key_set_data *base);
void pkg_key_set_data_add_signing_key_set(struct pkg_key_set_data *pksd, int64_t ks);
void pkg_key_set_data_add_defined_key_set(struct pkg_key_set_data *pksd, int64_t ks, const xmlChar *alias);

struct signature
{
    int8_t *signature;
    int hashCode;
    int haveHashCode;  
};

struct signature *signature_from_text(xmlChar *text);
void signature_destroy(struct signature *sig);

struct pkg_signatures
{
    struct signature **signatures;
};

struct pkg_signatures* pkg_signatures_create(void);
void pkg_signatures_destroy(struct pkg_signatures *psig);
int pkg_signatures_read(struct pkg_signatures *psig, xmlNode *node, struct signatures ***pastSignatures);

struct pkg_user_state
{
    int stopped;
    int notLaunched;
    int installed;
    int blocked;
    int enabled;

    char *lastDisableAppCaller;

    xmlHashTable *disabledComponents;
    xmlHashTable *enabledComponents;
};

struct pkg_user_state *pkg_user_state_create(void);
void pkg_user_state_destroy(struct pkg_user_state *st);
struct pkg_user_state *pkg_user_state_clone(struct pkg_user_state *base);


struct shared_user_setting
{
    char *name;
    int userId;
    int uidFlags;
    int pkgFlags;
    xmlHashTable *grantedPermissions;
    int *gids;
    int gids_size;
    struct pkg_setting **packages;
    struct signatures *signatures;
};

struct shared_user_setting *shared_user_setting_create(const char *name, int pkgFlags);
void shared_user_setting_destroy(struct shared_user_setting *sus);
void shared_user_setting_set_flags(struct shared_user_setting *sus, int pkgFlags);
void shared_user_setting_add_package(struct shred_user_setting *sus, struct pkg_setting *pkg);

struct pkg_setting
{
    char *name;
    char *realName;
    char *codePath;
    char *resourcePath;
    char *nativeLibraryPath;
    char *installerPackageName;
    int versionCode;
    int pkgFlags;
    xmlHashTable *grantedPermissions;
    int appId;
    int sharedId;
    int64_t timeStamp;
    int64_t firstInstallTime;
    int64_t lastUpdateTime;
    int uidError;
    imap *userState;
    int installStatus;
    struct pkg_signatures *signatures;
    int permissionsFixed;
    struct pkg_key_set_data *keySetData;
    struct shared_user_setting *sharedUser;
    struct pkg_setting *origPackage;
    int *gids;
    int gids_size;
    int haveGids;
};

struct pkg_setting *pkg_setting_create(const char *name, const char *realName, const char *codePath, const char *resourcePath, const char *nativeLibraryPath, int pVersionCode, int pkgFlags);
void pkg_setting_set_flags(struct pkt_setting *ps, int pkgFlags);
void pkg_setting_destroy(struct pkt_setting *ps);
void pkg_setting_set_enabled(struct pkg_setting *ps, int state, int userId, xmlChar *callingPackage);
void pkg_setting_add_disabled_component(struct pkg_setting *ps, const xmlChar *name, int userId);
void pkg_setting_add_enabled_component(struct pkg_setting *ps, const xmlChar *name, int userId);
xmlHashTable *pkg_setting_get_enabled_components(struct pkg_setting *ps, int userId);
xmlHashTable *pkg_setting_get_disabled_components(struct pkg_setting *ps, int userId);
void pkg_setting_set_enabled_components_copy(struct pkg_setting *ps, xmlHashTable *components, int userId);
void pkg_setting_set_disabled_components_copy(struct pkg_setting *ps, xmlHashTable *components, int userId);


struct perm_info
{
    char *packageName;
    char *name;
    int icon;
    char *nonLocalizedLabel;
    int protectionLevel;
};

struct perm_info *perm_info_create(void);
void perm_info_destroy(struct perm_info *pi);

struct base_perm
{
    char *name;
    char *sourcePackage;
    struct pkg_setting *packageSetting;
    int type;
    int protectionLevel;
    int uid;
    int *gids;
    int gids_size;
    struct perm_info *pendingInfo;
};

struct base_perm *base_perm_create(const char *name, const char *sourcePackage, int type);
void base_perm_destroy(struct base_perm *perm);
int base_perm_fix_protection_level(int level);

struct pkg_clean_item
{
    int userId;
    char *packageName;
    int andCode;
};

struct pkg_clean_item pkg_clean_item_create(int userId, const char *packageName, int andCode);
void pkg_clean_item_destroy(struct pkg_clean_item *pci);
void pkg_clean_item_equals(struct pkg_clean_item *pci1, struct pkg_clean_item *pci2);

struct verifier_device_id
{
    int64_t identity;
    char *identityString;
};

struct verifier_device_id *verifier_device_id_parse(const xmlChar *text);
void verifier_device_id_destroy(struct verifier_device_id *vdi);

struct key_set_mgr
{
    i64map *key_sets;
    i64map *public_keys;
    i64map *key_set_mapping;
    xmlHashTable *packages;
    int64_t lastIssuedKeySetId;
    int64_t lastIssuedKeyId;
};

struct key_set_mgr *key_set_mgr_create(xmlHashTable *packages);
void key_set_mgr_destroy(struct key_set_mgr *mgr);
void key_set_mgr_read(struct key_set_mgr *mgr, xmlNode *node);

struct pm_settings {
    xmlHashTable *packages;
    xmlHashTable *disabled_packages;
    struct pkg_setting **pending_packages;
    xmlHashTable *shared_users;
    void **user_ids;
    imap *other_user_ids;
    struct signature **past_signatures;
    xmlHashTable *permissions;
    xmlHashTable *permission_trees;
    xmlNode **preferred_activities;
    struct pkg_clean_item **packages_to_clean;
    xmlHashTable *renamed_packages;
    int internal_sdk_platform;
    int external_sdk_platform;
    struct verifier_device_id *verifier_device_id;
    int read_external_storage_enforced;
    struct key_set_mgr *key_set_mgr;
    int first_available_uid;
};

struct pm_settings *pm_settings_create(void);
void pm_settings_destroy(struct pm_settings *s);
int pm_settings_read(struct pm_settings *s, const char *path);

#endif
