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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlstring.h>
#include <libxml/hash.h>
#include <libxml/dict.h>

#include "settings.h"
#include "../log.h"
#include "../util.h"
#include "../xmlutils.h"
#include "applicationinfo.h"
#include "defines.h"

#define SC (char*)
#define UC (unsigned char*)

#define LOGV(x...) INFO(x)
#define LOGE(x...) ERROR(x)

static uint8_t USERID_NULL_OBJECT = 0;
static uint8_t PAST_SIGS_NULL = 0;

struct pkg_key_set_data *pkg_key_set_data_create(void)
{
    struct pkg_key_set_data *pksd = mzalloc(sizeof(struct pkg_key_set_data));
    pksd->keySetAliases = xmlHashCreate(16);
    return pksd;
}

void pkg_key_set_data_destroy(struct pkg_key_set_data *pksd)
{
    xmlHashFree(pksd->keySetAliases, NULL);
    free(pksd->signingKeySets);
    free(pksd->definedKeySets);
    free(pksd);
}

void pkg_key_set_data_add_signing_key_set(struct pkg_key_set_data *pksd, int64_t ks)
{
    int i;
    for(i = 0; i < pksd->signingKeySetsSize; ++i)
        if(pksd->signingKeySets[i] == ks)
            return;
    pksd->signingKeySets = realloc(pksd->signingKeySets, sizeof(int64_t)*(pksd->signingKeySetsSize+1));
    pksd->signingKeySets[pksd->signingKeySetsSize++] = ks;
}

void pkg_key_set_data_add_defined_key_set(struct pkg_key_set_data *pksd, int64_t ks, const xmlChar *alias)
{
    int i;
    for(i = 0; i < pksd->definedKeySetsSize; ++i)
        if(pksd->definedKeySets[i] == ks)
            return;
    pksd->definedKeySets = realloc(pksd->definedKeySets, sizeof(int64_t)*(pksd->definedKeySetsSize+1));
    pksd->definedKeySets[pksd->definedKeySetsSize] = ks;
    xmlHashAddEntry(pksd->keySetAliases, alias, &pksd->definedKeySets[pksd->definedKeySetsSize]);
    ++pksd->definedKeySetsSize;
}

