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

#include "rom_quirks.h"
#include "rq_inject_file_contexts.h"
#include "lib/log.h"
#include "lib/util.h"

static void workaround_mount_in_sh(const char *path)
{
    char line[512];
    char *tmp_name = NULL;
    FILE *f_in, *f_out;

    f_in = fopen(path, "re");
    if(!f_in)
        return;

    const int size = strlen(path) + 5;
    tmp_name = malloc(size);
    snprintf(tmp_name, size, "%s-new", path);
    f_out = fopen(tmp_name, "we");
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

    struct stat info;
    stat(path, &info);
    chmod(tmp_name, info.st_mode);

    rename(tmp_name, path);
    free(tmp_name);
}

// Keep this as a backup function in case the file_contexts binary injection doesn't work
static void disable_restorecon_recursive(void)
{
    DIR *d = opendir("/");
    if(d)
    {
        struct dirent *dt;
        char path[128];
        while((dt = readdir(d)))
        {
            if(dt->d_type != DT_REG)
                continue;

            if(strendswith(dt->d_name, ".rc"))
            {
                snprintf(path, sizeof(path), "/%s", dt->d_name);
                char line[512];
                char *tmp_name = NULL;
                FILE *f_in, *f_out;

                f_in = fopen(path, "re");
                if(!f_in)
                    return;

                const int size = strlen(path) + 5;
                tmp_name = malloc(size);
                snprintf(tmp_name, size, "%s-new", path);
                f_out = fopen(tmp_name, "we");
                if(!f_out)
                {
                    fclose(f_in);
                    free(tmp_name);
                    return;
                }

                while(fgets(line, sizeof(line), f_in))
                {
                    if (strstr(line, "restorecon_recursive ") || (strstr(line, "restorecon ") && strstr(line, "--recursive"))) {
                        if (strstr(line, "/data") || strstr(line, "/system") || strstr(line, "/cache") || strstr(line, "/mnt")) {
                            fputc('#', f_out);
                        }
                    }
                    fputs(line, f_out);
                }

                fclose(f_in);
                fclose(f_out);
                rename(tmp_name, path);
                free(tmp_name);

                chmod(path, 0750);
            }
        }
        closedir(d);
    }
}

void rom_quirks_on_initrd_finalized(void)
{
    int file_contexts_injected = 0;

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

            // The Android L and later releases have SELinux
            // set to "enforcing" and "restorecon_recursive /data" line in init.rc.
            // Restorecon on /data goes into /data/media/0/multirom/roms/ and changes
            // context of all secondary ROMs files to that of /data, including the files
            // in secondary ROMs /system dirs. We need to prevent that.
            // Right now, we do that by adding entries into /file_contexts that say
            // MultiROM folders don't have any context
            //
            // Android N is using the binary format of file_contexts, try to inject it
            // with MultiROM exclusions, if that fails go back to the old method and remove
            // 'restorecon_recursive' from init.rc scripts
            //
            // Android 8.0 is using text format contexts again, but now has two separate files
            // 'nonplat_file_contexts' and 'plat_file_contexts', we need to patch the latter
            // https://source.android.com/security/selinux/images/SELinux_Treble.pdf
            if( (strcmp(dt->d_name, "file_contexts") == 0)
                || (strcmp(dt->d_name, "file_contexts.bin") == 0)
                || (strcmp(dt->d_name, "plat_file_contexts") == 0)
              ) {

                snprintf(buff, sizeof(buff), "/%s", dt->d_name);

                if(inject_file_contexts(buff) == 0)
                    file_contexts_injected = 1;
            }

            // franco.Kernel includes script init.fk.sh which remounts /system as read only
            // comment out lines with mount and /system in all .sh scripts in /
            if(strendswith(dt->d_name, ".sh"))
            {
                snprintf(buff, sizeof(buff), "/%s", dt->d_name);
                workaround_mount_in_sh(buff);
            }
        }
        closedir(d);
    }

    if (!file_contexts_injected)
        disable_restorecon_recursive();
}
