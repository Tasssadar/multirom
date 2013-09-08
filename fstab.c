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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/mount.h>

#include "fstab.h"
#include "util.h"
#include "log.h"

// flags from system/core/fs_mgr/fs_mgr.c
struct flag_list {
    const char *name;
    unsigned flag;
};

static struct flag_list mount_flags[] = {
    { "noatime",    MS_NOATIME },
    { "noexec",     MS_NOEXEC },
    { "nosuid",     MS_NOSUID },
    { "nodev",      MS_NODEV },
    { "nodiratime", MS_NODIRATIME },
    { "ro",         MS_RDONLY },
    { "rw",         0 },
    { "remount",    MS_REMOUNT },
    { "bind",       MS_BIND },
    { "rec",        MS_REC },
    { "unbindable", MS_UNBINDABLE },
    { "private",    MS_PRIVATE },
    { "slave",      MS_SLAVE },
    { "shared",     MS_SHARED },
    { "sync",       MS_SYNCHRONOUS },
    { "defaults",   0 },
    { 0,            0 },
};

struct fstab *fstab_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if(!f)
    {
        ERROR("Failed to open fstab %s\n", path);
        return NULL;
    }

    struct fstab *t = mzalloc(sizeof(struct fstab));
    t->version = -1;

    const char *delim = " \t";
    char *saveptr = NULL;
    char *p;
    char line[1024];
    int len;
    struct fstab_part *part = NULL;
    while((p = fgets(line, sizeof(line), f)))
    {
        len = strlen(line);
        if(line[len-1] == '\n')
            line[len-1] = 0;

        while(isspace(*p))
            ++p;

        if(*p == '#' || *p == 0)
            continue;

        part = mzalloc(sizeof(struct fstab_part));

        if(!(p = strtok_r(line, delim, &saveptr)))
        {
            ERROR("Error first token\n");
            goto fail;
        }

        if(t->version == -1)
        {
            if(strstr(p, "/dev/") == p) t->version = 2;
            else                        t->version = 1;
        }

        if(t->version == 2)
            part->device = strdup(p);
        else
            part->path = strdup (p);

        if(!(p = strtok_r(NULL, delim, &saveptr)))
        {
            ERROR("Error second token\n");
            goto fail;
        }

        if(t->version == 2)
            part->path = strdup(p);
        else
            part->type = strdup(p);

        if(!(p = strtok_r(NULL, delim, &saveptr)))
        {
            ERROR("Error third token\n");
            goto fail;
        }

        if(t->version == 2)
            part->type = strdup(p);
        else
            part->device = strdup(p);

        if((p = strtok_r(NULL, delim, &saveptr)))
            fstab_parse_options(p, part);

        if((p = strtok_r(NULL, delim, &saveptr)))
            part->options2 = strdup(p);

        list_add(part, &t->parts);
        ++t->count;
        part = NULL;
    }

    return t;

fail:
    fclose(f);
    free(part);
    fstab_destroy(t);
    return NULL;
}

void fstab_destroy(struct fstab *f)
{
    list_clear(&f->parts, fstab_destroy_part);
    free(f);
}

void fstab_destroy_part(struct fstab_part *p)
{
    free(p->path);
    free(p->type);
    free(p->device);
    free(p->options);
    free(p->options2);
    free(p);
}

void fstab_dump(struct fstab *f)
{
    INFO("Dumping fstab:\n");
    INFO("version: %d\n", f->version);
    INFO("count: %d\n", f->count);

    int i;
    for(i = 0; i < f->count; ++i)
    {
        INFO("Partition %d:\n", i);
        INFO("    path: %s\n", f->parts[i]->path);
        INFO("    device: %s\n", f->parts[i]->device);
        INFO("    type: %s\n", f->parts[i]->type);
        INFO("    mountflags: 0x%lX\n", f->parts[i]->mountflags);
        INFO("    options: %s\n", f->parts[i]->options);
        INFO("    options2: %s\n", f->parts[i]->options2);
    }
}

struct fstab_part *fstab_find_by_path(struct fstab *f, const char *path)
{
    int i;
    for(i = 0; i < f->count; ++i)
        if(strcmp(f->parts[i]->path, path) == 0)
            return f->parts[i];

    return NULL;
}

void fstab_parse_options(char *opt, struct fstab_part *part)
{
    int i;
    char *p;
    char *saveptr = NULL;

    part->options = malloc(strlen(opt) + 2); // NULL and possible trailing comma
    part->options[0] = 0;

    p = strtok_r(opt, ",", &saveptr);
    while(p)
    {
        for(i = 0; mount_flags[i].name; ++i)
        {
            if(strcmp(mount_flags[i].name, p) == 0)
            {
                part->mountflags |= mount_flags[i].flag;
                break;
            }
        }

        if(!mount_flags[i].name)
        {
            strcat(part->options, p);
            strcat(part->options, ",");
        }

        p = strtok_r(NULL, ",", &saveptr);
    }

    int len = strlen(part->options);
    if(len != 0)
    {
        part->options[len-1] = 0; // remove trailing comma
        part->options = realloc(part->options, len);
    }
    else
    {
        free(part->options);
        part->options = NULL;
    }
}
