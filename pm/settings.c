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
#include <libxml/xmlwriter.h>

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

static struct pkg_user_state DEFAULT_USER_STATE = {
    .stopped = 0,
    .notLaunched = 0,
    .installed = 1,
    .blocked = false,
    .enabled = COMPONENT_ENABLED_STATE_DEFAULT,
    .lastDisableAppCaller = NULL,
    .disabledComponents = NULL,
    .enabledComponents = NULL
};

static void *empty_hash_copier(void *payload, xmlChar *name)
{
    return payload;
}

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

struct pkg_key_set_data
{
    int64_t *signingKeySets;
    int signingKeySetsSize;
    int64_t *definedKeySets;
    int definedKeySetsSize;
    xmlHashTable keySetAliases;
};

struct pkg_key_set_data_aliases_copier_data
{
    xmlHashTable *dest;
    int64_t *base_definedKeySets;
    int64_t *dest_definedKeySets;
};

static void pkg_key_set_data_aliases_copier(void *payload, void *data, xmlChar *name)
{
    struct pkg_key_set_data_aliases_copier_data *d = data;
    int64_t *alias = payload;
    int idx = alias - d->base_definedKeySets;

    xmlHashAddEntry(dest, name, &d->dest_definedKeySets[idx]);
}

struct pkg_key_set_data *pkg_key_set_data_clone(struct pkg_key_set_data *base)
{
    struct pkg_key_set_data *pksd = mzalloc(sizeof(struct pkg_key_set_data));
    if(base->signingKeySetsSize)
    {
        pksd->signingKeySets = malloc(sizeof(int64_t)*base->signingKeySetsSize);
        memcpy(pksd->signingKeySets, base->signingKeySets, sizeof(int64_t)*base->signingKeySetsSize);
        pksd->signingKeySetsSize = base->signingKeySetsSize;
    }
    if(base->definedKeySetsSize)
    {
        pksd->definedKeySets = malloc(sizeof(int64_t)*base->definedKeySetsSize);
        memcpy(pksd->definedKeySets, base->definedKeySets, sizeof(int64_t)*base->definedKeySetsSize);
        pksd->definedKeySetsSize = base->definedKeySetsSize;
    }
    pksd->keySetAliases = xmlHashCreate(16);
    struct pkg_key_set_data_aliases_copier_data d = {
        .dest = pksd->keySetAliases,
        .base_definedKeySets = base->definedKeySets,
        .dest_definedKeySets = pksd->definedKeySets,
    };
    xmlHashScan(base->keySetAliases, pkg_key_set_data_aliases_copier, &d);
    return pksd;
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
    sig->signature_size = len/2;
    sig->signature = malloc(sig->signature_size);

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

int signature_equals(struct signature *a, struct signature *b)
{
    if(a == b)
        return 1;

    if(!a || !b || a->signature_size != b->signature_size)
        return 0;

    return memcmp(a->signature, b->signature, a->signature_size) == 0;
}

xmlChar *signature_to_xmlchar(struct signature *sig)
{
    int i, x, d;
    int8_t v;
    xmlChar *res = xmlMalloc((sig->signature_size * 2) + 1);

    for(i = 0, x = 0; i < sig->signature_size; ++i)
    {
        v = sig->signature[i];
        d = (v >> 4) & 0xF;
        res[x++] = d >= 10 ? ('a' + d - 10) : ('0' + d);
        d = v & 0xF;
        res[x++] = d >= 10 ? ('a' + d - 10) : ('0' + d);
    }
    res[x] = 0;
    return res;
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

static void pkg_signatures_write(struct pkg_signatures *psig, xmlTextWriter *writer, const char *tagName, struct signature ***past_signatures)
{
    if(!psig->signatures)
        return;

    int i, x, pastSize;
    const int size = list_item_count(psig->signatures);

    xmlTextWriterStartElement(writer, UCtagName);
    xmlTextWriterWriteFormatAttribute(writer, UC"count", "%d", size);
    for(i = 0; i < size; ++i)
    {
        struct signature *sig = psig->signatures[i];
        pastSize = list_item_count(*past_signatures);

        xmlTextWriterStartElement(writer, UC"cert");
        for(x = 0; x < pastSize; ++x)
        {
            struct signature *pastSig = (*past_signatures)[x];
            if(signature_equals(pastSig, sig))
            {
                xmlTextWriterWriteFormatAttribute(writer, UC"index", "%d", x);
                break;
            }
        }

        if(j >= pastSize)
        {
            list_add(past_signatures, sig);
            xmlChar *keyStr = signature_to_xmlchar(sig);
            xmlTextWriterWriteFormatAttribute(writer, UC"index", "%d", pastSize);
            xmlTextWriterWriteAttribute(writer, UC"key", keyStr);
            xmlFree(keyStr);
        }

        xmlTextWriterEndElement(writer);
    }
    xmlTextWriterEndElement(writer);
}


struct pkg_user_state *pkg_user_state_create(void)
{
    struct pkg_user_state *st = mzalloc(sizeof(struct pkg_user_state));
    st->installed = 1;
    st->blocked = 0;
    st->enabled = COMPONENT_ENABLED_STATE_DEFAULT;
    st->disabledComponents = xmlHashCreate(8);
    st->enabledComponents = xmlHashCreate(8);
    return st;
}

void pkg_user_state_destroy(struct pkg_user_state *st)
{
    xmlHashFree(st->disabledComponents, NULL);
    xmlHashFree(st->enabledComponents, NULL);
    free(st->lastDisableAppCaller);
    free(st);
}

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

struct pkg_user_state *pkg_user_state_clone(struct pkg_user_state *base)
{
    if(!base)
        return NULL;

    struct pkg_user_state *st = mzalloc(sizeof(struct pkg_user_state));
    memcpy(st, base, sizeof(struct pkg_user_state));
    st->lastDisableAppCaller = strdup_safe(base->lastDisableAppCaller);
    st->disabledComponents = xmlHashCopy(base->disabledComponents, empty_hash_copier);
    st->enabledComponents = xmlHashCopy(base->enabledComponents, empty_hash_copier);
    return st;
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

    ps->grantedPermissions = xmlHashCreate(16);
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
    xmlHashFree(ps->grantedPermissions, NULL);
    imap_destroy(ps->userState, pkg_user_state_destroy);
    pkg_signatures_destroy(ps->signatures);
    pkg_key_set_data_destroy(ps->keySetData);
    free(ps->gids);
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

static struct pkg_user_state *pkg_setting_read_user_state(struct pkg_setting *ps, int userId)
{
    struct pkg_user_state *st = imap_get_val(ps->userState, userId);
    if(st)
        return st;
    return &DEFAULT_USER_STATE;
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
    xmlHashAddEntry(st->disabledComponents, name, NULL);
}

void pkg_setting_add_enabled_component(struct pkg_setting *ps, const xmlChar *name, int userId)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    xmlHashAddEntry(st->enabledComponents, name, NULL);
}

xmlHashTable *pkg_setting_get_enabled_components(struct pkg_setting *ps, int userId)
{
    return pkg_setting_read_user_state(ps, userId)->enabledComponents;
}

xmlHashTable *pkg_setting_get_disabled_components(struct pkg_setting *ps, int userId)
{
    return pkg_setting_read_user_state(ps, userId)->disabledComponents;
}

void pkg_setting_set_enabled_components_copy(struct pkg_setting *ps, xmlHashTable *components, int userId)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    xmlHashFree(st->enabledComponents);
    if(components)
        st->enabledComponents = xmlHashCopy(components, empty_hash_copier);
    else
        st->enabledComponents = xmlHashCreate(16);
}

void pkg_setting_set_disabled_components_copy(struct pkg_setting *ps, xmlHashTable *components, int userId)
{
    struct pkg_user_state *st = pkg_setting_modify_user_state(ps, userId);
    xmlHashFree(st->disabledComponents);
    if(components)
        st->disabledComponents = xmlHashCopy(components, empty_hash_copier);
    else
        st->disabledComponents = xmlHashCreate(16);
}

void pkg_setting_copy_from(struct pkg_setting *ps, struct pkg_setting *base);
{
    xmlHashFree(ps->grantedPermissions, NULL);
    ps->grantedPermissions = xmlHashCopy(base->grantedPermissions, empty_hash_copier);

    ps->gids_size = base->gids_size;
    if(base->gids_size)
    {
        ps->gids = realloc(ps->gids, sizeof(int)*base->gids_size);
        memcpy(ps->gids, base->gids, sizeof(int)*base->gids_size);
    }

    ps->timeStamp = base->timeStamp;
    ps->firstInstallTime = base->firstInstallTime;
    ps->lastUpdateTime = base->lastUpdateTime;
    ps->permissionsFixed = base->permissionsFixed;
    ps->haveGids = base->haveGids;

    imap_clear(ps->userState, pkg_user_state_destroy);
    size_t i;
    for(i = 0; i < base->userState->size; ++i)
    {
        struct pkg_user_state *st = pkg_user_state_clone(base->userState->value[i]);
        imap_add_not_exist(ps->userState, base->userState->keys[i], st);
    }

    ps->installStatus = base->installStatus;

    pkg_key_set_data_destroy(ps->keySetData);
    ps->keySetData = pkg_key_set_data_clone(base->keySetData);
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
    ses->grantedPermissions = xmlHashCreate(16);
    shared_user_setting_set_flags(pkgFlags);
    return ses;
}

void shared_user_setting_destroy(struct shared_user_setting *sus)
{
    xmlHashFree(ses->grantedPermissions, NULL);
    pkg_signatures_destroy(ses->signatures);
    free(ses->name);
    free(ses);
}

void shared_user_setting_set_flags(struct shared_user_setting *sus, int pkgFlags)
{
    ses->pkgFlags = pkgFlags & (FLAG_SYSTEM | FLAG_PRIVILEGED | FLAG_FORWARD_LOCK | FLAG_EXTERNAL_STORAGE);
}

void shared_user_setting_add_package(struct shred_user_setting *sus, struct pkg_setting *pkg)
{
    int i;
    const int size = list_item_count(sus->packages);
    for(i = 0; i < size; ++i)
        if(sus->packages[i] == pkg)
            return;

    list_add(&sus->packages, pkg);
    shared_user_setting_set_flags(sus, sus->pkgFlags | pkg->pkgFlags);
}


struct pkg_clean_item pkg_clean_item_create(int userId, const char *packageName, int andCode)
{
    struct pkg_clean_item *pci = mzalloc(sizeof(struct pkg_clean_item));
    pci->userId = userId;
    pci->packageName = strdup_safe(packageName);
    pci->andCode = andCode;
}

void pkg_clean_item_destroy(struct pkg_clean_item *pci)
{
    free(pci->packageName);
    free(pci);
}

void pkg_clean_item_equals(struct pkg_clean_item *pci1, struct pkg_clean_item *pci2)
{
    return pci1->userId == pci2->userId && pci1->andCode == pci2->andCode &&
            strcmp(pci1->packageName, pci2->packageName) == 0;
}


struct verifier_device_id *verifier_device_id_parse(const xmlChar *text)
{
    int64_t output = 0;
    int numParsed = 0;
    int i, value, group;
    const int N = xmlStrlen(text);