static int8_t signature_parse_hex_digit(xmlChar c)
{
    if('0' <= c && c <= '9')
        return c - '0';
    else if('a' <= c && c <= 'f')
        return c - 'a' + 10;
    else if('A' <= c && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

struct signature *signature_from_text(xmlChar *text)
{
    int len = xmlStrlen(text);

    if(len % 2 != 0)
    {
        LOGE("signature text size %d is not even", len);
        return NULL;
    }

    struct signature *sig = mzalloc(sizeof(struct signature));
    sig->signature = malloc(len/2);

    int i, sigIndex = 0;
    for(i = 0; i < len; )
    {
        const int8_t hi = signature_parse_hex_digit(text[i++]);
        const int8_t lo = signature_parse_hex_digit(text[i++]);
        sig->signature[sigIndex++] = ((hi << 4) | lo);
    }
    return sig;
}

void signature_destroy(struct signature *sig)
{
    free(sig->signature);
    free(sig);
}


struct pkg_signatures* pkg_signatures_create(void)
{
    struct pkg_signatures *psig = mzalloc(sizeof(struct pkg_signatures));
    return psig;
}

void pkg_signatures_destroy(struct pkg_signatures *psig)
{
    list_clear(&psig->signatures, signature_destroy);
    free(psig);
}

int pkg_signatures_read(struct pkg_signatures *psig, xmlNode *node, struct signatures ***pastSignatures)
{
    xmlChar *countStr = xml_get_prop(node, "count");
    if(!countStr)
    {
        LOGE("Error in package manager settings: <signatures> has no count!\n");
        return -1;
    }

    int count = atoi(countStr);
    int pos = 0;
    xmlNode *child;

    list_clear(&psig->signatures, signature_destroy);
    psig->signatures = mzalloc(sizeof(struct signature*)*(count+1));

    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
                continue;

        if(xml_str_equal(child->name, "cert"))
        {
            if(pos < count)
            {
                xmlChar *index = xml_get_prop(child, "index");
                if(index)
                {
                    int idx = atoi(SCindex);
                    xmlChar *key = xml_get_prop(child, "key");
                    int pastSignaturesSize = list_item_count(*pastSignatures);
                    if(!key)
                    {
                        if(idx >= 0 && idx < pastSignaturesSize)
                        {
                            if((*pastSignatures)[idx] != &PAST_SIGS_NULL)
                                psig->signatures[pos++] = (*pastSignatures)[idx];
                            else
                                LOGE("Error in package manager settings: <cert> index %s is not defined!\n", index);
                        }
                        else
                        {
                            LOGE("Error in package manager settings: <cert> index %s is out of bounds!\n", index);   
                        }
                    }
                    else
                    {
                        struct signature *sig = signature_from_text((int8_t*)key);

                        if(sig)
                        {
                            while(pastSignaturesSize <= idx) {
                                list_add(pastSignatures, &PAST_SIGS_NULL);
                                ++pastSignaturesSize;
                            }

                            (*pastSignatures)[idx] = sig;
                            psig->signatures[pos++] = sig;
                        }
                        xmlFree(key);
                    }
                    xmlFree(index);
                }
                else
                    LOGE("Error in package manager settings: <cert> has no index!\n");   
            }
            else
                LOGE("Error in package manager settings: too many <cert> tags, expected %d!\n", count);      
        }
    }

    if(pos < count)
    {
        psig->signatures = realloc(psig->signatures, sizeof(struct signature*)*(pos+1));
        psig->signatures[pos] = NULL;
    }
    xmlFree(countStr);
    return 0;
}


struct pkg_user_state *pkg_user_state_create(void)
{
    struct pkg_user_state *st = mzalloc(sizeof(struct pkg_user_state));
    st->installed = 1;
    st->blocked = 0;
    st->enabled = COMPONENT_ENABLED_STATE_DEFAULT;
    st->disabledComponents = xmlDictCreate();
    st->enabledComponents = xmlDictCreate();
    return st;
}

void pkg_user_state_destroy(struct pkg_user_state *st)
{
    xmlDictFree(st->disabledComponents);
    xmlDictFree(st->enabledComponents);
    free(st->lastDisableAppCaller);
    free(st);
}

struct pkg_setting *pkg_setting_create(const char *name, const char *realName,
        const char *codePath, const char *resourcePath, const char *nativeLibraryPath,
        int pVersionCode, int pkgFlags)
{
    struct pkt_setting *ps = mzalloc(sizeof(struct pkg_setting));
    ps->name = strdup_safe(name);
    ps->realName = strdup_safe(realName);
    ps->codePath = strdup_safe(codePath);
    ps->resourcePath = strdup_safe(codePathStr);
    ps->nativeLibraryPath = strdup_safe(nativeLibraryPath);
    ps->versionCode = pVersionCode;
    pkg_setting_set_flags(ps, pkgFlags);

    ps->grantedPermissions = xmlDictCreate();
    ps->userState = imap_create();
    ps->signatures = pkg_signatures_create();
    ps->keySetData = pkg_key_set_data_create();
    return ps;
}

void pkg_setting_destroy(struct pkt_setting *ps)
{
    free(ps->name);
    free(ps->realName);
    free(ps->codePath);
    free(ps->resourcePath);
    free(ps->nativeLibraryPath);
    free(ps->installerPackageName);
    xmlDictFree(ps->grantedPermissions);
    imap_destroy(ps->userState, pkg_user_state_destroy);
    pkg_signatures_destroy(ps->signatures);
    pkg_key_set_data_destroy(ps->keySetData);
    free(ps);
}

void pkg_setting_set_flags(struct pkt_setting *ps, int pkgFlags)
{
    ps->pkgFlags = pkgFlags & (FLAG_SYSTEM | FLAG_PRIVILEGED | FLAG_FORWARD_LOCK | FLAG_EXTERNAL_STORAGE);
}

static struct pkg_user_state *pkg_setting_modify_user_state(struct pkg_setting *ps, int userId)
{
    struct pkg_user_state *st = imap_get_val(ps->userState, userId);
    if(!st)
    {
        st = pkg_user_state_create();
        imap_add_not_exist(ps->userState, userId, st);
    }
    return st;        
}

void pkg_setting_set_enabled(struct pkg_setting *ps, int state, int userId, xmlChar *callingPackage)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    st->enabled = state;
    st->lastDisableAppCaller = strset(st->lastDisableAppCaller, (char*)callingPackage);
}

