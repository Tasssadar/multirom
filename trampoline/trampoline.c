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

#include "devices.h"
#include "../lib/log.h"
#include "../lib/util.h"
#include "../lib/fstab.h"
#include "../lib/inject.h"
#include "../version.h"
#include "adb.h"
#include "../hooks.h"
#include "encryption.h"

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

static void mount_and_run(struct fstab *fstab)
{
    struct fstab_part *p = fstab_find_first_by_path(fstab, "/data");
    if(!p)
    {
        ERROR("Failed to find /data partition in fstab\n");
        return;
    }

    if(wait_for_file(p->device, 5) < 0)
    {
        ERROR("Waiting too long for dev %s\n", p->device);
        return;
    }

    mkdir(REALDATA, 0755);

    int enc_res = encryption_before_mount(fstab);
    if(enc_res == ENC_RES_ERR)
        ERROR("Decryption failed, trying to mount anyway - might be unencrypted device.");
    else if(enc_res == ENC_RES_BOOT_INTERNAL)
        return;

    int mount_err = -1;
    struct fstab_part *p_itr = p;
    do
    {
        // Remove nosuid flag, because secondary ROMs have
        // su binaries on /data
        p_itr->mountflags &= ~(MS_NOSUID);

        if(mount(p_itr->device, REALDATA, p_itr->type, p_itr->mountflags, p_itr->options) >= 0)
            mount_err = 0;
        else
            mount_err = -errno;
    }
    while(mount_err < 0 && (p_itr = fstab_find_next_by_path(fstab, "/data", p_itr)));

    if(mount_err < 0)
    {
        ERROR("Failed to mount /realdata, err %d, trying all filesystems\n", mount_err);

        fstab_dump(fstab);

        const char *fs_types[] = { "ext4", "f2fs", "ext3", "ext2" };
        const char *fs_opts [] = {
            "barrier=1,data=ordered,nomblk_io_submit,noauto_da_alloc,errors=panic", // ext4
            "inline_xattr,flush_merge,errors=recover", // f2fs
            "", // ext3
            "" // ext2
        };

        int mounted = 0;
        size_t i;
        for(i = 0; i < ARRAY_SIZE(fs_types); ++i)
        {
            ERROR("Trying to mount %s with fs %s\n", p->device, fs_types[i]);
            if(mount(p->device, REALDATA, fs_types[i], p->mountflags, fs_opts[i]) >= 0)
            {
                ERROR("/realdata successfuly mounted with fs %s\n", fs_types[i]);
                mounted = 1;
                break;
            }
        }

        if(!mounted)
        {
            ERROR("Failed to mount /realdata with all possible filesystems!\n");
            return;
        }
    }

    if(find_multirom() == -1)
    {
        ERROR("Could not find multirom folder!\n");
        return;
    }

    adb_init(path_multirom);
    run_multirom();
    adb_quit();

    return;
fail:
    // REMOVE. DEBUGGING
    while(1)
    {
        sleep(1);
    }
}

static int is_charger_mode(void)
{
    char buff[2048] = { 0 };

    FILE *f = fopen("/proc/cmdline", "r");
    if(!f)
        return 0;

    fgets(buff, sizeof(buff), f);
    fclose(f);

    return (strstr(buff, "androidboot.mode=charger") != NULL);
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

    fstab = fstab_auto_load();
    if(!fstab)
        goto exit;

#if 0
    fstab_dump(fstab); //debug
#endif

    // REMOVE, DEBUGGING!
    //adb_init("/adb_sbin/");

    // mount and run multirom from sdcard
    mount_and_run(fstab);

exit:
    if(fstab)
        fstab_destroy(fstab);

    // close and destroy everything
    devices_close();

run_main_init:
    if(access(KEEP_REALDATA, F_OK) < 0)
    {
        umount(REALDATA);
        umount("/dev/pts");
        umount("/dev");
        rmdir("/dev/pts");
        rmdir("/dev/socket");
        rmdir("/dev");
        rmdir(REALDATA);
        encryption_destroy();
    }

    umount("/proc");
    umount("/sys");
    rmdir("/proc");
    rmdir("/sys");

    INFO("Running main_init\n");

    fixup_symlinks();

    chmod("/main_init", EXEC_MASK);
    rename("/main_init", "/init");

    res = execve(cmd[0], cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}