    for(i = 0; i < N; ++i)
    {
        group = text[i];

        if('A' <= group && group <= 'Z')
            value = group - 'A';
        else if('2' <= group && group <= '7')
            value = group - ('2' - 26);
        else if(group == '-')
            continue;
        else if('a' <= group && group <= 'z')
            value = group - 'a';
        else if(group == '0')
            value = 'O' - 'A';
        else if(group == '1')
            value = 'I' - 'A';
        else
        {
            LOGE("Illegal base-32 character in verifier device id: %d\n", group);
            return NULL;
        }

        output = (output << 5) | value;
        ++numParsed;

        if(numParsed == 1 && (value & 0xF) != value)
        {
            LOGE("Illegal start character in verifier device id; will overflow\n");
            return NULL;
        }
        else if(numParsed > 13)
        {
            LOGE("Too long verifier device id, should have 13 characters\n");
            return NULL;
        }
    }

    if(numParsed != 13)
    {
        LOGE("Too short verifier device id, should have 13 characters\n");
        return NULL;
    }

    struct verifier_device_id *vdi = mzalloc(sizeof(struct verifier_device_id));
    vdi->identity = output;

    static const char ENCODE[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', '2', '3', '4', '5', '6', '7',
    };
    const int LONG_SIZE = 13;
    const int GROUP_SIZE = 4;
    int index = LONG_SIZE + (LONG_SIZE / GROUP_SIZE);
    int64_t input = vdi->identity;
    vdi->identityString = mzalloc(index + 1);

