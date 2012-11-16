#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>

#include "devices.h"
#include "log.h"
#include "util.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define REALDATA "/realdata"
#define DATA_DEV "/dev/block/mmcblk0p9"
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
    if(find_multirom() == -1)
    {
        ERROR("Could not find multirom folder!");
        return;
    }

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

    // multirom
    sprintf(path, "%s/%s", path_multirom, MULTIROM_BIN);
    if (stat(path, &info) < 0)
    {
        ERROR("Could not find multirom: %s", path);
        return;
    }
    chmod(path, EXEC_MASK);

    ERROR("Running multirom");

    pid_t pID = fork();
    if(pID == 0)
    {
        char *cmd[] = { path, NULL };
        int res = execve(cmd[0], cmd, NULL);

        ERROR("exec failed %d %d %s\n", res, errno, strerror(errno));
        _exit(127);
    }
    int status = 0;
    while(waitpid(pID, &status, WNOHANG) == 0) { usleep(300000); }


    ERROR("MultiROM exited with status %d", status);
}

int main()
{
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

    ERROR("Initializing devices");
    devices_init();
    ERROR("Done initializing");

    chmod("/busybox", EXEC_MASK);
    chmod("/main_init", EXEC_MASK);

    int ok = 1;
    if(wait_for_file(DATA_DEV, 5) < 0)
    {
        ERROR("Waing too long for data block dev");
        ok = 0;
    }

    if(wait_for_file("/dev/graphics/fb0", 5) < 0)
    {
        ERROR("Waiting too long for fb0");
        ok = 0;
    }

    // mount and run multirom from sdcard
    if(ok)
    {
        mkdir(REALDATA, 0755);
        if (mount(DATA_DEV, REALDATA, "ext4", MS_RELATIME | MS_NOATIME,
            "user_xattr,acl,barrier=1,data=ordered") >= 0)
        {
            run_multirom();
        }
        else
            ERROR("Failed to mount /realdata %d\n", errno);
    }

    // close and destroy everything
    devices_close();

    struct stat info;
    if(stat(KEEP_REALDATA, &info) < 0)
    {
        umount(REALDATA);
        umount("/dev/pts");
        umount("/dev");
        rmdir("/dev/pts");
        rmdir("/dev/socket");
        rmdir("/dev");
    }

    umount("/proc");
    umount("/sys");
    rmdir("/proc");
    rmdir("/sys");

    // run the main init
    char *cmd[] = { "/main_init", (char *)0 };
    int res = execve("/main_init", cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}