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
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/xattr.h>

#include "multirom.h"
#include "rom_quirks.h"
#include "log.h"
#include "util.h"

#define MULTIROM_DIR_ANDROID "/data/media/0/multirom"
#define MULTIROM_DIR_ANDROID_LEN 22
#define RESTORECON_LAST "security.restorecon_last"
#define RESTORECON_LAST_HACK_PATH "/data/media"
#define RESTORECON_LAST_HACK_PATH_LEN 11

static void write_changed_restorecons(const char *path, FILE *rc)
{
    struct dirent *dt;
    char *next_path = 0;
    size_t new_len, allocd = 0;
    const int path_len = strlen(path);
    DIR *d = opendir(path);
    if(!d)
        return;

    while((dt = readdir(d)))
    {
        if(strcmp(dt->d_name, ".") == 0 || strcmp(dt->d_name, "..") == 0)
            continue;

        if(dt->d_type == DT_DIR)
        {
            new_len = imax((size_t)(path_len + strlen(dt->d_name) + 2), allocd);
            if(new_len > allocd)
            {
                next_path = realloc(next_path, new_len);
                allocd = new_len;
            }

            snprintf(next_path, allocd, "%s/%s", path, dt->d_name);
            const int next_path_len = strlen(next_path);

            if(strncmp(next_path, MULTIROM_DIR_ANDROID, next_path_len) == 0)
            {
                if(next_path_len != MULTIROM_DIR_ANDROID_LEN)
                {
                    fprintf(rc, "    restorecon \"%s/%s\"\n", path, dt->d_name);
                    write_changed_restorecons(next_path, rc);
                }
            }
            else
            {
                fprintf(rc, "    restorecon_recursive \"%s/%s\"\n", path, dt->d_name);

                // restorecon_recursive works only if RESTORECON_LAST xattr contains hash
                // different from current file_contexts. Because /data/media/ is shared
                // among multiple ROMs, this doesn't work well because some ROMs don't
                // set this xattr, so restorecon thinks nothing changed but it did.
                if (strncmp(next_path, RESTORECON_LAST_HACK_PATH, RESTORECON_LAST_HACK_PATH_LEN) == 0 &&
                    (next_path[RESTORECON_LAST_HACK_PATH_LEN] == 0 || next_path[RESTORECON_LAST_HACK_PATH_LEN] == '/'))
                {
                    removexattr(next_path, RESTORECON_LAST);
                }
            }
        }
        else
            fprintf(rc, "    restorecon \"%s/%s\"\n", path, dt->d_name);
    }

    closedir(d);
    free(next_path);
}

static void workaround_rc_restorecon(const char *rc_file_name)
{
    FILE *f_in, *f_out;
    char *name_out = NULL;
    char line[512];
    char *r;
    int changed = 0;

    f_in = fopen(rc_file_name, "r");
    if(!f_in)
    {
        ERROR("Failed to open input file %s\n", rc_file_name);
        return;
    }

    name_out = malloc(strlen(rc_file_name)+5);
    snprintf(name_out, strlen(rc_file_name)+5, "%s.new", rc_file_name);

    f_out = fopen(name_out, "w");
    if(!f_out)
    {
        ERROR("Failed to open output file %s\n", name_out);
        fclose(f_in);
        free(name_out);
        return;
    }

    while(fgets(line, sizeof(line), f_in))
    {
        r = strstr(line, "restorecon_recursive");
        if(r)
        {
            r += strlen("restorecon_recursive");

            while(*r && isspace(*r))
                ++r;

            if(strcmp(r, "/data\n") == 0)
            {
                changed = 1;
                fputc('#', f_out);
                fputs(line, f_out);
                write_changed_restorecons("/data", f_out);
            }
            else
                fputs(line, f_out);
        }
        else
            fputs(line, f_out);
    }

    fclose(f_out);
    fclose(f_in);

    if(changed)
    {
        rename(name_out, rc_file_name);
        chmod(rc_file_name, 0750);
    }
    else
        unlink(name_out);
    free(name_out);
}

static void workaround_mount_in_sh(const char *path)
{
    char line[512];
    char *tmp_name = NULL;
    FILE *f_in, *f_out;

    f_in = fopen(path, "r");
    if(!f_in)
        return;

    const int size = strlen(path) + 5;
    tmp_name = malloc(size);
    snprintf(tmp_name, size, "%s-new", path);
    f_out = fopen(tmp_name, "w");
    if(!f_out)
    {
        fclose(f_in);
        free(tmp_name);
        return;
    }

    while(fgets(line, sizeof(line), f_in))
    {
        if(strstr(line, "mount ") && strstr(line, "/system"))
            fputc('#', f_out);
        fputs(line, f_out);
    }

    fclose(f_in);
    fclose(f_out);
    rename(tmp_name, path);
    free(tmp_name);
}

void rom_quirks_on_android_mounted_fs(struct multirom_rom *rom)
{
    // CyanogenMod has init script 50selinuxrelabel which calls
    // restorecon on /data. On secondary ROMs, /system is placed
    // inside /data/media/ and mount-binded to /system, so restorecon
    // sets contexts to files in /system as if they were in /data.
    // This behaviour is there mainly because of old recoveries which
    // didn't set contexts properly, so it should be safe to remove
    // that file entirely.
    if(rom->type != ROM_ANDROID_USB_IMG && access("/system/etc/init.d/50selinuxrelabel", F_OK) >= 0)
    {
        INFO("Removing /system/etc/init.d/50selinuxrelabel.\n");
        remove("/system/etc/init.d/50selinuxrelabel");
    }

    // walk over all _regular_ files in /
    DIR *d = opendir("/");
    if(d)
    {
        struct dirent *dt;
        char buff[128];
        while((dt = readdir(d)))
        {
            if(dt->d_type != DT_REG)
                continue;

            // The Android L preview (and presumably later releases) have SELinux
            // set to "enforcing" and "restorecon_recursive /data" line in init.rc.
            // Restorecon on /data goes into /data/media/0/multirom/roms/ and changes
            // context of all secondary ROMs files to that of /data, including the files
            // in secondary ROMs /system dirs. We need to prevent that.
            if(strstr(dt->d_name, ".rc"))
            {
                snprintf(buff, sizeof(buff), "/%s", dt->d_name);
                workaround_rc_restorecon(buff);
            }

            // franco.Kernel includes script init.fk.sh which remounts /system as read only
            // comment out lines with mount and /system in all .sh scripts in /
            if(strstr(dt->d_name, ".sh") && (M(rom->type) & MASK_ANDROID) && rom->type != ROM_ANDROID_USB_IMG)
            {
                snprintf(buff, sizeof(buff), "/%s", dt->d_name);
                workaround_mount_in_sh(buff);
            }
        }
        closedir(d);
    }
}
