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

#include "share.h"
#include "util.h"
#include "log.h"

/* TODO:
 * - libxml2 build
 * - write a comment about wtf it does
 */

enum
{
    ALLOWED_PACKAGE_TAG_PACKAGE = 0,
    ALLOWED_PACKAGE_TAG_UPDATED_PACKAGE = 1,

    ALLOWED_PACKAGE_TAGS_COUNT,
};

static char * const allowed_package_tags[] = { "package", "updated-package", NULL };

static const char *updated_tag = "-updated";
#define UPDATED_TAG_LEN 8

struct packages_hash_scanner_data
{
    xmlHashTable *rom_packages_hash;
    xmlHashTable *shared_packages_hash;
    xmlHashTable *shared_keys;
    xmlDict *rom_used_key_indexes;
    xmlDict *rom_used_user_ids;
    xmlHashTable *rom_key_mapping;
    xmlNode *last_nodes[ALLOWED_PACKAGE_TAGS_COUNT];
    int new_data_apps;
    int new_sys_apps;
};

#define PACKAGE_HASH_TYPE_ROM 0
#define PACKAGE_HASH_TYPE_SHARED 1

static int packages_allowed_tags_get_idx(const char *name)
{
    size_t i;
    for(i = 0; i < ALLOWED_PACKAGE_TAGS_COUNT; ++i)
        if(strcmp(allowed_package_tags[i], name) == 0)
            return i;
    return -1;
}

static xmlNode *packages_find_first_node(xmlNode *root, const char *name)
{
    xmlNode *ptr;
    for (ptr = root; ptr; ptr = ptr->next)
        if (ptr->type == XML_ELEMENT_NODE && xmlStrEqual(ptr->name, (xmlChar*)name))
            return ptr;
    return NULL;
}

static void packages_keys_add_package(xmlNode *parent, xmlHashTable *keys)
{
    xmlChar *key;
    xmlChar *idx;
    xmlNode *ptr = packages_find_first_node(parent->children, "sigs");
    if(!ptr)
        return;

    for(ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if(ptr->type != XML_ELEMENT_NODE || !xmlStrEqual(ptr->name, (xmlChar*)"cert"))
            continue;

        idx = xmlGetProp(ptr, (xmlChar*)"index");
        key = xmlGetProp(ptr, (xmlChar*)"key");
        if(idx && key)
        {
            INFO("Adding used cert index %s\n", idx);
            xmlHashAddEntry(keys, idx, key);
            xmlFree(idx);
        }
        else
        {
            xmlFree(idx);
            xmlFree(key);
        }
    }
}

static void packages_xmlchar_hash_deallocator(void *payload, xmlChar *name)
{
    xmlFree(payload);
}

static void packages_keys_add_used_idx(xmlNode *parent, xmlDict *used_indexes)
{
    xmlChar *idx;
    xmlNode *ptr = packages_find_first_node(parent->children, "sigs");
    if(!ptr)
        return;

    for(ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if(ptr->type != XML_ELEMENT_NODE || !xmlStrEqual(ptr->name, (xmlChar*)"cert"))
            continue;

        idx = xmlGetProp(ptr, (xmlChar*)"index");
        if(idx)
        {
            xmlDictLookup(used_indexes, idx, -1);
            xmlFree(idx);
        }
    }
}

const const xmlChar* packages_keys_get_unused_idx(xmlDict *used_indexes)
{
    int idx = -1, idx_len;
    char buff[12];
    do
    {   
        ++idx;
        idx_len = snprintf(buff, sizeof(buff), "%d", idx);
    } while(xmlDictExists(used_indexes, (xmlChar*)buff, idx_len));

    return xmlDictLookup(used_indexes, (xmlChar*)buff, idx_len);
}