void pkg_setting_add_disabled_component(struct pkg_setting *ps, const xmlChar *name, int userId)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    xmlDictLookup(st->disabledComponents, name, -1);
}

void pkg_setting_add_enabled_component(struct pkg_setting *ps, const xmlChar *name, int userId)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    xmlDictLookup(st->enabledComponents, name, -1);
}


struct perm_info *perm_info_create(void)
{
    struct perm_info *pi = mzalloc(sizeof(struct perm_info));
    return pi;
}

void perm_info_destroy(struct perm_info *pi)
{
    free(pi->name);
    free(pi->packageName);
    free(pi->nonLocalizedLabel);
    free(pi);
}


struct base_perm *base_perm_create(const char *name, const char *sourcePackage, int type)
{
    struct base_perm *perm = mzalloc(sizeof(struct base_perm));
    perm->name = strdup_safe(name);
    perm->sourcePackage = strdup_safe(sourcePackage);
    perm->type = type;
    perm->protectionLevel = PROTECTION_SIGNATURE;
    return perm;
}

void base_perm_destroy(struct base_perm *perm)
{
    if(perm->pendingInfo)
        perm_info_destroy(perm->pendingInfo);
    free(perm->name);
    free(perm->sourcePackage);
    free(perm);
}

int base_perm_fix_protection_level(int level)
{
    if(level == PROTECTION_SIGNATURE_OR_SYSTEM)
        level = PROTECTION_SIGNATURE | PROTECTION_FLAG_SYSTEM;
    return level;
}

struct shared_user_setting *shared_user_setting_create(const char *name, int pkgFlags)
{
    struct shared_user_setting *ses = mzalloc(sizeof(struct shared_user_setting));
    ses->uidFlags = pkgFlags;
    ses->name = strdup_safe(name);
    ses->signatures = pkg_signatures_create();
    ses->grantedPermissions = xmlDictCreate();
    shared_user_setting_set_flags(pkgFlags);
    return ses;
}

void shared_user_setting_destroy(struct shared_user_setting *sus)
{
    xmlDictFree(ses->grantedPermissions);
    pkg_signatures_destroy(ses->signatures);
    free(ses->name);
    free(ses);
}

void shared_user_setting_set_flags(struct shared_user_setting *sus, int pkgFlags)
{
    ses->pkgFlags = pkgFlags & (FLAG_SYSTEM | FLAG_PRIVILEGED | FLAG_FORWARD_LOCK | FLAG_EXTERNAL_STORAGE);
}


struct pm_settings *pm_settings_create(void)
{
    struct pm_settings *s = mzalloc(sizeof(struct pm_settings));
    s->packages = xmlHashCreate(0);
    s->shared_users = xmlHashCreate(16);
    s->other_user_ids = imap_create();
    s->permissions = xmlHashCreate(16);
    s->permission_trees = xmlHashCreate(16);
    return s;
}

static void pkg_setting_deallocator(void *payload, xmlChar *name)
{
    pkg_setting_destroy(payload);
}

static void base_perm_deallocator(void *payload, xmlChar *name)
{
    base_perm_destroy(payload);
}

static void shared_user_setting_deallocator(void *payload, xmlChar *name)
{
    shared_user_setting_destroy(payload);
}

void pm_settings_destroy(struct pm_settings *s)
{
    xmlHashDestroy(s->packages, pkg_setting_deallocator);
    list_clear(&s->pending_packages, pkg_setting_destroy);
    list_clear(&s->user_ids, NULL);
    imap_destroy(s->other_user_ids, NULL);
    xmlHashFree(s->shared_users, shared_user_setting_deallocator);
    list_clear(&s->past_signatures, NULL);
    xmlHashDestroy(s->permissions, base_perm_deallocator);
    xmlHashDestroy(s->permission_trees, base_perm_deallocator);
    free(s);
}

