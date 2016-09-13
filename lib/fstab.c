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
#include <unistd.h>
#include <dirent.h>

#include "fstab.h"
#include "util.h"
#include "log.h"
#include "containers.h"

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

struct fstab *fstab_create_empty(int version)
{
    struct fstab *t = mzalloc(sizeof(struct fstab));
    t->version = version;
    return t;
}

struct fstab *fstab_load(const char *path, int resolve_symlinks)
{
    FILE *f = fopen(path, "re");
    if(!f)
    {
        ERROR("Failed to open fstab %s\n", path);
        return NULL;
    }

    struct fstab *t = fstab_create_empty(-1);
    const char *delim = " \t";
    char *saveptr = NULL;
    char *p;
    char line[1024];
    int len, is_dev_on_line = 0;
    struct fstab_part *part = NULL;

    t->path = strdup(path);

    while((p = fgets(line, sizeof(line), f)))
    {
        len = strlen(line);
        if(line[len-1] == '\n')
            line[len-1] = 0;

        while(isspace(*p))
            ++p;

        if(*p == '#' || *p == 0)
            continue;

        if(t->version == -1)
            is_dev_on_line = (strstr(line, "/dev/") != NULL);

        part = mzalloc(sizeof(struct fstab_part));

        if(!(p = strtok_r(line, delim, &saveptr)))
        {
            ERROR("Error first token\n");
            goto fail;
        }

        if(t->version == -1)
        {
            if(is_dev_on_line && strstr(p, "/dev/") != p)
                t->version = 1;
            else
                t->version = 2;
        }

        if(t->version == 2)
            part->device = resolve_symlinks ? readlink_recursive(p) : strdup(p);
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
            part->device = resolve_symlinks ? readlink_recursive(p) : strdup(p);

        if((p = strtok_r(NULL, delim, &saveptr)))
        {
            part->options_raw = strdup(p);
            fstab_parse_options(p, part);
        }

        if((p = strtok_r(NULL, delim, &saveptr)))
            part->options2 = strdup(p);

        // Check device
        if(!part->device)
        {
            if (strcmp(part->path, "/data") == 0 || strcmp(part->path, "/system") == 0 ||
                strcmp(part->path, "/boot") == 0 || strcmp(part->path, "/cache") == 0)
            {
                ERROR("fstab: device for part %s does not exist!\n", part->path);
            }
            fstab_destroy_part(part);
            part = NULL;
            continue;
        }

        list_add(&t->parts, part);
        ++t->count;
        part = NULL;
    }

    fclose(f);
    return t;

fail:
    fclose(f);
    free(part);
    fstab_destroy(t);
    return NULL;
}

int fstab_save(struct fstab *f, const char *path)
{
    int i;
    FILE *out;
    struct fstab_part *p;

    out = fopen(path, "we");
    if(!f)
    {
        ERROR("fstab_save: failed to open %s!", path);
        return -1;
    }

    for(i = 0; i < f->count; ++i)
    {
        p = f->parts[i];
        if(p->disabled)
            fputc('#', out);

        if(f->version == 1)
            fprintf(out, "%s\t%s\t%s\t", p->path, p->type, p->device);
        else
            fprintf(out, "%s\t%s\t%s\t", p->device, p->path, p->type);
        fprintf(out, "%s\t%s\n", p->options_raw, p->options2);
    }
    fclose(out);
    return 0;
}

void fstab_destroy(struct fstab *f)
{
    list_clear(&f->parts, fstab_destroy_part);
    free(f->path);
    free(f);
}