void packages_keys_update_package(xmlNode *node, xmlHashTable *keys, xmlHashTable *key_mapping, xmlDict *used_indexes)
{
    xmlChar *idx;
    xmlChar *key;
    const xmlChar *mapped_idx;
    xmlNode *ptr = packages_find_first_node(node->children, "sigs");
    if(!ptr)
        return;

    for(ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if(ptr->type != XML_ELEMENT_NODE || !xmlStrEqual(ptr->name, (xmlChar*)"cert"))
            continue;

        idx = xmlGetProp(ptr, (xmlChar*)"index");
        if(idx && (key = xmlHashLookup(keys, idx)))
        {
            mapped_idx = xmlHashLookup(key_mapping, idx);
            if(!mapped_idx)
            {
                mapped_idx = packages_keys_get_unused_idx(used_indexes);
                xmlHashAddEntry(key_mapping, idx, (void*)mapped_idx);
                INFO("new mapped idx %s to %s", idx, mapped_idx);
                xmlSetProp(ptr, (xmlChar*)"key", key);
            }
            INFO("Replacing cert %s with %s", idx, mapped_idx);
            xmlSetProp(ptr, (xmlChar*)"index", mapped_idx);
            xmlFree(idx);
            return;
        }
        xmlFree(idx);
    }
}

static void packages_users_add_used(xmlNode *parent, xmlDict *used_users)
{
    xmlChar *id = xmlGetProp(parent, (xmlChar*)"userId");
    if(!id)
        return;

    INFO("Adding used user %s\n", id);
    xmlDictLookup(used_users, id, -1);
    xmlFree(id);
}

static const xmlChar *packages_users_get_unused(xmlDict *used_users)
{
    int id = 10010;
    int len;
    char buff[12];
    do
    {   
        ++id;
        len = snprintf(buff, sizeof(buff), "%d", id);
    } while(xmlDictExists(used_users, (xmlChar*)buff, len));

    return xmlDictLookup(used_users, (xmlChar*)buff, len);    
}

static void packages_users_update_package(xmlNode *node, xmlDict *used_users)
{
    xmlChar *id = xmlGetProp(node, (xmlChar*)"userId");
    if(!id)
        return;

    if(xmlDictExists(used_users, id, -1))
    {
        const xmlChar *new_id = packages_users_get_unused(used_users);
        INFO("Replacing user %s with %s\n", id, new_id);
        xmlSetProp(node, (xmlChar*)"userId", new_id);
    }
    xmlFree(id);
}

// adb push out/target/product/hammerhead/system/bin/vold /sdcard/multirom/roms/omni-4.4.4-20140728-hammer/system/bin/ && adb shell chmod 0755 /sdcard/multirom/roms/omni-4.4.4-20140728-hammer/system/bin/vold && adb shell chown 0:2000 /sdcard/multirom/roms/omni-4.4.4-20140728-hammer/system/bin/vold
// adb push out/target/product/hammerhead/system/bin/vold /system/bin/ && adb shell chmod 0755 /system/bin/vold && adb shell chown 0:2000 /system/bin/vold
static int packages_build_hash_from_node(xmlNode *node, char *const allowed_children_names[], struct packages_hash_scanner_data *data, int type)
{
    xmlChar *name;
    xmlNode *ptr;
    int i;
    int counter = 0;
    xmlHashTable *table;

    switch(type)
    {
        case PACKAGE_HASH_TYPE_SHARED:
            table = data->shared_packages_hash;
            break;
        case PACKAGE_HASH_TYPE_ROM:
            table = data->rom_packages_hash;
            break;
        default:
            ERROR("Unknown hash type");
            return -1;
    }

    for(ptr = node->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;

        for(i = 0; allowed_children_names[i]; ++i)
        {
            if(!xmlStrEqual(ptr->name, (xmlChar*)allowed_children_names[i]))
                continue;

            name = xmlGetProp(ptr, (xmlChar*)"name");
            if(!name)
            {
                ERROR("Failed to get name attr for node %s\n", allowed_children_names[i]);
                break;
            }

            if(i == ALLOWED_PACKAGE_TAG_UPDATED_PACKAGE)
            {
                name = xmlRealloc(name, xmlStrlen(name) + UPDATED_TAG_LEN + 1);
                strcat((char*)name, updated_tag);
            }

            INFO("Adding %s: %s\n", allowed_children_names[i], name);
            xmlHashAddEntry(table, name, ptr);
            xmlFree(name);

            if(type == PACKAGE_HASH_TYPE_ROM)
            {
                data->last_nodes[i] = ptr;
                packages_keys_add_used_idx(ptr, data->rom_used_key_indexes);
                packages_users_add_used(ptr, data->rom_used_user_ids);
            }
            else if(type == PACKAGE_HASH_TYPE_SHARED)
            {
                packages_keys_add_package(ptr, data->shared_keys);
            }

            ++counter;
            break;
        }
    }

    if(type == PACKAGE_HASH_TYPE_ROM)
    {
        for(i = 0; allowed_children_names[i]; ++i)
        {
            if(!data->last_nodes[i])
            {
                if(i != 0) data->last_nodes[i] = data->last_nodes[i-1];
                else       data->last_nodes[i] = node->children;
            }
        }
    }

    return counter;
}