static int pm_settings_add_user_id(struct pm_settings *s, int uid, void *obj, const xmlChar *name)
{
    if(uid > LAST_APPLICATION_UID)
        return 0;

    if(uid >= FIRST_APPLICATION_UID)
    {
        int N = list_item_count(s->user_ids);
        const int index = uid - FIRST_APPLICATION_UID;
        while(index >= N)
        {
            list_add(&s->user_ids, &USERID_NULL_OBJECT);
            ++N;
        }

        if(s->user_ids[index] != &USERID_NULL_OBJECT)
        {
            LOGE("Adding duplicate user id: %d name=%s\n", uid, name);
            return 0;
        }
        s->user_ids[index] = obj;
    }
    else
    {
        if(imap_get_val(s->other_user_ids, uid))
        {
            LOGE("Adding duplicate shared id: %d name=%s\n", uid, name);
            return 0;
        }
        imap_add_not_exist(s->other_user_ids, uid, obj);
    }
    return 1;
}

// Settings.java:314
static struct pkg_setting *pm_settings_add_package(struct pm_settings *s, const xmlChar *name, const xmlChar *realName, const xmlChar *codePath, const xmlChar *resourcePath, const xmlChar *nativeLibraryPathString, int uid, int vc, int pkgFlags)
{
    struct pkg_setting *ps = xmlHashLookup(s->packages, name);
    if(ps)
    {
        if(ps->appId == uid)
            return ps;
        LOGE("Adding duplicate package, keeping first: %s\n", name);
        return NULL;
    }

    ps = pkg_setting_create(name, realName, codePath, resourcePath, nativeLibraryPath, vc, pkgFlags);
    ps->appId = uid;
    if(pm_settings_add_user_id(s, uid, ps, name))
    {
        xmlHashAddEntry(s->packages, name, ps);
        return ps;
    }
    pkg_setting_destroy(ps);
    return NULL;
}

static void pm_settings_read_disabled_components(struct pm_settings *s, struct pkg_setting *ps, xmlNode *node, int userId)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "item"))
        {
            xmlChar *name = xml_get_prop(child, "name");
            if(name)
                pkg_setting_add_disabled_component(ps, name, userId);
            else
                LOGE("Error in package manager settings: <disabled-components> has no name\n");
        }
        else
            LOGE("Error in package manager settings: unknown element under <disabled-components>: %s\n", child->name);
    }
}

static void pm_settings_read_enabled_components(struct pm_settings *s, struct pkg_setting *ps, xmlNode *node, int userId)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "item"))
        {
            xmlChar *name = xml_get_prop(child, "name");
            if(name)
                pkg_setting_add_disabled_component(ps, name, userId);
            else
                LOGE("Error in package manager settings: <enabled-components> has no name\n");
        }
        else
            LOGE("Error in package manager settings: unknown element under <enabled-components>: %s\n", child->name);
    }
}

static void pm_settings_read_granted_permissions(struct pm_settings *s, xmlNode *node, xmlDict *outPerms)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "item"))
        {
            xmlChar *name = xml_get_prop(child, "name");
            if(name)
                xmlDictLookup(outPerms, name, -1);
            else
                LOGE("Error in package manager settings: <perms> has no name\n");
        }
        else
            LOGE("Error in package manager settings: unknown element under <perms>: %s\n", child->name);
    }
}