    for(i = 0; i < LONG_SIZE; ++i)
    {
        if(i > 0 && (i % GROUP_SIZE) == (LONG_SIZE % GROUP_SIZE))
            vdi->identityString[--index] = '-';

        group = input & 0x1F;
        input = ((uint64_t)input) >> 5;
        vdi->identityString[--index] = ENCODE[group];
    }

    return vdi;
}

void verifier_device_id_destroy(struct verifier_device_id *vdi)
{
    if(vdi)
    {
        free(vdi->identityString);
        free(vdi);
    }
}

static void key_set_mapping_deallocator(int64_t **list)
{
    int64_t **ptr = list;
    list_clear(&ptr, free);
}

struct key_set_mgr *key_set_mgr_create(xmlHashTable *packages)
{
    struct key_set_mgr *mgr = mzalloc(sizeof(struct key_set_mgr));
    mgr->key_sets = i64map_create();
    mgr->public_keys = i64map_create();
    mgr->key_set_mapping = i64map_create();
    mgr->packages = packages;
    return mgr;
}

void key_set_mgr_destroy(struct key_set_mgr *mgr)
{
    i64map_destroy(mgr->key_sets, NULL);
    i64map_destroy(mgr->public_keys, NULL);
    i64map_destroy(mgr->key_set_mapping, key_set_mapping_deallocator);
    free(mgr);
}

static void key_set_mgr_read_public_key(struct key_set_mgr *mgr, xmlNode *node)
{
    xmlChar *encodedID = xml_get_prop(node, "identifier");
    xmlChar *encodedPublicKey = xml_get_prop(node, "value");
    if(encodedID && encodedPublicKey)
    {
        int64_t identifier = strtoll(SCencodedID, NULL, 0);
        i64map_add(mgr->public_keys, identifier, encodedPublicKey, xmlFree);
    }
    else
    {
        LOGE("key doesn't have identifier or value");
        xmlFree(encodedPublicKey);
    }

    xmlFree(encodedID);
}

static void key_set_mgr_read_keys(struct key_set_mgr *mgr, xmlNode *node)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "public-key"))
            key_set_mgr_read_public_key(mgr, child);
        else if(xml_str_equal(child->name, "lastIssuedKeyId"))
        {
            xmlChar *idStr = xml_get_prop(child, "value");
            if(idStr)
            {
                mgr->lastIssuedKeyId = strtoll(SCidStr, NULL, 0);
                xmlFree(idStr);
            }
        }
        else if(xml_str_equal(child->name, "lastIssuedKeySetId"))
        {
            xmlChar *idStr = xml_get_prop(child, "value");
            if(idStr)
            {
                mgr->lastIssuedKeySetId = strtoll(SCidStr, NULL, 0);
                xmlFree(idStr);
            }
        }
    }
}

static int64_t key_set_mgr_read_identifier(xmlNode *node, int *ok)
{
    xmlChar *identifierStr = xml_get_prop(node, "identifier");
    if(identifierStr)
    {
        char *r;
        int64_t res = strtoll(SCidentifierStr, &r, 0);
        *ok = (r != (char*)identifierStr);
        xmlFree(identifierStr);
        return res;
    }
    *ok = 0;
    return 0;
}

static void key_set_mgr_read_key_set_list(struct key_set_mgr *mgr, xmlNode *node)
{
    int64_t currentKeySetId = 0;
    xmlNode *child;
    int ok;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "keyset"))
        {
            currentKeySetId = key_set_mgr_read_identifier(child, &ok);
            if(!ok)
            {
                LOGE("Failed to read keyset identifier");
                return;
            }
            i64map_add(mgr->key_sets, currentKeySetId, NULL, free); // FIXME
            i64map_add(mgr->key_set_mapping, currentKeySetId, mzalloc(sizeof(int64_t**)), key_set_mapping_deallocator);
        }
        else if(xml_str_equal(child->name, "key-id"))
        {
            int64_t id = key_set_mgr_read_identifier(child, &ok);
            if(!ok)
            {
                LOGE("Failed to read key-id identifier");
                return;
            }

            int64_t ***mapping = i64map_get_ref(mgr->key_set_mapping, id);
            if(mapping)
            {
                ok = 1;
                int i;
                const int size = list_item_count(*mapping);
                for(i = 0; i < size; ++i)
                {
                    if(*((*mapping)[i]) == id)
                    {
                        ok = 0;
                        break;
                    }
                }

                if(ok)
                {
                    int64_t *val = mzalloc(sizeof(int64_t));
                    *val = id;
                    list_add(mapping, val);
                }
            }
        }
    }
}

void key_set_mgr_read(struct key_set_mgr *mgr, xmlNode *node)
{
    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;

        if(xml_str_equal(child->name, "keys"))
            key_set_mgr_read_keys(mgr, child);
        else if(xml_str_equal(child->name, "keysets"))
            key_set_mgr_read_key_set_list(mgr, child);
    }
}