static int packages_build_hash(xmlDoc *doc, struct packages_hash_scanner_data *data, int type)
{
    xmlNode *ptr;
    int count;

    ptr = packages_find_first_node(xmlDocGetRootElement(doc), "packages");
    if(!ptr)
    {
        ERROR("Failed to find packages node in xml!\n");
        return -1;
    }

    count = packages_build_hash_from_node(ptr, allowed_package_tags, data, type);
    if(count < 0)
    {
        ERROR("Failed to build hash for packages!\n");
        return -1;
    }
    INFO("Got %d package entries\n", count);
    return 0;
}

static void packages_hash_compare_scanner(void *payload, void *data, xmlChar *name)
{
    struct packages_hash_scanner_data *d = data;
    xmlChar *codePath;
    xmlNode *node = payload;
    xmlNode *other_node;
    int tag_idx;
    int data_app;

    codePath = xmlGetProp(node, (xmlChar*)"codePath");
    if(!codePath)
    {
        ERROR("Failed to get codepath for package %s!\n", name);
        return;
    }

    data_app = (xmlStrncmp(codePath, (xmlChar*)"/system/", 8) != 0);

    other_node = xmlHashLookup(d->rom_packages_hash, name);
    if(other_node)
    {
        xmlChar *uuid_rom = xmlGetProp(other_node, (xmlChar*)"userId");
        xmlChar *uuid_share = xmlGetProp(node, (xmlChar*)"userId");
        INFO("scanner: Both have %s %s (uuid: %s %s)\n", name, codePath, uuid_rom, uuid_share);
        xmlChar *codePath_other = xmlGetProp(other_node, (xmlChar*)"codePath");
        if(codePath_other && !xmlStrEqual(codePath_other, codePath))
            ERROR("CODE PATH DIFFERS %s vs %s", codePath_other, codePath);
        //if(uuid_rom && uuid_share && !xmlStrEqual(uuid_rom, uuid_share))
            //ERROR("DIFFERENT UUID IN ROM AND SHARE XMLS -----------------------------------------------------------\n");
        xmlFree(codePath_other);
        xmlFree(uuid_rom);
        xmlFree(uuid_share);
    }
    else if(data_app)
    {
        INFO("scanner: Adding new data package %s %s\n", name, codePath);
        tag_idx = packages_allowed_tags_get_idx((char*)node->name);
        if(tag_idx < 0)
        {
            ERROR("Failed to get tag idx for %s\n", node->name);
            xmlFree(codePath);
            return;
        }

        node = xmlCopyNode(node, 1);
        packages_keys_update_package(node, d->shared_keys, d->rom_key_mapping, d->rom_used_key_indexes);
        packages_users_update_package(node, d->rom_used_user_ids);
        node = xmlAddNextSibling(d->last_nodes[tag_idx], node);
        if(node)
        {
            d->last_nodes[tag_idx] = node;
            ++d->new_data_apps;
        }
        else
        {
            ERROR("Failed to add new node with app %s!\n", name);
            xmlFreeNode(node);
        }
    }
#if 0
    else if(xmlStrEqual(node->name, (xmlChar*)"updated-package"))
    {
        xmlChar *real_name = xmlStrdup(name);
        real_name[xmlStrlen(real_name) - UPDATED_TAG_LEN] = 0;
        other_node = xmlHashLookup(d->rom_packages_hash, real_name);
        if(other_node)
        {
            xmlFree(codePath);
            codePath = xmlGetProp(other_node, (xmlChar*)"codePath");
            if(codePath && xmlStrncmp(codePath, (xmlChar*)"/system/", 8) == 0)
            {
                INFO("Setting system package %s %s to updated-package\n", name, codePath);
                xmlNodeSetName(other_node, (xmlChar*)"updated-package");
                ++d->new_sys_apps;
                /*if(d->last_nodes[ALLOWED_PACKAGE_TAG_PACKAGE] == other_node)
                    d->last_nodes[ALLOWED_PACKAGE_TAG_PACKAGE] = other_node->prev;
                xmlUnlinkNode(other_node);
                xmlFreeNode(other_node);
                node = xmlCopyNode(node, 1);
                node = xmlAddNextSibling(d->last_nodes[ALLOWED_PACKAGE_TAG_UPDATED_PACKAGE], node);
                if(node)
                {
                    d->last_nodes[ALLOWED_PACKAGE_TAG_UPDATED_PACKAGE] = node;
                    ++d->new_sys_apps;
                }
                else
                {
                    ERROR("Failed to replace update-package node for app %s\n", name);
                }*/
            }
            else
                ERROR("Failed to get codePath for package %s\n", name);
        }
        else
            INFO("Updated package %s isn't in rom_packages\n", real_name);
        xmlFree(real_name);
    }
#endif
    else
    {
        INFO("scanner: skipping absent system package %s %s\n", name, codePath);
    }
    xmlFree(codePath);
}

