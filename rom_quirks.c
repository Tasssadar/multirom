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
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <errno.h>

#include "rom_quirks.h"
#include "rq_inject_file_contexts.h"
#include "lib/log.h"
#include "lib/util.h"
#include "libbootimg.h"

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
                        if (strstr(line, "/data") || strstr(line, "/system") || strstr(line, "/cache") || strstr(line, "/mnt") ||
                                strstr(line, "/vendor")) {
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
    int failed_file_contexts_injections = 0;

    // walk over all _regular_ files in /
    char* path = "/system/etc/selinux/plat_file_contexts";
    if (!access(path, F_OK)) {
        char buf;
        int sourcefile, destfile, n;
        sourcefile = open(path, O_RDONLY);
        destfile = open("/plat_file_contexts", O_WRONLY | O_CREAT, 0644);
        while((n = read(sourcefile, &buf, 1)))
        {
            write(destfile, &buf, 1 );
        }
        close(sourcefile);
        close(destfile);
    }
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
            //
            // The possibility of several combinations of file_contexts, file_contexts.bin and
            // plat_file_contexts seems to exist, so use a fail counter instead.
            if( (strcmp(dt->d_name, "file_contexts") == 0)
                || (strcmp(dt->d_name, "file_contexts.bin") == 0)
                || (strcmp(dt->d_name, "plat_file_contexts") == 0)
              ) {

                snprintf(buff, sizeof(buff), "/%s", dt->d_name);

                if (inject_file_contexts(buff) != 0)
                    failed_file_contexts_injections++;
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

    if (!access(path, F_OK)) {
        if(!mount("/plat_file_contexts", path, "ext4", MS_BIND, "discard,nomblk_io_submit")) {
            INFO("file_contexts bind mounted in system\n");
        } else {
            ERROR("file_contexts bind mount failed! %s\n", strerror(errno));
        }
    }

    if (failed_file_contexts_injections)
        disable_restorecon_recursive();
}

char* convert_to_raw(char* str) {
    char* temp = malloc(strlen(str));
    char* temp2 = temp;
    char* out = calloc(1, strlen(str) + 2);
    int i = 0, j = 0;

    while (str[j] != '\n') {
        j++;
    }

    char* token;
    strcpy(temp, str);
    temp[j] = '\0';
    if (strstr(temp, ".")) {
        while (token = strsep(&temp, ".")) {
            strcpy(out + i, token);
            i += strlen(token);
        }
    } else if(strstr(temp, "-")) {
        while (token = strsep(&temp, "-")) {
            strcpy(out + i, token);
            i += strlen(token);
        }
    } else {
        strcat(temp, "00");
        strcpy(out, temp);
    }
    free(temp2);
    return out;
}

void rom_quirks_change_patch_and_osver(struct multirom_status *s, struct multirom_rom *to_boot) {

    char* path = "/system/build.prop";
    struct bootimg primary_img;
    char* patchstring = NULL, *stringtoappend = NULL;
    char* existing_ver = NULL;
    char* existing_level = NULL;
    int sourcefile, destfile, n;

    if (libbootimg_init_load(&primary_img, "/dev/block/bootdevice/by-name/boot", LIBBOOTIMG_LOAD_ALL) < 0)
    {
        ERROR("cant open /dev/block/bootdevice/by-name/boot\n");
        return;
    }

    char* primary_os_version = libbootimg_get_osversion(&primary_img.hdr, false);
    char* primary_os_level = libbootimg_get_oslevel(&primary_img.hdr, false);

    char* primary_os_ver_raw = libbootimg_get_osversion(&primary_img.hdr, true);
    char* primary_os_level_raw = libbootimg_get_oslevel(&primary_img.hdr, true);
    libbootimg_destroy(&primary_img);

    sourcefile = open(path, O_RDONLY, 0644);

    if (sourcefile == -1) {
        ERROR("open failed! %s\n", strerror(errno));
    }

    struct stat stats;
    int         status;
    int size;

    status = stat(path, &stats);
    if(status == 0) {
        size = stats.st_size;
    }

    asprintf(&patchstring, "%s=%s\n%s=%s", "ro.build.version.security_patch", primary_os_level, "ro.build.version.release", primary_os_version);
    char* filebuf = calloc(1, size + strlen(patchstring));


    n = read(sourcefile, filebuf, size);
    if (n == -1) {
        ERROR("read failed! %s\n", strerror(errno));
    }
    close(sourcefile);

    existing_level = strstr(filebuf, "ro.build.version.security_patch");
    existing_level += strlen("ro.build.version.security_patch=");

    existing_ver = strstr(filebuf, "ro.build.version.release");
    existing_ver += strlen("ro.build.version.release=");

    char* existing_level_raw = convert_to_raw(existing_level);
    char* existing_ver_raw = convert_to_raw(existing_ver);

    if (strtol(primary_os_ver_raw, NULL, 10) > strtol(existing_ver_raw, NULL, 10) || strtol(primary_os_level_raw, NULL, 10) > strtol(existing_level_raw, NULL, 10) || s->use_primary_kernel || !to_boot->has_bootimg) {

        existing_level = strstr(filebuf, "ro.build.version.security_patch");

        int i = 0;
        while (existing_level[i] != '\n') {
            i++;
        }
        existing_level += i;
        asprintf(&stringtoappend, "%s%s", patchstring, existing_level);

        existing_ver = strstr(filebuf, "ro.build.version.release");
        memcpy(existing_ver, stringtoappend, strlen(stringtoappend));

        destfile = open("/build.prop", O_RDWR | O_CREAT, 0644);
        write(destfile, filebuf, strlen(filebuf));
        INFO("build.prop %s\n", filebuf);
        close(destfile);


        if (!access(path, F_OK)) {
            if(!mount("/build.prop", path, "ext4", MS_BIND, "discard,nomblk_io_submit")) {
                INFO("build.prop bind mounted in system\n");
            } else {
                ERROR("build.prop bind mount failed! %s\n", strerror(errno));
            }
        }
    }
    free(existing_level_raw);
    free(existing_ver_raw);
    free(patchstring);
    if (stringtoappend) {
        free(stringtoappend);
    }
    free(filebuf);
}
