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
#include "log.h"
#include "../util.h"
#include "../version.h"
#include "adb.h"
#include "../fstab.h"

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

static int run_multirom_bin(char *path)
{
    ERROR("Running multirom");
    pid_t pID = fork();
    if(pID == 0)
    {
        char * cmd[] = { path, NULL };
        int res = execve(cmd[0], cmd, NULL);

        ERROR("exec failed %d %d %s\n", res, errno, strerror(errno));
        _exit(127);
    }
    else
    {
        int status = 0;
        while(waitpid(pID, &status, WNOHANG) == 0)
            usleep(300000);
        ERROR("MultiROM exited with status %d", status);
        return status;
    }
}

static void run_multirom(void)
{
    char path[256];
    struct stat info;

    // busybox
    sprintf(path, "%s/%s", path_multirom, BUSYBOX_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find busybox: %s", path);
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
        ERROR("Could not find multirom: %s", path);
        return;
    }
    chmod(path, EXEC_MASK);

    do {
        if(run_multirom_bin(path) == 0)
            break;
    } while(restart);
}

static void mount_and_run(struct fstab *fstab)
{
    struct fstab_part *p = fstab_find_by_path(fstab, "/data");
    if(!p)
    {
        ERROR("Failed to find /data partition in fstab\n");
        return;
    }

    if(wait_for_file(p->device, 5) < 0)
    {
        ERROR("Waiting too long for dev %s", p->device);
        return;
    }

    // Remove nosuid flag, because secondary ROMs have
    // su binaries on /data
    p->mountflags &= ~(MS_NOSUID);

    mkdir(REALDATA, 0755);
    if (mount(p->device, REALDATA, p->type, p->mountflags, p->options) < 0)
    {
        ERROR("Failed to mount /realdata %d\n", errno);
        return;
    }

    if(find_multirom() == -1)
    {
        ERROR("Could not find multirom folder!");
        return;
    }

    adb_init(path_multirom);
    run_multirom();
    adb_quit();
}

int main(int argc, char *argv[])
{
    int i, res;
    static char *const cmd[] = { "/main_init", NULL };
    struct fstab *fstab = NULL;;

    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d\n", VERSION_TRAMPOLINE);
            return 0;
        }
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

    ERROR("Running trampoline v%d\n", VERSION_TRAMPOLINE);

    ERROR("Initializing devices...");
    devices_init();
    ERROR("Done initializing");

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

    // mount and run multirom from sdcard
    mount_and_run(fstab);

exit:
    if(fstab)
        fstab_destroy(fstab);

    // close and destroy everything
    devices_close();

    if(access(KEEP_REALDATA, F_OK) < 0)
    {
        umount(REALDATA);
        umount("/dev/pts");
        umount("/dev");
        rmdir("/dev/pts");
        rmdir("/dev/socket");
        rmdir("/dev");
        rmdir(REALDATA);
    }

    umount("/proc");
    umount("/sys");
    rmdir("/proc");
    rmdir("/sys");

    chmod("/main_init", EXEC_MASK);

    // run the main init
    res = execve(cmd[0], cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}