static int packages_handle(struct multirom_rom *rom, char *group_path)
{
    char buff[128];
    char buff2[128];
    xmlDoc *rom_packages = NULL;
    xmlDoc *shared_packages = NULL;
    xmlSaveCtxt *save_ctx = NULL;
    int res = -1;
    struct packages_hash_scanner_data scann_data = { 
        .shared_packages_hash = NULL,
        .rom_packages_hash = NULL,
        .shared_keys = NULL,
        .rom_used_key_indexes = NULL,
        .rom_key_mapping = NULL,
        .rom_used_user_ids = NULL,
        .new_data_apps = 0,
        .new_sys_apps = 0,
    };

    LIBXML_TEST_VERSION

    snprintf(buff2, sizeof(buff2), "%s/last_booted_rom", group_path);
    FILE *f = fopen(buff2, "r");
    if(f)
    {
        fgets(buff2, sizeof(buff2), f);
        fclose(f);

        snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
        INFO("Copying %s to %s, last_booted_rom\n", buff, buff2);
        copy_file(buff, buff2);
    }

    // Write last_booted_rom
    snprintf(buff, sizeof(buff), "%s/share_packages.xml", rom->base_path);
    snprintf(buff2, sizeof(buff2), "%s/last_booted_rom", group_path);
    write_file2(buff2, buff, O_TRUNC);

    snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
    snprintf(buff2, sizeof(buff2), "%s/system/shared_packages.xml", group_path);
    if(access(buff2, F_OK) >= 0)
    {
        INFO("%s exists, moving to packages.xml\n", buff2);
        rename(buff2, buff);
    }

    if(access(buff, F_OK) < 0)
    {
        INFO("%s doesn't exist, skipping packages handling\n", buff);
        res = 0;
        goto exit;
    }

    shared_packages = xmlReadFile(buff, NULL, 0);
    if(!shared_packages)
    {
        ERROR("Parsing of %s failed!\n", buff);
        goto exit;
    }

    snprintf(buff, sizeof(buff), "%s/share_packages.xml", rom->base_path);
    rom_packages = xmlReadFile(buff, NULL, 0);
    if(!rom_packages)
    {
        ERROR("Parsing of %s failed! copying packages.xml to shared_packages.xml\n", buff);
        snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
        snprintf(buff2, sizeof(buff2), "%s/system/shared_packages.xml", group_path);
        copy_file(buff, buff2);
        remove(buff);
        res = 0;
        goto exit;

        /*ERROR("Parsing of %s failed!\n", buff);
        snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
        rom_packages = xmlReadFile(buff, NULL, 0);
        if(!rom_packages)
        {
            ERROR("Parsing of %s failed!\n", buff);
            remove(buff);
            res = 0;
            goto exit;
        }*/
    }

    scann_data.shared_packages_hash = xmlHashCreate(0);
    scann_data.rom_packages_hash = xmlHashCreate(0);
    scann_data.shared_keys = xmlHashCreate(0);
    scann_data.rom_used_key_indexes = xmlDictCreate();
    scann_data.rom_key_mapping = xmlHashCreate(64);
    scann_data.rom_used_user_ids = xmlDictCreate();
    if (packages_build_hash(rom_packages, &scann_data, PACKAGE_HASH_TYPE_ROM) < 0 ||
        packages_build_hash(shared_packages, &scann_data, PACKAGE_HASH_TYPE_SHARED) < 0)
    {
        goto exit;
    }

    xmlHashScan(scann_data.shared_packages_hash, packages_hash_compare_scanner, &scann_data);

    if(scann_data.new_sys_apps > 0 || scann_data.new_data_apps > 0)
    {
        INFO("new_sys_apps: %d, new_data_apps: %d\n", scann_data.new_sys_apps, scann_data.new_data_apps);
        snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
        save_ctx = xmlSaveToFilename(buff, NULL, XML_SAVE_FORMAT);
        if(!save_ctx)
        {
            ERROR("Failed to create XML save ctx!\n");
            goto exit;
        }

        res = xmlSaveDoc(save_ctx, rom_packages) >= 0 ? 0 : -1;
        xmlSaveClose(save_ctx);

        snprintf(buff2, sizeof(buff2), "%s/system/packages-mrom.xml", group_path);
        if(copy_file(buff, buff2) < 0)
        {
            ERROR("Failed to copy %s to %s!\n", buff, buff2);
            res = -1;
            goto exit;
        }
    }
    else
    {
        snprintf(buff, sizeof(buff), "%s/share_packages.xml", rom->base_path);
        snprintf(buff2, sizeof(buff2), "%s/system/packages.xml", group_path);
        INFO("No changes, copying %s to %s\n", buff, buff2);
        copy_file(buff, buff2);
    }

exit:
    if(shared_packages)
        xmlFreeDoc(shared_packages);
    
    if(rom_packages)
        xmlFreeDoc(rom_packages);

    if(scann_data.shared_packages_hash)
    {
        xmlHashFree(scann_data.shared_packages_hash, NULL);
        xmlHashFree(scann_data.rom_packages_hash, NULL);
        xmlHashFree(scann_data.shared_keys, packages_xmlchar_hash_deallocator);
        xmlDictFree(scann_data.rom_used_key_indexes);
        xmlHashFree(scann_data.rom_key_mapping, NULL);
        xmlDictFree(scann_data.rom_used_user_ids);
    }

    xmlCleanupParser();
    return res;
}

