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
#include <unistd.h>
#include <cutils/android_reboot.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "multirom.h"
#include "lib/framebuffer.h"
#include "lib/log.h"
#include "version.h"
#include "lib/util.h"
#include "lib/mrom_data.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define KEEP_REALDATA "/dev/.keep_realdata"
#define REALDATA "/realdata"


static void do_kexec(void)
{
    emergency_remount_ro();

    execl("/kexec", "/kexec", "-e", NULL);

    ERROR("kexec -e failed! (%d: %s)", errno, strerror(errno));
    while(1);
}

int main(int argc, const char *argv[])
{
    int i;
    const char *rom_to_boot = NULL;

    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d%s\napkL%d\n", VERSION_MULTIROM, VERSION_DEV_FIX, VERSION_APKL);
            fflush(stdout);
            return 0;
        }
        else if(strcmp(argv[i], "-apkL") == 0)
        {
            // Return all (internal and external) installed ROMs needed for the MultiROM Manager app

            // external partitions will be mounted to /mnt/mrom/ and kept mounted so the app
            // can manipulate them

            int i;
            struct multirom_status s;
            memset(&s, 0, sizeof(struct multirom_status));

            multirom_apk_get_roms(&s);

            for(i = 0; s.roms && s.roms[i]; ++i)
            {
                if (!s.roms[i]->partition)
                {
                    // Internal ROMs
                    printf("ROM: name=%s base=%s icon=%s\n",
                        s.roms[i]->name, s.roms[i]->base_path, s.roms[i]->icon_path);
                }
                else
                {
                    // External ROMs
                    printf("ROM: name=%s base=%s icon=%s part_name=%s part_mount=%s part_uuid=%s part_fs=%s\n",
                        s.roms[i]->name, s.roms[i]->base_path, s.roms[i]->icon_path,
                        s.roms[i]->partition->name, s.roms[i]->partition->mount_path, s.roms[i]->partition->uuid, s.roms[i]->partition->fs);
                }
            }
            fflush(stdout);

            multirom_free_status(&s);

            return 0;
        }
        else if(strncmp(argv[i], "--boot-rom=", sizeof("--boot-rom")) == 0)
        {
            rom_to_boot = argv[i] + sizeof("--boot-rom");
        }
    }

    srand(time(0));
    klog_init();

    // output all messages to dmesg,
    // but it is possible to filter out INFO messages
    klog_set_level(6);

    mrom_set_log_tag("multirom");

    ERROR("Running MultiROM v%d%s\n", VERSION_MULTIROM, VERSION_DEV_FIX);

    // root is mounted read only in android and MultiROM uses
    // it to store some temp files, so remount it.
    // Yes, there is better solution to this.
    if(rom_to_boot)
        mount(NULL, "/", NULL, MS_REMOUNT, NULL);

    int exit = multirom(rom_to_boot);

    if(rom_to_boot)
        mount(NULL, "/", NULL, MS_RDONLY | MS_REMOUNT, NULL);

    if(exit >= 0)
    {
        if(exit & EXIT_REBOOT_RECOVERY)
            do_reboot(REBOOT_RECOVERY);
        else if(exit & EXIT_REBOOT_BOOTLOADER)
            do_reboot(REBOOT_BOOTLOADER);
        else if(exit & EXIT_SHUTDOWN)
            do_reboot(REBOOT_SHUTDOWN);
        else if(exit & EXIT_REBOOT)
            do_reboot(REBOOT_SYSTEM);

        if(exit & EXIT_KEXEC)
        {
            do_kexec();
            return 0;
        }

        // indicates trampoline to keep /realdata mounted
        if(!(exit & EXIT_UMOUNT))
            close(open(KEEP_REALDATA, O_WRONLY | O_CREAT, 0000));
    }

    return 0;
}