static int pm_settings_read_package(struct pm_settings *s, xmlNode *node)
{
    xmlChar *name = NULL;
    xmlChar *realName = NULL;
    xmlChar *idStr = NULL;
    xmlChar *sharedIdStr = NULL;
    xmlChar *codePathStr = NULL;
    xmlChar *resourcePathStr = NULL;
    xmlChar *nativeLibraryPathStr = NULL;
    xmlChar *systemStr = NULL;
    xmlChar *installerPackageName = NULL;
    xmlChar *uidError = NULL;
    xmlChar *version = NULL;
    xmlChar *timeStampStr;
    int pkgFlags = 0;
    int64_t timeStamp = 0;
    int64_t firstInstallTime = 0;
    int64_t lastUpdateTime = 0;
    int versionCode = 0;
    struct pkg_setting *packageSetting = NULL;

    name = xml_get_prop(node, "name");
    realName = xml_get_prop(node, "realName");
    idStr = xml_get_prop(node, "userId");
    uidError = xml_get_prop(node, "uidError");
    sharedIdStr = xml_get_prop(node, "sharedUserId");
    codePathStr = xml_get_prop(node, "codePath");
    resourcePathStr = xml_get_prop(node, "resourcePath");
    nativeLibraryPathStr = xml_get_prop(node, "nativeLibraryPath");
    version = xml_get_prop(node, "version");
    installerPackageName = xml_get_prop(node, "installer");

    if(version)
        versionCode = atoi(SCversion);

    systemStr = xml_get_prop(node, "flags");
    if(systemStr)
        pkgFlags = atoi(SCsystemStr);
    else
    {
        systemStr = xml_get_prop(node, "system");
        if(!systemStr || strcasecmp(SCsystemStr, "true") == 0)
            pkgFlags |= FLAG_SYSTEM;
    }

    timeStampStr = xml_get_prop(node, "ft");
    if(!timeStampStr)
        timeStamp = atoll(SCtimeStampStr);
    else
    {
        timeStampStr = xml_get_prop(node, "ts");
        if(timeStampStr)
            timeStamp = atoll(SCtimeStampStr);
    }

    xmlFree(timeStampStr);
    timeStampStr = xml_get_prop(node, "it");
    if(timeStampStr)
        firstInstallTime = strtoll(SCtimeStampStr, NULL, 16);;

    xmlFree(timeStampStr);
    timeStampStr = xml_get_prop(node, "ut");
    if(timeStampStr)
        lastUpdateTime = strtoll(SCtimeStampStr, NULL, 16);;

    LOGV("Reading package: %s userId=%s sharedUserId=%s\n", name, idStr, sharedIdStr);

    int userId = idStr ? atoi(idStr) : 0;
    if(!resourcePathStr && codePathStr)
        resourcePathStr = xmlStrdup(codePathStr);

    if(!name)
        LOGE("Error in package manager settings: <package> has no name!\n");
    else if(!codePathStr)
        LOGE("Error in package manager settings: <package> has no codePath!\n");
    else if(userId > 0)
    {
        // Settings.java:2406
        packageSetting = pm_settings_add_package(s, name, realName, codePathStr,
                resourcePathStr, nativeLibraryPathStr, userId, versionCode, pkgFlags);
        if(!packageSetting)
            LOGE("Failure adding uid %d while parsing settings.\n", userId);
        else
        {
            packageSetting->timeStamp = timeStamp;
            packageSetting->firstInstallTime = firstInstallTime;
            packageSetting->lastUpdateTime = lastUpdateTime;
        }
    }
    else if(sharedIdStr)
    {
        userId = atoi(SCsharedIdStr);
        if(userId > 0)
        {
            packageSetting = pkg_setting_create(name, realName, codePathStr,
                    resourcePathStr, nativeLibraryPathStr, userId, versionCode, pkgFlags);
            packageSetting->timeStamp = timeStamp;
            packageSetting->firstInstallTime = firstInstallTime;
            packageSetting->lastUpdateTime = lastUpdateTime;
            list_add(&s->pending_packages, packageSetting);
        }
        else
        {
            LOGE("Error in package manager settings: package %s has bad sharedId %s\n", name, sharedIdStr);
        }
    }
    else
    {
        LOGE("Error in package manager settings: package %s has bad userId %s\n", name, idStr);
    }

    if(packageSetting)
    {
        packageSetting->uidError = xml_str_equal(uidError, "true");
        packageSetting->installerPackageName = strdup_safe(installerPackageName);
        // This is already set in constructor
        // packageSetting.nativeLibraryPathString = nativeLibraryPathStr;

        xmlChar *enabledStr = xml_get_prop(node, "enabled");
        if(enabledStr)
        {
            char *endptr = NULL;
            int enabled = strtol((char*)enabledStr, &endptr, 0);
            if((char*)enabledStr != endptr)
                pkg_setting_set_enabled(packageSetting, enabled, 0, NULL);
            else if(strcasecmp(SCenabledStr, "true") == 0)
                pkg_setting_set_enabled(packageSetting, COMPONENT_ENABLED_STATE_ENABLED, 0, NULL);
            else if(strcasecmp(SCenabledStr, "false") == 0)
                pkg_setting_set_enabled(packageSetting, COMPONENT_ENABLED_STATE_DISABLED, 0, NULL);
            else if(strcasecmp(SCenabledStr, "default") == 0)
                pkg_setting_set_enabled(packageSetting, COMPONENT_ENABLED_STATE_DEFAULT, 0, NULL);
            else
                LOGE("Error in package manager settings: package %s has bad enabled value %s\n", name, enabledStr);

            xmlFree(enabledStr);
        }
        else
            pkg_setting_set_enabled(packageSetting, COMPONENT_ENABLED_STATE_DEFAULT, 0, NULL);

        xmlChar *installStatusStr = xml_get_prop(node, "installStatus");
        if(installStatusStr)
        {
            if(strcasecmp(SCinstallStatusStr, "false") == 0)
                packageSetting->installStatus = PKG_INSTALL_INCOMPLETE;
            else
                packageSetting->installStatus = PKG_INSTALL_COMPLETE;
            xmlFree(installStatusStr);
        }

        xmlNode *child;
        for(child = node->children; child; child = child->next)
        {
            if(child->type != XML_ELEMENT_NODE)
                continue;

            // Settings.java:2497
            if(xml_str_equal(child->name, "disabled-components"))
                pm_settings_read_disabled_components(s, packageSetting, 0);
            else if(xml_str_equal(child->name, "enabled-components"))
                pm_settings_read_enabled_components(s, packageSetting, 0);
            else if(xml_str_equal(child->name, "sigs"))
                pkg_signatures_read(packageSetting->signatures, child, &s->past_signatures);
            else if(xml_str_equal(child->name, "perms"))
            {
                pm_settings_read_granted_permissions(s, child, packageSetting->grantedPermissions);
                packageSetting->permissionsFixed = 1;
            }
            else if(xml_str_equal(child->name, "signing-keyset"))
            {
                xmlChar *identifierStr = xml_get_prop(child, "identifier");
                int64_t id = identifierStr ? atoll(identifierStr) : 0;

                pkg_key_set_data_add_signing_key_set(packageSetting->keySetData, id);
                
                xmlFree(identifierStr);
            }
            else if(xml_str_equal(child->name, "defined-keyset"))
            {
                xmlChar *identifierStr = xml_get_prop(child, "identifier");
                int64_t id = identifierStr ? atoll(identifierStr) : 0;
                xmlChar *alias = xml_get_prop(child, "alias");

                pkg_key_set_data_add_defined_key_set(packageSetting->keySetData, id, alias);

                xmlFree(alias);
                xmlFree(identifierStr);
            }
            else
                LOGE("Unknown element under <package>: %s\n", child->name);
        }
    }
}