#define SHRDIR_WITH_DATA_ONLY 0x01
#define SHRDIR_WITH_APP_ONLY  0x02
#define SHRDIR_CREATE_ONLY    0x04

struct share_dir_info
{
    const char *path;
    mode_t perms;
    const char *owner;
    int flags;
};

static const struct share_dir_info share_dirs[] = {
    {
        .path = "app",
        .perms = 0771,
        .owner = "system",
        .flags = 0,
    },
    {
        .path = "app-lib",
        .perms = 0771,
        .owner = "system",
        .flags = 0,
    },
    {
        .path = "app-asec",
        .perms = 0700,
        .owner = "root",
        .flags = 0,
    },
    {
        .path = "app-private",
        .perms = 0771,
        .owner = "system",
        .flags = 0,
    },
    {
        .path = "system",
        .perms = 0771,
        .owner = "system",
        .flags = 0,
    },
    {
        .path = "misc",
        .perms = 01770,
        .owner = "system",
        .flags = SHRDIR_CREATE_ONLY,
    },
    {
        .path = "misc/systemkeys",
        .perms = 0700,
        .owner = "system",
        .flags = 0,
    },

    { .path = NULL }
};

static const struct share_dir_info keep_dirs[] = {
    {
        .path = "system/users",
        .perms = 0771,
        .owner = "system",
        .flags = 0,
    },
    {
        .path = "system/sync",
        .perms = 0700,
        .owner = "system",
        .flags = 0,
    },

    { .path = NULL }
};