void fstab_destroy_part(struct fstab_part *p)
{
    free(p->path);
    free(p->type);
    free(p->device);
    free(p->options);
    free(p->options_raw);
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

struct fstab_part *fstab_find_first_by_path(struct fstab *f, const char *path)
{
    int i;
    for(i = 0; i < f->count; ++i)
        if(strcmp(f->parts[i]->path, path) == 0)
            return f->parts[i];

    return NULL;
}

struct fstab_part *fstab_find_next_by_path(struct fstab *f, const char *path, struct fstab_part *prev)
{
    int i, found_prev = 0;
    for(i = 0; i < f->count; ++i)
    {
        if(!found_prev)
        {
            if(f->parts[i] == prev)
                found_prev = 1;
        }
        else if(strcmp(f->parts[i]->path, path) == 0)
        {
            return f->parts[i];
        }
    }
    return NULL;
}

int fstab_disable_parts(struct fstab *f, const char *path)
{
    int i, cnt = 0;

    for(i = 0; i < f->count; ++i)
    {
        if(strcmp(f->parts[i]->path, path) == 0)
        {
            f->parts[i]->disabled = 1;
            ++cnt;
        }
    }

    if(cnt == 0)
    {
        ERROR("Failed to disable partition %s, couldn't find it in fstab!\n", path);
        return -1;
    }
    return cnt;
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

struct fstab *fstab_auto_load(void)
{
    char path[64];
    path[0] = 0;

    if(access("/mrom.fstab", F_OK) >= 0)
        strcpy(path, "/mrom.fstab");
    else
    {
        DIR *d = opendir("/");
        if(!d)
        {
            ERROR("Failed to open /\n");
            return NULL;
        }

        struct dirent *dt;
        while((dt = readdir(d)))
        {
            if(dt->d_type != DT_REG)
                continue;

            // For some reason, CM includes goldfish's fstab, ignore it
            // (goldfish is the virtual device for emulator)
            if(strcmp(dt->d_name, "fstab.goldfish") == 0 || strcmp(dt->d_name, "fstab.ranchu") == 0)
                continue;

            if(strncmp(dt->d_name, "fstab.", sizeof("fstab.")-1) == 0)
            {
                strcpy(path, "/");
                strcat(path, dt->d_name);

                // try to find specifically fstab.device
#ifdef TARGET_DEVICE
                if(strcmp(dt->d_name, "fstab."TARGET_DEVICE) == 0)
                    break;
#endif
            }
        }
        closedir(d);
    }

    if(path[0] == 0)
    {
        ERROR("Failed to find fstab!\n");
        return NULL;
    }

    ERROR("Loading fstab \"%s\"...\n", path);
    return fstab_load(path, 1);
}

void fstab_add_part(struct fstab *f, const char *dev, const char *path, const char *type, const char *options, const char *options2)
{
    struct fstab_part *p = mzalloc(sizeof(struct fstab_part));
    p->path = strdup(path);
    p->device = strdup(dev);
    p->type = strdup(type);
    p->options_raw = strdup(options);
    fstab_parse_options(p->options_raw, p);
    p->options2 = strdup(options2);

    list_add(&f->parts, p);
    ++f->count;
}

struct fstab_part *fstab_clone_part(struct fstab_part *p)
{
    struct fstab_part *new_p = mzalloc(sizeof(struct fstab_part));
    memcpy(new_p, p, sizeof(struct fstab_part));

    new_p->path = strdup(p->path);
    new_p->device = strdup(p->device);
    new_p->type = strdup(p->type);
    new_p->options_raw = strdup(p->options_raw);
    new_p->options = strdup(p->options);
    new_p->options2 = strdup(p->options2);

    return new_p;
}

void fstab_add_part_struct(struct fstab *f, struct fstab_part *p)
{
    list_add(&f->parts, p);
    ++f->count;
}

void fstab_update_device(struct fstab *f, const char *oldDev, const char *newDev)
{
    int i;
    char *tmp = strdup(oldDev);

    for(i = 0; i < f->count; ++i)
    {
        if(strcmp(f->parts[i]->device, tmp) == 0)
        {
            f->parts[i]->device = realloc(f->parts[i]->device, strlen(newDev)+1);
            strcpy(f->parts[i]->device, newDev);
        }
    }

    free(tmp);
}