static int pm_settings_read_int(xmlNode *node, const xmlChar *name, int defValue)
{
    xmlChar *v = xml_get_prop(node, name);
    if(!v)
        return defValue;

    char *r;
    int res = strtoll((char*)v, &r, 0);
    if(r == (char*)v)
        res = defValue;
    xmlFree(v);
    return res;
}

static void pm_settings_read_permissions(struct pm_settings *s, xmlHashTable *out, xmlNode *node)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "item"))
        {
            xmlChar *name = xml_get_prop(child, "name");
            xmlChar *sourcePackage = xml_get_prop(child, "package");
            xmlChar *ptype = xml_get_prop(child, "type");
            if(name && sourcePackage)
            {
                int dynamic = xml_str_equal(ptype, "dynamic");
                struct base_perm *perm = base_perm_create(name, sourcePackage, dynamic ? BASEPERM_TYPE_DYNAMIC : BASEPERM_TYPE_NORMAL);
                perm->protectionLevel = pm_settings_read_int(child, "protection", PROTECTION_NORMAL);
                perm->protectionLevel = base_perm_fix_protection_level(perm->protectionLevel);
                if(dynamic)
                {
                    struct perm_info *pi = perm_info_create();
                    pi->packageName = strdup_safe(sourcePackage);
                    pi->name = strdup_safe(name);
                    pi->icon = pm_settings_read_int(child, "icon", 0);
                    pi->nonLocalizedLabel = (char*)xml_get_prop(child, "label");
                    pi->protectionLevel = perm->protectionLevel;
                    perm->pendingInfo = pi;
                }
                xmlHashUpdateEntry(out, name, perm, base_perm_deallocator);
            }
            else
                LOGE("Error in package manager settings: permissions has no name!\n");
            xmlFree(name);
            xmlFree(sourcePackage);
            xmlFree(ptype);
        }
        else
            LOGE("Unknown element reading permissions: %s\n", child->name);
    }
}