static void key_set_mgr_write_public_keys(struct key_set_mgr *mgr, xmlTextWriter *writer)
{
    size_t i;
    xmlTextWriterStartElement(writer, UC"keys");
    for(i = 0; i < mgr->public_keys->size; ++i)
    {
        int64_t id = mgr->public_keys->keys[i];
        xmlChar *encodedKey = mgr->public_keys->values[i];
        
        xmlTextWriterStartElement(writer, UC"public-key");
        xmlTextWriterWriteFormatAttribute(writer, UC"identifier", "%lld", id);
        xmlTextWriterWriteAttribute(writer, UC"value", encodedKey);
        xmlTextWriterEndElement(writer);
    }
    xmlTextWriterEndElement(writer);
}

static void key_set_mgr_write_key_sets(struct key_set_mgr *mgr, xmlTextWriter *writer)
{
    int x;
    size_t i;
    xmlTextWriterStartElement(writer, UC"keysets");
    for(i = 0; i < mgr->key_set_mapping->size; ++i)
    {
        const int64_t id = mgr->key_set_mapping->keys[i];
        int64_t **keys = mgr->key_set_mapping->values[i];
        const int size = list_item_count(keys);
        
        xmlTextWriterStartElement(writer, UC"keyset");
        xmlTextWriterWriteFormatAttribute(writer, UC"identifier", "%lld", id);
        for(x = 0; x < size; ++x)
        {
            xmlTextWriterStartElement(writer, UC"key-id");
            xmlTextWriterWriteAttribute(writer, UC"identifier", *(keys[x]));
            xmlTextWriterEndElement(writer);    
        }
        xmlTextWriterEndElement(writer);
    }
    xmlTextWriterEndElement(writer);
}

static void key_set_mgr_write(struct key_set_mgr *mgr, xmlTextWriter *writer)
{
    xmlTextWriterStartElement(writer, UC"keyset-settings");

    key_set_mgr_write_public_keys(mgr, writer);
    key_set_mgr_write_key_sets(mgr, writer);

    xmlTextWriterStartElement(writer, UC"lastIssuedKeyId");
    xmlTextWriterWriteFormatAttribute(writer, UC"value", "%lld", mgr->lastIssuedKeyId);
    xmlTextWriterEndElement(writer);

    xmlTextWriterStartElement(writer, UC"lastIssuedKeySetId");
    xmlTextWriterWriteFormatAttribute(writer, UC"value", "%lld", mgr->lastIssuedKeySetId);
    xmlTextWriterEndElement(writer);

    xmlTextWriterEndElement(writer);
}


