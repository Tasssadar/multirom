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

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlstring.h>
#include <libxml/hash.h>

#include "share.h"
#include "util.h"
#include "log.h"

static xmlNode *packages_find_first_node(xmlNode *root, const char *name)
{
    xmlNode *ptr;
    for (ptr = root; ptr; ptr = ptr->next)
        if (ptr->type == XML_ELEMENT_NODE && xmlStrEqual(ptr->name, (xmlChar*)name))
            return ptr;
    return NULL;
}

static int packages_build_hash_from_node(xmlNode *node, char *const allowed_children_names[], xmlHashTable *table, xmlNode **last_nodes)
{
    xmlChar *name;
    xmlNode *ptr;
    int i;
    int counter = 0;

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

            INFO("Adding %s: %s\n", allowed_children_names[i], name);
            xmlHashAddEntry(table, name, ptr);
            xmlFree(name);
            if(last_nodes)
                last_nodes[i] = ptr;
            ++counter;
            break;
        }
    }

    if(last_nodes)
    {
        for(i = 0; allowed_children_names[i]; ++i)
        {
            if(!last_nodes[i])
            {
                if(i != 0) last_nodes[i] = last_nodes[i-1];
                else       last_nodes[i] = node->children;
            }
        }
    }

    return counter;
}

static char * const allowed_package_tags[] = { "package", "updated-package", NULL };
#define ALLOWED_PACKAGE_TAGS_COUNT 2

static int packages_allowed_tags_get_idx(const char *name)
{
    int i;
    for(i = 0; i < ALLOWED_PACKAGE_TAGS_COUNT; ++i)
        if(strcmp(allowed_package_tags[i], name) == 0)
            return i;
    return -1;
}

static int packages_build_hash(xmlDoc *doc, xmlHashTable *packages, xmlNode **last_nodes)
{
    xmlNode *ptr;
    int count;

    ptr = packages_find_first_node(xmlDocGetRootElement(doc), "packages");
    if(!ptr)
    {
        ERROR("Failed to find packages node in xml!\n");
        return -1;
    }

    count = packages_build_hash_from_node(ptr, allowed_package_tags, packages, last_nodes);
    if(count < 0)
    {
        ERROR("Failed to build hash for packages!\n");
        return -1;
    }
    INFO("Got %d package entries\n", count);
    return 0;
}

struct packages_hash_scanner_data
{
    xmlHashTable *rom_packages_hash;
    xmlHashTable *shared_packages_hash;
    xmlNode *last_nodes[ALLOWED_PACKAGE_TAGS_COUNT];
    int restored;
};

static void packages_hash_compare_scanner(void *node, void *data, xmlChar *name)
{
    struct packages_hash_scanner_data *d = data;
    if(xmlHashLookup(d->rom_packages_hash, name))
    {
        INFO("scanner: Both have package %s\n", name);
        xmlHashRemoveEntry(d->rom_packages_hash, name, NULL);
    }
    else
    {
        xmlChar *codePath = xmlGetProp(node, (xmlChar*)"codePath");
        if(!codePath)
        {
            ERROR("Failed to get codepath for package %s!\n", name);
            return;
        }

        if(xmlStrncmp(codePath, (xmlChar*)"/system/", 8) == 0)
        {
            INFO("scanner: Adding removed system package %s %s\n", name, codePath);
            xmlNode *new_node = xmlCopyNode(node, 1);
            int tag_idx = packages_allowed_tags_get_idx((char*)new_node->name);
            if(tag_idx < 0)
            {
                ERROR("Failed to get tag idx for %s\n", new_node->name);
                xmlFree(codePath);
                return;
            }

            new_node = xmlAddNextSibling(d->last_nodes[tag_idx], new_node);
            if(new_node)
            {
                d->last_nodes[tag_idx] = new_node;
                ++d->restored;
            }
            else
                ERROR("Failed to add new node with app %s!\n", name);
        }
        else
        {
            INFO("scanner: skipping removed user package %s %s\n", name, codePath);
        }

        xmlFree(codePath);
    }
}

static void packages_hash_new_apps_scanner(void *node, void *data, xmlChar *name)
{
    struct packages_hash_scanner_data *d = data;
    xmlChar *codePath = xmlGetProp(node, (xmlChar*)"codePath");
    if(!codePath)
    {
        ERROR("Failed to get codepath for package %s!\n", name);
        return;
    }

    INFO("scanner: adding new package %s %s\n", name, codePath);
    //xmlNode *new_node = xmlCopyNode(node, 1);
    //xmlAddChild(rom_packages_root, new_node);
    xmlFree(codePath);
}

