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

#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <cutils/android_reboot.h>

#include "devices.h"
#include "../lib/log.h"
#include "../lib/util.h"
#include "../lib/fstab.h"
#include "../lib/inject.h"
#include "../version.h"
#include "adb.h"
#include "../hooks.h"
#include "encryption.h"

#ifdef MR_POPULATE_BY_NAME_PATH
	#include "Populate_ByName_using_emmc.c"
#endif

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define REALDATA "/realdata"
#define MULTIROM_BIN "multirom"
#define BUSYBOX_BIN "busybox"
#define KEEP_REALDATA "/dev/.keep_realdata"

// Not defined in android includes?
#define MS_RELATIME (1<<21)

static char path_multirom[64] = { 0 };

static int find_multirom(void)
{
    int i;
    struct stat info;

    static const char *paths[] = {
        REALDATA"/media/0/multirom", // 4.2
        REALDATA"/media/multirom",
        NULL,
    };

    for(i = 0; paths[i]; ++i)
    {
        if(stat(paths[i], &info) < 0)
            continue;

        strcpy(path_multirom, paths[i]);
        return 0;
    }
    return -1;
}

static void run_multirom(void)
{
    char path[256];
    struct stat info;

    // busybox
    sprintf(path, "%s/%s", path_multirom, BUSYBOX_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find busybox: %s\n", path);
        return;
    }
    chmod(path, EXEC_MASK);

    // restart after crash
    sprintf(path, "%s/restart_after_crash", path_multirom);
    int restart = (stat(path, &info) >= 0);

    // multirom
    sprintf(path, "%s/%s", path_multirom, MULTIROM_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find multirom: %s\n", path);
        return;
    }
    chmod(path, EXEC_MASK);

    char *cmd[] = { path, NULL };
    do
    {
        ERROR("Running multirom\n");
        int res = run_cmd(cmd);
        if(res == 0)
            break;
        else
            ERROR("MultiROM exited with status code %d!\n", res);
    }
    while(restart);
}

static int try_mount_all_entries(struct fstab *fstab, struct fstab_part *first_data_p)
{
    size_t i;
    struct fstab_part *p_itr = first_data_p;

    do
    {
        // Remove nosuid flag, because secondary ROMs have
        // su binaries on /data
        p_itr->mountflags &= ~(MS_NOSUID);

        if(mount(p_itr->device, REALDATA, p_itr->type, p_itr->mountflags, p_itr->options) >= 0)
            return 0;
    }
    while((p_itr = fstab_find_next_by_path(fstab, "/data", p_itr)));

    ERROR("Failed to mount /realdata with data from fstab, trying all filesystems\n");

    const char *fs_types[] = { "ext4", "f2fs", "ext3", "ext2" };
    const char *fs_opts [] = {
        "barrier=1,data=ordered,nomblk_io_submit,noauto_da_alloc,errors=panic", // ext4
        "inline_xattr,flush_merge", // f2fs
        "", // ext3
        "" // ext2
    };

    for(i = 0; i < ARRAY_SIZE(fs_types); ++i)
    {
        if(mount(first_data_p->device, REALDATA, fs_types[i], first_data_p->mountflags, fs_opts[i]) >= 0)
        {
            INFO("/realdata successfuly mounted with fs %s\n", fs_types[i]);
            return 0;
        }
    }

    return -1;
}

static int mount_and_run(struct fstab *fstab)
{
    struct fstab_part *datap = fstab_find_first_by_path(fstab, "/data");
    if(!datap)
    {
        ERROR("Failed to find /data partition in fstab\n");
        return -1;
    }

    if(access(datap->device, R_OK) < 0)
    {
        INFO("Waiting for %s\n", datap->device);
        if(wait_for_file(datap->device, 5) < 0)
        {
            ERROR("Waiting too long for dev %s\n", datap->device);
            return -1;
        }
    }

    mkdir(REALDATA, 0755);

    if(try_mount_all_entries(fstab, datap) < 0)
    {
#ifndef MR_ENCRYPTION
        ERROR("Failed to mount /data with all possible filesystems!\n");
        return -1;
#else
        INFO("Failed to mount /data, trying encryption...\n");
        switch(encryption_before_mount(fstab))
        {
            case ENC_RES_ERR:
                ERROR("/data decryption failed!\n");
                return -1;
            case ENC_RES_BOOT_INTERNAL:
                return 0;
            case ENC_RES_BOOT_RECOVERY:
                sync();
                android_reboot(ANDROID_RB_RESTART2, 0, "recovery"); // REBOOT_RECOVERY
                while (1)
                    sleep(1);
                // we're never returning
                return 0;
            default:
            case ENC_RES_OK:
            {
                if(try_mount_all_entries(fstab, datap) < 0)
                {
                    ERROR("Failed to mount decrypted /data with all possible filesystems!\n");
                    return -1;
                }
                break;
            }
        }
#endif
    }

    if(find_multirom() == -1)
    {
        ERROR("Could not find multirom folder!\n");
        return -1;
    }

    adb_init(path_multirom);
    run_multirom();
    adb_quit();
    return 0;
}