static int share_setup_apps(struct multirom_rom *rom, char *group_path)
{
    int i;
    char src[128];
    char dst[128];

    INFO("Setting up app share with %s\n", group_path);

    // mount shares
    for(i = 0; share_dirs[i].path; ++i)
    {
        snprintf(src, sizeof(src), "%s/%s", group_path, share_dirs[i].path);
        if(access(src, F_OK) < 0)
        {
            INFO("%s doesn't exist, creating \n", src);
            mkdir_with_perms(src, share_dirs[i].perms, share_dirs[i].owner, share_dirs[i].owner);
        }

        snprintf(dst, sizeof(dst), "/data/%s", share_dirs[i].path);
        if(access(dst, F_OK) < 0)
        {
            INFO("%s doesn't exist, creating", dst);
            mkdir_with_perms(dst, share_dirs[i].perms, share_dirs[i].owner, share_dirs[i].owner);
        }

        if(share_dirs[i].flags & SHRDIR_CREATE_ONLY)
            continue;

        INFO("Mounting %s to %s\n", src, dst);
        if(mount(src, dst, "", MS_BIND, "") < 0)
        {
            ERROR("Mount %s to %s failed: %s\n", src, dst, strerror(errno));
            return -1;
        }
    }

    // mount keeps
    for(i = 0; keep_dirs[i].path; ++i)
    {
        snprintf(dst, sizeof(dst), "/data/%s", keep_dirs[i].path);
        if(access(dst, F_OK) < 0)
        {
            INFO("%s doesn't exist, trying to create\n", dst);
            if(mkdir_with_perms(dst, keep_dirs[i].perms, keep_dirs[i].owner, keep_dirs[i].owner) < 0)
            {
                ERROR("Failed to create %s, skipping\n", dst);
                continue;
            }
        }

        snprintf(src, sizeof(src), "%s/data/%s", rom->base_path, keep_dirs[i].path);
        if(access(src, F_OK) < 0)
        {
            INFO("%s doesn't exist, creating\n", src);
            mkdir_with_perms(src, keep_dirs[i].perms, keep_dirs[i].owner, keep_dirs[i].owner);
        }

        if(keep_dirs[i].flags & SHRDIR_CREATE_ONLY)
            continue;

        INFO("Mounting %s to %s\n", src, dst);
        if(mount(src, dst, "", MS_BIND, "") < 0)
        {
            ERROR("Mount %s to %s failed: %s\n", src, dst, strerror(errno));
            return -1;
        }
    }

    return 0;
}

int share_setup(struct multirom_rom *rom)
{
    char buff[128];
    char *group_path = NULL;
    struct stat info;
    int res = 0;

    snprintf(buff, sizeof(buff), "%s/share_app", rom->base_path);
    if(lstat(buff, &info) < 0)
        return 0;

    group_path = readlink_recursive(buff);

    if(!S_ISLNK(info.st_mode) || !group_path)
    {
        ERROR("share: invalid symlink %s\n", buff);
        goto exit;
    }

    res = share_setup_apps(rom, group_path);
    if(res >= 0)
        res = packages_handle(rom, group_path);

    multirom_copy_log(NULL, "../multirom_last_share.txt");

exit:
    free(group_path);
    return res;
}














/*
 * ==========================================================================================================================================================================
 * FIXME: might be useful yet, delete on release
 * ==========================================================================================================================================================================
 */
#if 0
static void packages_apptime_stored_load(struct multirom_rom *rom, xmlHashTable *stored_mtimes)
{
    FILE *f;
    char *r;
    char buff[128];
    int64_t *ts;

    snprintf(buff, sizeof(buff), "%s/share_sysapp_mtimes", rom->base_path);
    f = fopen(buff, "r");
    if(!f)
    {
        INFO("No stored system app mtimes.\n");
        return;
    }

    while(fgets(buff, sizeof(buff), f))
    {
        r = strchr(buff, ' ');
        if(!r)
            continue;
        *r++ = 0;
        INFO("Using stored system app mtime %s %s", buff, r);
        ts = malloc(sizeof(int64_t));
        *ts = strtoll(r, NULL, 16);
        xmlHashAddEntry(stored_mtimes, (xmlChar*)buff, ts);
    }

    fclose(f);
}

static void packages_apptime_stored_writer(void *payload, void *data, xmlChar *name)
{
    FILE *f = data;
    int64_t *ts = payload;
    fprintf(f, "%s %llx\n", name, *ts);
}