struct pm_settings *pm_settings_create(void)
{
    struct pm_settings *s = mzalloc(sizeof(struct pm_settings));
    s->packages = xmlHashCreate(0);
    s->disabled_packages = xmlHashCreate(16);
    s->shared_users = xmlHashCreate(16);
    s->other_user_ids = imap_create();
    s->permissions = xmlHashCreate(16);
    s->permission_trees = xmlHashCreate(16);
    s->renamed_packages = xmlHashCreate(16);
    s->key_set_mgr = key_set_mgr_create(s->packages);
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

static void xml_free_deallocator(void *payload, xmlChar *name)
{
    xmlFree(payload);
}

void pm_settings_destroy(struct pm_settings *s)
{
    xmlHashDestroy(s->packages, pkg_setting_deallocator);
    xmlHashDestroy(s->disabled_packages, pkg_setting_deallocator);
    list_clear(&s->pending_packages, pkg_setting_destroy);
    list_clear(&s->user_ids, NULL);
    imap_destroy(s->other_user_ids, NULL);
    xmlHashFree(s->shared_users, shared_user_setting_deallocator);
    list_clear(&s->past_signatures, NULL);
    xmlHashDestroy(s->permissions, base_perm_deallocator);
    xmlHashDestroy(s->permission_trees, base_perm_deallocator);
    list_clear(&s->preferred_activities, xmlFreeNode);
    list_clear(&s->packages_to_clean, pkg_clean_item_destroy);
    xmlHashDestroy(s->renamed_packages, xml_free_deallocator);
    verifier_device_id_destroy(s->verifier_device_id);
    key_set_mgr_destroy(s->key_set_mgr);
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

static void *pm_settings_get_user_id_obj(struct pm_settings *s, int uid)
{
    if(uid >= FIRST_APPLICATION_UID)
    {
        const int N = list_item_count(s->user_ids);
        const int index = uid - FIRST_APPLICATION_UID;
        return index < N ? s->user_ids[index] : NULL;
    }
    else
        return imap_get_val(s->other_user_ids, uid);
}

static int pm_settings_new_user_id(struct pm_settings *s, void *obj)
{
    int i;
    const int N = list_item_count(s->user_ids);
    for(i = s->first_available_uid; i < N; ++i)
    {
        if(s->user_ids[i] == &USERID_NULL_OBJECT)
        {
            s->user_ids[i] = obj;
            return FIRST_APPLICATION_UID + i;
        }
    }

    if(N > LAST_APPLICATION_UID - FIRST_APPLICATION_UID)
        return -1;

    list_add(&s->user_ids, obj);
    return FIRST_APPLICATION_UID + N;
}

static void pm_settings_add_package_setting(struct pm_settings *s, struct pkg_setting *p,
        const char *name, struct shared_user_setting *sharedUser)
{
    xmlHashUpdateEntry(s->packages, name, p, pkg_setting_deallocator);
    if(sharedUser)
    {
        if(p->sharedUser && p->sharedUser != sharedUser)
        {
            LOGE("Package %s was under %s but is now %s; I am not changing its files so it will probably fail!",
                    name, p->sharedUser->name, sharedUser->name);
        }
        else if(p->appId != sharedUser->userId)
        {
            LOGE("Package %s was user id %d but is now user %s with id %d!",
                    name, p->appId, sharedUser->name, sharedUser->userId);
        }
        shared_user_setting_add_package(sharedUser, p);
        p->sharedUser = sharedUser;
        p->appId = sharedUser->userId;
    }
}

static struct pkg_setting *pm_settings_get_package(struct pm_settings *s, char *name,
        struct pkg_setting *origPackage, const char *realName, struct shared_user_setting *sharedUser,
        const char *codePath, const char *resourcePath, const char *nativeLibraryPathString,
        int vc, int pkgFlags, void *installUser, int add, int allowInstall)
{
    struct pkg_setting *p = xmlHashLookup(s->packages, UCname);
    if(p)
    {
        if(!xml_str_equal(p->codePath, codePath))
        {
            if(p->pkgFlags & FLAG_SYSTEM)
                LOGE("Trying to update system app code path from %s to %s\n", p->codePath, codePath);
            else
            {
                LOGV("Package %s codePath changed from %s to %s; Retaining data and using new\n", name, p->codePath, codePath);
                p->nativeLibraryPath = strset(p->nativeLibraryPath, nativeLibraryPathString);
            }
        }

        if(p->sharedUser != sharedUser)
        {
            LOGE("Package %s shared user changed from %s to %s; replacing with new\n", name,
                    p->sharedUser ? p->sharedUser->name : "<nothing>",
                    sharedUser ? sharedUser->name : "<nothing>");
            p = NULL;
        }
        else
        {
            int sysPrivFlags = pkgFlags & (FLAG_SYSTEM | FLAG_PRIVILEGED);
            p->pkgFlags |= sysPrivFlags;
        }
    }

    if(!p)
    {
        if(origPackage)
        {
            p = pkg_setting_create(origPackage->name, name, codePath, resourcePath,
                    nativeLibraryPathString, vc, pkgFlags);
            struct pkg_signatures *s = p->signatures;
            pkg_setting_copy_from(p, origPackage);
            pkg_signatures_destroy(p->signatures);
            p->signatures = s;
            p->sharedUser = origPackage->sharedUser;
            p->appId = origPackage->appId;
            p->origPackage = origPackage;
            name = origPackage->name;
            p->timeStamp = get_mtime_ms(codePath);
        }
        else
        {
            p = pkg_setting_create(name, realName, codePath, resourcePath,
                    nativeLibraryPathString, vc, pkgFlags);
            p->timeStamp = get_mtime_ms(codePath);
            p->sharedUser = sharedUser;

            // Settings.java:468
            if(!(pkgFlags & FLAG_SYSTEM))
            {
                // FIXME: Add support for multiple users
            }

            if(sharedUser)
                p->appId = sharedUser->userId;
            else
            {
                struct pkg_setting *dis = xmlHashLookup(s->disabled_packages, UCname);
                if(dis)
                {
                    if(dis->signatures->signatures)
                    {
                        list_clear(&p->signatures->signatures, signature_destroy);
                        list_copy(&p->signatures->signatures, dis->signatures->signatures);
                    }
                    p->appId = dis->appId;

                    xmlHashFree(p->grantedPermissions, NULL);
                    p->grantedPermissions = xmlHashCopy(dis->grantedPermissions, empty_hash_copier);

                    // FIXME: Add support for multiple users
                    xmlHashTable *t = pkg_get_enabled_components(dis, 0);
                    pkg_setting_set_enabled_components_copy(p, t, 0);
                    t = pkg_get_disabled_components(dis, 0);
                    pkg_setting_set_disabled_components_copy(p, t, 0);

                    pm_settings_add_user_id(s, p->appId, p, name);
                }
                else
                {
                    p->appId = pm_settings_new_user_id(s, p);
                }
            }
        }

        if(p->appId < 0)
        {
            LOGE("Package %s could not be assigned a valied uid\n", name);
            return NULL;
        }
        if(add)
            pm_settings_add_package_setting(s, p, name, sharedUser);
    }
    else
    {
        if(installUser && allowInstall)
        {
            // FIXME: Add support for multiple users
        }
    }
    return p;
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

static void pm_settings_read_granted_permissions(struct pm_settings *s, xmlNode *node, xmlHashTable *outPerms)
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
                xmlHashAddEntry(outPerms, name, NULL);
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
                    resourcePathStr, nativeLibraryPathStr, versionCode, pkgFlags);
            packageSetting->sharedId = userId;
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
    xmlFree(name);
    xmlFree(realName);
    xmlFree(idStr);
    xmlFree(sharedIdStr);
    xmlFree(codePathStr);
    xmlFree(resourcePathStr);
    xmlFree(nativeLibraryPathStr);
    xmlFree(systemStr);
    xmlFree(installerPackageName);
    xmlFree(uidError);
    xmlFree(version);
    xmlFree(timeStampStr);
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

    xmlFree(name);
    xmlFree(idStr);
    xmlFree(systemStr);
}

static void pm_settings_read_disabled_package(struct pm_settings *s, xmlNode *node)
{
    xmlChar *name = xml_get_prop(node, "name");
    xmlChar *realName = xml_get_prop(node, "realName");
    xmlChar *codePathStr = xml_get_prop(node, "codePath");
    xmlChar *resourcePathStr = xml_get_prop(node, "resourcePath");
    xmlChar *nativeLibraryPathStr = xml_get_prop(node, "nativeLibraryPath");
    if(!resourcePathStr && codePathStr)
        resourcePathStr = xmlStrdup(codePathStr);
    xmlChar *version = xml_get_prop(node, "version");
    int versionCode = 0;
    if(version)
        versionCode = atoi(SCversion);

    int pkgFlags = FLAG_SYSTEM;
    if(xmlStrncmp(codePathStr, PRIVILEGED_APP_PATH_U, PRIVILEGED_APP_PATH_LEN) == 0)
        pkgFlags |= FLAG_PRIVILEGED;

    struct pkg_setting *ps = pkg_setting_create(name, realName, codePathStr,
            resourcePathStr, nativeLibraryPathStr, versionCode, pkgFlags);
    xmlChar *timeStampStr = xml_get_prop(node, "ft");
    if(timeStampStr)
        ps->timeStamp = strtoll(SCtimeStampStr, NULL, 16);
    else
    {
        timeStampStr = xml_get_prop(node, "ts");
        if(timeStampStr)
            ps->timeStamp = strtoll(SCtimeStampStr, NULL, 0);    
    }

    xmlFree(timeStampStr);
    timeStampStr = xml_get_prop(node, "it");
    if(timeStampStr)
        ps->firstInstallTime = strtoll(SCtimeStampStr, NULL, 16);

    xmlFree(timeStampStr);
    timeStampStr = xml_get_prop(node, "ut");
    if(timeStampStr)
        ps->lastUpdateTime = strtoll(SCtimeStampStr, NULL, 16);

    xmlChar *idStr = xml_get_prop(node, "userId");
    ps->appId = idStr ? atoi(SCidStr) : 0;
    if(ps->appId <= 0)
    {
        xmlChar *sharedIdStr = xml_get_prop(node, "sharedUserId");
        ps->appId = sharedIdStr ? atoi(SCsharedIdStr) : 0;
        xmlFree(sharedIdStr);
    }

    xmlNode *child;
    for(child = node->children; child; child = child->next)
    {
        if(child->type != XML_ELEMENT_NODE)
            continue;
        
        if(xml_str_equal(child->name, "perms"))
            pm_settings_read_granted_permissions(s, child, ses->grantedPermissions);
        else
            LOGE("Unknown element under <updated-package>: %s\n", child->name);
    }

    xmlHashUpdateEntry(s->disabled_packages, name, ps, pkg_setting_deallocator);

    xmlFree(name);
    xmlFree(realName);
    xmlFree(codePathStr);
    xmlFree(resourcePathStr);
    xmlFree(nativeLibraryPathStr);
    xmlFree(version);
    xmlFree(idStr);
    xmlFree(timeStampStr);
}

static void pm_settings_add_pkg_to_clean(struct pm_settings *s, struct pkg_clean_item *pic)
{
    int i;
    const int size = list_item_count(s->packages_to_clean);
    for(i = 0; i < size; ++i)
    {
        if(pkg_clean_item_equals(pic, s->packages_to_clean[i]))
            return;
    }
    list_add(&s->packages_to_clean, pic);
}

static void pm_settings_shared_users_to_disabled_scanner(void *payload, void *data, xmlChar *name)
{
    struct pkg_setting *disabledPs = payload;
    struct pm_settings *s = data;
    struct shared_user_setting *id = pm_settings_get_user_id_obj(s, pp->sharedId);
    if(id)
        disabledPs->sharedUser = id;
}

int pm_settings_read(struct pm_settings *s, const char *path)
{
    xmlDoc *doc = NULL;
    xmlNode *node;
    int res = -1;

    doc = xmlReadFile(path, NULL, 0);
    if(!doc || (node = xmlDocGetRootElement(doc)) == NULL)
    {
        ERROR("Failed to read %s!\n", path);
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
        {
            xmlNode *cpy = xmlCopyNode(node, 1);
            xmlUnlinkNode(cpy);
            list_add(&s->preferred_activities, cpy);
        }
        else if(xml_str_equal(node->name, "updated-package"))
            pm_settings_read_disabled_package(s, node);
        else if(xml_str_equal(node->name, "cleaning-package"))
        {
            xmlChar *name = xml_get_prop(node, "name");
            xmlChar *userStr = xml_get_prop(node, "user");
            xmlChar *codeStr = xml_get_prop(node, "code");

            if(name)
            {
                int userId = 0;
                int andCode = 1;
                if(userStr)
                    userId = atoi(SCuserStr);
                if(codeStr)
                    andCode = strcasecmp(SCcodeStr, "true") == 0 ? 1 : 0;
                pm_settings_add_pkg_to_clean(s, pkg_clean_item_create(userId, name, andCode));
            }

            xmlFree(name);
            xmlFree(userStr);
            xmlFree(codeStr);
        }
        else if(xml_str_equal(node->name, "renamed-package"))
        {
            xmlChar *nname = xml_get_prop(node, "new");
            xmlChar *oname = xml_get_prop(node, "old");

            if(nname && oname)
                xmlHashUpdateEntry(s->renamed_packages, nname, oname, xml_free_deallocator);
            else
                xmlFree(oname);
            xmlFree(nname);
        }
        else if(xml_str_equal(node->name, "last-platform-version"))
        {
            s->internal_sdk_platform = s->external_sdk_platform = 0;

            xmlChar *internal = xml_get_prop(node, "internal");
            xmlChar *external = xml_get_prop(node, "external");

            if(internal)
                s->internal_sdk_platform = atoi(SCinternal);
            if(external)
                s->external_sdk_platform = atoi(SCexternal);

            xmlFree(internal);
            xmlFree(external);
        }
        else if(xml_str_equal(node->name, "verifier"))
        {
            xmlChar *deviceIdentity = xml_get_prop(node, "device");
            s->verifier_device_id = verifier_device_id_parse(deviceIdentity);
            if(!s->verifier_device_id)
                LOGE("Discarded invalid verifier device Id %s\n", deviceIdentity);
            xmlFree(deviceIdentity);
        }
        else if(xml_str_equal(node->name, "read-external-storage"))
        {
            xmlChar *enforcement = xml_get_prog(node, "enforcement");
            s->read_external_storage_enforced = atoi(SCenforcement);
            xmlFree(enforcement);
        }
        else if(xml_str_equal(node->name, "keyset-settings"))
            key_set_mgr_read(s->key_set_mgr, node);
        else
            LOGE("Unknown element under <packages>: %s\n", node->name);
    }

    const int N = list_item_count(s->pending_packages);
    int i;
    for(i = 0; i < N; ++i)
    {
        struct pkg_setting **pp = s->pending_packages[i];
        struct shared_user_setting *idObj = pm_settings_get_user_id_obj(s, pp->sharedId);
        if(idObj)
        {
            struct pkg_setting *p = pm_settings_get_package(s, pp->name, NULL, pp->realName,
                    idObj, pp->codePath, pp->resourcePath, pp->nativeLibraryPath,
                    pp->versionCode, pp->pkgFlags, NULL, 1, 0);
            if(!p)
            {
                LOGE("Unable to create application package for %s\n", pp->name);
                continue;
            }
            pkg_setting_copy_from(p, pp);
        }
        else
        {
            LOGE("Bad package setting: %s has shared uid %s that is not defined\n", pp->name, pp->sharedId);
        }
    }
    list_clear(&s->pending_packages, pkg_setting_destroy);

    xmlHashScan(s->disabled_packages, pm_settings_shared_users_to_disabled_scanner, s);

    res = 0;
exit:
    if(doc)
        xmlFreeDoc(doc);
    xmlCleanupParser();
    return res;
}

static void pm_settings_write_base_perm(void *payload, void *data, xmlChar *name)
{
    xmlTextWriter *writer = data;
    struct base_perm *perm = payload;

    xmlTextWriterStartElement(writer, UC"item");
    xmlTextWriterWriteAttribute(writer, UC"name", name);
    xmlTextWriterWriteAttribute(writer, UC"package", UCperm->sourcePackage);
    if(perm->protectionLevel != PROTECTION_NORMAL)
        xmlTextWriterWriteFormatAttribute(writer, UC"protection", "%d", perm->protectionLevel);
    if(perm->type == BASEPERM_TYPE_DYNAMIC)
    {
        // FIXME: our base_perm doesn't have 'perm' - Settings.java:1616
        struct perm_info *pi = perm->pendingInfo;
        if(pi)
        {
            xmlTextWriterWriteAttribute(writer, UC"type", UC"dynamic");
            if(pi->icon)
                xmlTextWriterWriteFormatAttribute(writer, UC"icon", "%d", pi->icon);
            if(pi->nonLocalizedLabel)
                xmlTextWriterWriteAttribute(writer, UC"label", pi->nonLocalizedLabel);
        }
    }
    xmlTextWriterEndElement(writer);
}

static void pm_settings_write_granted_perm(void *payload, void *data, xmlChar *name)
{
    xmlTextWriter *writer = data;
    xmlTextWriterStartElement(writer, UC"item");
    xmlTextWriterWriteAttribute(writer, UC"name", name);
    xmlTextWriterEndElement(writer);
}

static void pm_settings_write_key_set_alias(void *payload, void *data, xmlChar *name)
{
    xmlTextWriter *writer = d->writer;
    int64_t id = *((int64_t)payload);

    xmlTextWriterStartElement(writer, UC"defined-keyset");
    xmlTextWriterWriteAttribute(writer, UC"alias", name);
    xmlTextWriterWriteFormatAttribute(writer, UC"identifier", "%lld", id);
    xmlTextWriterEndElement(writer);
}

struct pm_settings_write_scan_data
{
    xmlTextWriter *writer;
    struct pm_settings *settings;
};

static void pm_settings_write_package(void *payload, void *data, xmlChar *name)
{
    struct pm_settings_write_scan_data *d = data;
    xmlTextWriter *writer = d->writer;
    struct pkg_setting *pkg = payload;

    xmlTextWriterStartElement(writer, UC"package");

    xmlTextWriterWriteAttribute(writer, UC"name", name);
    if(pkg->realName)
        xmlTextWriterWriteAttribute(writer, UC"realName", UCpkg->realName);
    xmlTextWriterWriteAttribute(writer, UC"codePath", UCpkg->codePath);
    if(strcmp(pkg->resourcePath, pkg->codePath) != 0)
        xmlTextWriterWriteAttribute(writer, UC"resourcePath", UCpkg->resourcePath);
    if(pkg->nativeLibraryPath)
        xmlTextWriterWriteAttribute(writer, UC"nativeLibraryPath", UCpkg->nativeLibraryPath);
    xmlTextWriterWriteFormatAttribute(writer, UC"flags", "%d", pkg->pkgFlags);
    xmlTextWriterWriteFormatAttribute(writer, UC"ft", "%lld", pkg->timeStamp);
    xmlTextWriterWriteFormatAttribute(writer, UC"it", "%lld", pkg->firstInstallTime);
    xmlTextWriterWriteFormatAttribute(writer, UC"ut", "%lld", pkg->lastUpdateTime);
    xmlTextWriterWriteFormatAttribute(writer, UC"version", "%d", pkg->versionCode);
    if(!pkg->sharedUser)
        xmlTextWriterWriteFormatAttribute(writer, UC"userId", "%d", pkg->appId);
    else
        xmlTextWriterWriteFormatAttribute(writer, UC"sharedUserId", "%d", pkg->appId);
    if(pkg->uidError)
        xmlTextWriterWriteAttribute(writer, UC"uidError", UC"true");
    if(pkg->installStatus == PKG_INSTALL_INCOMPLETE)
        xmlTextWriterWriteAttribute(writer, UC"installStatus", UC"false");
    if(pkg->installerPackageName)
        xmlTextWriterWriteAttribute(writer, UC"installer", UCpkg->installerPackageName);

    pkg_signatures_write(pkg->signatures, writer, "sigs", &d->settings->past_signatures);

    if(!(pkg->pkgFlags & FLAG_SYSTEM))
    {
        xmlTextWriterStartElement(writer, UC"perms");
        if(!pkf->sharedUser)
            xmlHashScan(pkg->grantedPermissions, pm_settings_write_granted_perm, writer);
        xmlTextWriterEndElement(writer);
    }

    int i;
    for(i = 0; i < pkg->keySetData->signingKeySetsSize; ++i)
    {
        xmlTextWriterStartElement(writer, UC"signing-keyset");
        xmlTextWriterWriteFormatAttribute(writer, UC"identifier", "%lld", pkg->keySetData->signingKeySets[i]);
        xmlTextWriterEndElement(writer);
    }

    xmlHashScan(pkg->keySetData->keySetAliases, pm_settings_write_key_set_alias, writer);

    xmlTextWriterEndElement(writer);
}

static void pm_settings_write_disabled_package(void *payload, void *data, xmlChar *name)
{
    struct pm_settings_write_scan_data *d = data;
    xmlTextWriter *writer = d->writer;
    struct pkg_setting *pkg = payload;

    xmlTextWriterStartElement(writer, UC"updated-package");
    xmlTextWriterWriteAttribute(writer, UC"name", name);
    if(pkg->realName)
        xmlTextWriterWriteAttribute(writer, UC"realName", UCpkg->realName);
    xmlTextWriterWriteAttribute(writer, UC"codePath", UCpkg->codePath);
    if(strcmp(pkg->resourcePath, pkg->codePath) != 0)
        xmlTextWriterWriteAttribute(writer, UC"resourcePath", UCpkg->resourcePath);
    if(pkg->nativeLibraryPath)
        xmlTextWriterWriteAttribute(writer, UC"nativeLibraryPath", UCpkg->nativeLibraryPath);
    xmlTextWriterWriteFormatAttribute(writer, UC"ft", "%lld", pkg->timeStamp);
    xmlTextWriterWriteFormatAttribute(writer, UC"it", "%lld", pkg->firstInstallTime);
    xmlTextWriterWriteFormatAttribute(writer, UC"ut", "%lld", pkg->lastUpdateTime);
    xmlTextWriterWriteFormatAttribute(writer, UC"version", "%d", pkg->versionCode);
    if(!pkg->sharedUser)
        xmlTextWriterWriteFormatAttribute(writer, UC"userId", "%d", pkg->appId);
    else
        xmlTextWriterWriteFormatAttribute(writer, UC"sharedUserId", "%d", pkg->appId);

    xmlTextWriterStartElement(writer, UC"perms");
    if(!pkf->sharedUser)
        xmlHashScan(pkg->grantedPermissions, pm_settings_write_granted_perm, writer);
    xmlTextWriterEndElement(writer);

    xmlTextWriterEndElement(writer);
}

static void pm_settings_write_shared_user(void *payload, void *data, xmlChar *name)
{
    struct pm_settings_write_scan_data *d = data;
    xmlTextWriter *writer = d->writer;
    struct shared_user_setting *usr = payload;

    xmlTextWriterStartElement(writer, UC"shared-user");
    xmlTextWriterWriteAttribute(writer, UC"name", name);
    xmlTextWriterWriteFormatAttribute(writer, UC"userId", "%d", user->userId);

    pkg_signatures_write(user->signatures, writer, "sigs", &d->settings->past_signatures);

    xmlTextWriterStartElement(writer, UC"perms");
    xmlHashScan(user->grantedPermissions, pm_settings_write_granted_perm, writer);
    xmlTextWriterEndElement(writer);

    xmlTextWriterEndElement(writer);
}

static void pm_settings_write_renamed_package(void *payload, void *data, xmlChar *name)
{
    struct pm_settings_write_scan_data *d = data;
    xmlTextWriter *writer = d->writer;
    xmlChar *oname = payload;

    xmlTextWriterStartElement(writer, UC"renamed-package");
    xmlTextWriterWriteAttribute(writer, UC"new", name);
    xmlTextWriterWriteAttribute(writer, UC"old", oname);
    xmlTextWriterEndElement(writer);
}

int pm_settings_write(struct pm_settings *s, const char *path)
{
    int res = -1;
    int i, size;
    xmlTextWriter *writer = NULL;   

    list_clear(&s->past_signatures, NULL); // FIXME: leak

    writer = xmlNewTextWriterFilename(path, );
    if(!writer)
    {
        LOGE("Failed to open xml writer");
        goto exit;
    }

    // Settings.java:1285
    xmlTextWriterStartDocument(writer, NULL, "utf-8", "yes");
    xmlTextWriterStartElement(writer, UC"packages");

    xmlTextWriterStartElement(writer, UC"last-platform-version");
    xmlTextWriterWriteFormatAttribute(writer, UC"internal", "%d", s->internal_sdk_platform);
    xmlTextWriterWriteFormatAttribute(writer, UC"external", "%d", s->external_sdk_platform);
    xmlTextWriterEndElement(writer);

    if(s->verifier_device_id)
    {
        xmlTextWriterStartElement(writer, UC"verifier");
        xmlTextWriterWriteAttribute(writer, UC"device", UCs->verifier_device_id->identityString);
        xmlTextWriterEndElement(writer);
    }

    if(s->read_external_storage_enforced)
    {
        xmlTextWriterStartElement(writer, UC"read-external-storage");
        xmlTextWriterWriteFormatAttribute(writer, UC"enforcement", "%d", s->read_external_storage_enforced);
        xmlTextWriterEndElement(writer);
    }

    xmlTextWriterStartElement(writer, UC"permission-trees");
    xmlHashScan(s->permission_trees, pm_settings_write_base_perm, writer);
    xmlTextWriterEndElement(writer);

    xmlTextWriterStartElement(writer, UC"permissions");
    xmlHashScan(s->permissions, pm_settings_write_base_perm, writer);
    xmlTextWriterEndElement(writer);

    struct pm_settings_write_scan_data data = {
        .writer = writer,
        .settings = s,
    };

    xmlHashScan(s->packages, pm_settings_write_package, &data);
    xmlHashScan(s->disabled_packages, pm_settings_write_disabled_package, &data);
    xmlHashScan(s->shared_users, pm_settings_write_shared_user, &data);

    size = list_item_count(s->packages_to_clean);
    for(i = 0; i < size; ++i)
    {
        struct pkg_clean_item *c = s->packages_to_clean[i];
        xmlTextWriterStartElement(writer, UC"cleaning-package");
        xmlTextWriterWriteAttribute(writer, UC"name", UCc->packageName);
        xmlTextWriterWriteAttribute(writer, UC"code", c->andCode ? UC"true" : UC"false");
        xmlTextWriterWriteFormatAttribute(writer, UC"user", "%d", c->userId);
        xmlTextWriterEndElement(writer);
    }

    xmlHashScan(s->renamed_packages, pm_settings_write_renamed_package, &data);

    key_set_mgr_write(s->key_set_mgr, writer);

    xmlTextWriterEndElement(writer);
    xmlTextWriterEndDocument(writer);

    res = 0;
exit:
    if(writer)
        xmlFreeTextWriter(writer);
    return res;
}