static int is_charger_mode(void)
{
    char buff[2048] = { 0 };
    int charger_mode = 0;

    FILE *f = fopen("/proc/cmdline", "re");
    if(!f)
        return 0;

    while (fgets(buff, sizeof(buff), f) != NULL)
    {
        if (strstr(buff, "androidboot.mode=charger") != NULL)
        {
            charger_mode = 1;
            break;
        }
    }

    fclose(f);

    return charger_mode;
}

static void fixup_symlinks(void)
{
    static const char *init_links[] = { "/sbin/ueventd", "/sbin/watchdogd" };

    size_t i;
    ssize_t len;
    char buff[64];
    struct stat info;

    for(i = 0; i < ARRAY_SIZE(init_links); ++i)
    {
        if(lstat(init_links[i], &info) < 0 || !S_ISLNK(info.st_mode))
            continue;

        if (info.st_size < sizeof(buff)-1)
        {
            len = readlink(init_links[i], buff, sizeof(buff)-1);
            if(len >= 0)
            {
                buff[len] = 0;
                // if the symlink already points to ../init, skip it.
                if(strcmp(buff, "../init") == 0)
                    continue;
            }
        }

        ERROR("Fixing up symlink '%s' -> '%s' to '%s' -> '../init')\n", init_links[i], buff, init_links[i]);
        unlink(init_links[i]);
        symlink("../init", init_links[i]);
    }
}

int main(int argc, char *argv[])
{
    int i, res;
    static char *const cmd[] = { "/init", NULL };
    struct fstab *fstab = NULL;
    char *inject_path = NULL;
    char *mrom_dir = NULL;
    int force_inject = 0;

    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d\n", VERSION_TRAMPOLINE);
            fflush(stdout);
            return 0;
        }
        else if(strstartswith(argv[i], "--inject="))
            inject_path = argv[i] + strlen("--inject=");
        else if(strstartswith(argv[i], "--mrom_dir="))
            mrom_dir = argv[i] + strlen("--mrom_dir=");
        else if(strcmp(argv[i], "-f") == 0)
            force_inject = 1;
    }

    if(inject_path)
    {
        if(!mrom_dir)
        {
            printf("--mrom_dir=[path to multirom's data dir] needs to be specified!\n");
            fflush(stdout);
            return 1;
        }

        mrom_set_dir(mrom_dir);
        mrom_set_log_tag("trampoline_inject");
        return inject_bootimg(inject_path, force_inject);
    }

    umask(000);

    // Init only the little we need, leave the rest for real init
    mkdir("/dev", 0755);
    mkdir("/dev/pts", 0755);
    mkdir("/dev/socket", 0755);
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);

    mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=0755");
    mount("devpts", "/dev/pts", "devpts", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("pstore", "/sys/fs/pstore", "pstore", 0, NULL);

#if MR_USE_DEBUGFS_MOUNT
    // Mount the debugfs kernel sysfs
    mkdir("/sys/kernel/debug", 0755);
    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL);
#endif

    klog_init();
    // output all messages to dmesg,
    // but it is possible to filter out INFO messages
    klog_set_level(6);

    mrom_set_log_tag("trampoline");
    INFO("Running trampoline v%d\n", VERSION_TRAMPOLINE);

    if(is_charger_mode())
    {
        INFO("Charger mode detected, skipping multirom\n");
        goto run_main_init;
    }

#if MR_DEVICE_HOOKS >= 3
    tramp_hook_before_device_init();
#endif

    INFO("Initializing devices...\n");
    devices_init();
    INFO("Done initializing\n");

    if(wait_for_file("/dev/graphics/fb0", 5) < 0)
    {
        ERROR("Waiting too long for fb0");
        goto exit;
    }

#ifdef MR_POPULATE_BY_NAME_PATH
    //nkk71 M7 hack
    Populate_ByName_using_emmc();
#endif

    fstab = fstab_auto_load();
    if(!fstab)
        goto exit;

#if 0
    fstab_dump(fstab); //debug
#endif

    // mount and run multirom from sdcard
    if(mount_and_run(fstab) < 0 && mrom_is_second_boot())
    {
        ERROR("This is second boot and we couldn't mount /data, reboot!\n");
        sync();
        //android_reboot(ANDROID_RB_RESTART, 0, 0);
        android_reboot(ANDROID_RB_RESTART2, 0, "recovery"); // favour reboot to recovery, to avoid possible bootlooping
        while(1)
            sleep(1);
    }

exit:
    if(fstab)
        fstab_destroy(fstab);

    // close and destroy everything
    devices_close();

run_main_init:
    umount("/dev/pts");
    rmdir("/dev/pts");
    rmdir("/dev/socket");

    if(access(KEEP_REALDATA, F_OK) < 0)
    {
        umount(REALDATA);
        umount("/dev");
        rmdir(REALDATA);
        encryption_destroy();
    }

    encryption_cleanup();

#if MR_USE_DEBUGFS_MOUNT
    umount("/sys/kernel/debug");
#endif

    umount("/proc");
    umount("/sys/fs/pstore");
    umount("/sys");

    INFO("Running main_init\n");

    fixup_symlinks();

    chmod("/main_init", EXEC_MASK);
    rename("/main_init", "/init");

    res = execve(cmd[0], cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}