static void packages_apptime_stored_save(struct multirom_rom *rom, xmlHashTable *stored_mtimes)
{
    FILE *f;
    char *r;
    char buff[128];
    int64_t *ts;

    snprintf(buff, sizeof(buff), "%s/share_sysapp_mtimes", rom->base_path);
    f = fopen(buff, "w");
    if(!f)
    {
        INFO("Failed to open %s for writing\n", buff);
        return;
    }

    xmlHashScan(stored_mtimes, packages_apptime_stored_writer, f);
    fclose(f);
}

static void packages_apptime_stored_deallocator(void *payload, xmlChar *name)
{
    free(payload);
}

struct packages_apptime_scanner_data
{
    xmlHashTable *stored_mtimes;
    int changed;
};

static inline int packages_apptime_scanner_look_for_apk(xmlNode *node, const char *apk_name, struct stat *info)
{
    int i;
    char buff[64];
    char * const dirs[] = { "app", "priv-app", NULL };

    if(!apk_name)
        return -1;

    for(i = 0; dirs[i]; ++i)
    {
        snprintf(buff, sizeof(buff), "/system/%s/%s", dirs[i], apk_name);
        if(stat(buff, info) >= 0)
        {
            INFO("app found in %s\n", buff);
            xmlSetProp(node, (xmlChar*)"codePath", (xmlChar*)buff);
            return 0;
        }
    }
    return -1;
}

static void packages_apptime_scanner(void *payload, void *data, xmlChar *name)
{
    struct stat info;
    xmlNode *node = payload;
    xmlChar *val;
    int64_t xml_mtime;
    int64_t stat_mtime;
    int64_t *stored_mtime;
    struct packages_apptime_scanner_data *d = data;

    val = xmlGetProp(node, (xmlChar*)"codePath");
    if(!val)
    {
        ERROR("Failed to get codepath for package %s!\n", name);
        return;
    }

    if(stat((char*)val, &info) < 0)
    {
        ERROR("Stat on %s failed\n", val);
        if(packages_apptime_scanner_look_for_apk(node, strrchr((char*)val, '/'), &info) < 0)
        {
            xmlFree(val);
            return;
        }
    }
    xmlFree(val);

    stat_mtime = ((int64_t)info.st_mtime)*1000 + info.st_mtime_nsec / 1000000;

    if((val = xmlGetProp(node, (xmlChar*)"ft")))
        xml_mtime = strtoll((char*)val, NULL, 16);
    else if((val = xmlGetProp(node, (xmlChar*)"ts")))
        xml_mtime = strtoll((char*)val, NULL, 0);
    else
    {
        ERROR("Couldn't find timestamp prop on package %s\n", name);
        return;
    }
    xmlFree(val);

    stored_mtime = xmlHashLookup(d->stored_mtimes, name);

    INFO("package %s: xml %lld, stat %lld, stored %lld\n", name, xml_mtime, stat_mtime, stored_mtime ? *stored_mtime : -1);
    //if(xml_mtime != stat_mtime)
    {
        //if(stored_mtime && *stored_mtime == stat_mtime)
        {
            INFO("stored_mtime equals stat_time, editing XML tree - %lld", *stored_mtime);
            char buff[17];
            snprintf(buff, sizeof(buff), "%llx", stat_mtime+1);
            xmlSetProp(node, (xmlChar*)"ft", (xmlChar*)buff);
            ++d->changed;
        }
    }

    if(!stored_mtime || *stored_mtime != stat_mtime)
    {
        INFO("Storing mtime %lld for %s", stat_mtime, name);
        stored_mtime = malloc(sizeof(int64_t));
        *stored_mtime = stat_mtime;
        xmlHashUpdateEntry(d->stored_mtimes, name, stored_mtime, packages_apptime_stored_deallocator);
    }
}

static int packages_apptime_modify(struct multirom_rom *rom, xmlHashTable *system_packages)
{
    struct packages_apptime_scanner_data scanner_data = {
        .stored_mtimes = xmlHashCreate(64),
        .changed = 0
    };

    packages_apptime_stored_load(rom, scanner_data.stored_mtimes);
    xmlHashScan(system_packages, packages_apptime_scanner, &scanner_data);
    packages_apptime_stored_save(rom, scanner_data.stored_mtimes);

    xmlHashFree(scanner_data.stored_mtimes, packages_apptime_stored_deallocator);
    return scanner_data.changed;
}
#endif