static struct shared_user_setting *pm_settings_add_shared_user(struct pm_settings *s, const xmlChar *name, int userId, int pkgFlags)
{
    struct shared_user_setting *ses = xmlHashLookup(s->shared_users, name);
    if(ses)
    {
        if(ses->userId == userId)
            return ses;
        LOGE("Adding duplicate shared user, keeping first %s\n", name);
        return NULL;
    }
    ses = shared_user_setting_create(name, pkgFlags);
    ses->userId = userId;
    if(pm_settings_add_user_id(s, userId, ses, name))
    {
        xmlHashAddEntry(s->shared_users, name, ses);
        return ses;
    }
    shared_user_setting_destroy(ses);
    return NULL;
}

static void pm_settings_read_shared_user(struct pm_settings *s, xmlNode *node)
{
    xmlChar *name = NULL;
    xmlChar *idStr = NULL;
    xmlChar *systemStr = NULL;
    int pkgFlags = 0;
    int userId;
    struct shared_user_setting *su = NULL;

    name = xml_get_prop(node, "name");
    idStr = xml_get_prop(node, "userId");
    systemStr = xml_get_prop(node, "system");
    userId = idStr ? atoi(SCidStr) : 0;

    if(systemStr && xml_str_equal(systemStr, "true"))
        pkgFlags |= FLAG_SYSTEM;

    if(!name)
        LOGE("Error in package manager settings: <shared-user> has no name!\n");
    else if(userId == 0)
        LOGE("Error in package manager settings: shared-user %s has bad userId %s!\n", name, idStr);
    else // Settings.java:2605
        su = pm_settings_add_shared_user(s, name, userId, pkgFlags);

    if(su)
    {
        xmlNode *child;
        for(child = node->children; child; child = child->next)
        {
            if(child->type != XML_ELEMENT_NODE)
                continue;

            if(xml_str_equal(child->name, "sigs"))
                pkg_signatures_read(su->signatures, child, &s->past_signatures);
            else if(xml_str_equal(child->name, "perms"))
                pm_settings_read_granted_permissions(s, child, ses->grantedPermissions);
            else
                LOGE("Unknown element under <shared-user>: %s\n", child->name);
        }
    }
}

static void pm_settings_read_preferred_activities(struct pm_settings *s, xmlNode *node, int userId)
{
    // TODO
}

int pm_settings_read(struct pm_settings *s, const char *path)
{
    xmlDoc *doc = NULL;
    xmlNode *node;
    int res = -1;

    doc = xmlReadFile(path, NULL, 0);
    if(!doc || (node = xmlDocGetRootElement(doc)) == NULL)
    {
        ERROR("Failde to read %s!\n", path);
        goto exit
    }

    node = xml_find_node(node, "packages")
    if(!node)
    {
        ERROR("Failed to find <packages> node!");
        goto exit;
    }

    for(node = node->children; node; node = node->next)
    {
        if(node->type != XML_ELEMENT_NODE)
            continue;
        
        // Settings.java:1712
        if(xml_str_equal(node->name, "package"))
            pm_settings_read_package(s, node);
        else if(xml_str_equal(node->name, "permissions"))
            pm_settings_read_permissions(s, s->permissions, node);
        else if(xml_str_equal(node->name, "permission-trees"))
            pm_settings_read_permissions(s, s->permission_trees, node);
        else if(xml_str_equal(node->name, "shared-user"))
            pm_settings_read_shared_user(s, node);
        else if(xml_str_equal(node->name, "preferred-packages")) { /* not used */ }
        else if(xml_str_equal(node->name, "preferred-activities"))
            pm_settings_read_preferred_activities(s, node, 0);

    }

exit:
    if(doc)
        xmlFreeDoc(doc);
    xmlCleanupParser();
    return res;
}