static int packages_handle(struct multirom_rom *rom, char *group_path)
{
    char buff[128];
    char buff2[128];
    xmlDoc *rom_packages = NULL;
    xmlDoc *shared_packages = NULL;
    xmlSaveCtxt *save_ctx = NULL;
    xmlHashTable *shared_packages_hash = NULL;
    xmlHashTable *rom_packages_hash = NULL;
    xmlHashTable *last_package_nodes = NULL;
    struct packages_hash_scanner_data scann_data;
    int res = -1, new_packages;

    LIBXML_TEST_VERSION

    snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
    if(access(buff, F_OK) < 0)
    {
        INFO("%s doesn't exist, skipping packages handling\n", buff);
        return 0;
    }

    rom_packages = xmlReadFile(buff, NULL, 0);
    if(!rom_packages)
    {
        ERROR("Parsing of %s failed!\n", buff);
        goto exit;
    }

    snprintf(buff, sizeof(buff), "%s/system/shared_packages.xml", group_path);
    if(access(buff, F_OK) < 0)
    {
        snprintf(buff2, sizeof(buff2), "%s/system/packages.xml", group_path);
        INFO("%s doesn't exist, copying packages.xml\n", buff);
        copy_file(buff2, buff);
        res = 0;
        goto exit;
    }

    shared_packages = xmlReadFile(buff, NULL, 0);
    if(!shared_packages)
    {
        ERROR("Parsing of %s failed!\n", buff);
        goto exit;
    }

    shared_packages_hash = xmlHashCreate(0);
    rom_packages_hash = xmlHashCreate(0);
    last_package_nodes = xmlHashCreate(3);
    if (packages_build_hash(shared_packages, shared_packages_hash, NULL) < 0 ||
        packages_build_hash(rom_packages, rom_packages_hash, scann_data.last_nodes) < 0)
    {
        goto exit;
    }

    // scan_data.last_nodes initialized in packages_build_hash above
    scann_data.shared_packages_hash = shared_packages_hash;
    scann_data.rom_packages_hash = rom_packages_hash;
    scann_data.restored = 0;

    xmlHashScan(shared_packages_hash, packages_hash_compare_scanner, &scann_data);
    xmlHashScan(rom_packages_hash, packages_hash_new_apps_scanner, &scann_data); // TODO: Remove

    new_packages = xmlHashSize(rom_packages_hash);
    if(new_packages > 0 || scann_data.restored)
    {
        INFO("New packages: %d, restored: %d", new_packages, scann_data.restored);
        snprintf(buff, sizeof(buff), "%s/system/packages.xml", group_path);
        save_ctx = xmlSaveToFilename(buff, NULL, 0);
        if(!save_ctx)
        {
            ERROR("Failed to create XML save ctx!\n");
            goto exit;
        }

        res = xmlSaveDoc(save_ctx, rom_packages) >= 0 ? 0 : -1;
        xmlSaveClose(save_ctx);

        snprintf(buff2, sizeof(buff2), "%s/system/shared_packages.xml", group_path);
        if(copy_file(buff, buff2) < 0)
        {
            ERROR("Failed to copy %s to %s!", buff, buff2);
            res = -1;
            goto exit;
        }
    }
    else
    {
        INFO("No changes in package.xml\n");
    }

exit:
    if(shared_packages)
        xmlFreeDoc(shared_packages);
    if(rom_packages)
        xmlFreeDoc(rom_packages);
    if(shared_packages_hash)
        xmlHashFree(shared_packages_hash, NULL);
    if(rom_packages_hash)
        xmlHashFree(rom_packages_hash, NULL);
    xmlCleanupParser();
    return res;
}

static int share_setup_apps(struct multirom_rom *rom, char *group_path)
{
    int i;
    char src[128];
    char dst[128];

    INFO("Setting up app share with %s\n", group_path);

    static const char *keep_dirs[] =   { "system/users", "system/sync",  NULL };
    static const mode_t keep_perms[] = { 0771,           0700,                };
    static const char *keep_owners[] =  { "system",       "system",           };

    static const char *share_dirs[] =   { "app",     "app-lib", "app-asec", "app-private", "system", NULL };
    static const mode_t share_perms[] = { 0771,      0771,      0700,       0771,          0771           };
    static const char *share_owners[] = { "system",  "system",  "root",     "system",      "system"       };

    // mount shares
    for(i = 0; share_dirs[i]; ++i)
    {
        snprintf(src, sizeof(src), "%s/%s", group_path, share_dirs[i]);
        if(access(src, F_OK) < 0)
        {
            INFO("%s doesn't exist, skipping\n", src);
            continue;
        }

        snprintf(dst, sizeof(dst), "/data/%s", share_dirs[i]);
        if(access(dst, F_OK) < 0)
        {
            INFO("%s doesn't exist, creating", dst);
            mkdir_with_perms(dst, share_perms[i], share_owners[i], share_owners[i]);
        }

        INFO("Mounting %s to %s\n", src, dst);
        if(mount(src, dst, "", MS_BIND, "") < 0)
        {
            ERROR("Mount %s to %s failed: %s\n", src, dst, strerror(errno));
            return -1;
        }
    }

    // mount keeps
    for(i = 0; keep_dirs[i]; ++i)
    {
        snprintf(dst, sizeof(dst), "/data/%s", keep_dirs[i]);
        if(access(dst, F_OK) < 0)
        {
            INFO("%s doesn't exist, trying to create\n", dst);
            if(mkdir_recursive_with_perms(dst, keep_perms[i], keep_owners[i], keep_owners[i]) < 0)
            {
                ERROR("Failed to create %s, skipping\n", dst);
                continue;
            }
        }

        snprintf(src, sizeof(src), "%s/data/%s", rom->base_path, keep_dirs[i]);
        if(access(src, F_OK) < 0)
        {
            INFO("%s doesn't exist, creating\n", src);
            mkdir_recursive_with_perms(src, keep_perms[i], keep_owners[i], keep_owners[i]);
        }

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

exit:
    free(group_path);
    return res;
}
