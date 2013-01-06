#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

#include "devices.h"
#include "log.h"
#include "util.h"
#include "../version.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define REALDATA "/realdata"
#define BOOT_DEV "/dev/block/mmcblk0p2"
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

struct part_info
{
    uint64_t size;
    char *name;
};

#define PARTS_SIZE 24

int find_data_dev(char *data_dev)
{
    FILE *f = fopen("/proc/partitions", "r");
    if(!f)
        return -1;

    int res = -1;

    struct part_info parts[PARTS_SIZE];
    memset(parts, 0, sizeof(parts));
    int i = 0;
    int y, part_ok;

    char *p;
    char line[1024];
    // skip the first line
    fgets(line, sizeof(line), f);
    while(fgets(line, sizeof(line), f))
    {
        p = strtok(line, " \t\n");
        part_ok = 0;
        for(y = 0; p != NULL; ++y)
        {
            switch(y)
            {
                case 2:
                    parts[i].size = atol(p);
                    break;
                case 3:
                {
                    free(parts[i].name);
                    parts[i].name = malloc(strlen(p)+1);
                    strcpy(parts[i].name, p);
                    part_ok = 1;
                    break;
                }
            }
            p = strtok(NULL, " \t\n");
        }

        if(part_ok)
        {
            if(++i >= PARTS_SIZE)
                return -1;
        }
    }
    fclose(f);

    uint64_t max = 0;
    int idx = -1;
    for(y = 0; y < i; ++y)
    {
        if(strstr(parts[y].name, "mmcblk0p") != parts[y].name)
            continue;
        ERROR("got part %s, size %d", parts[y].name, (int)parts[y].size);
        if(parts[y].size > max)
        {
            idx = y;
            max = parts[y].size;
        }
    }

    if(idx == -1)
        goto exit;

    sprintf(data_dev, "/dev/block/%s", parts[idx].name);
    res = 0;
    ERROR("booting %s, size %d", parts[idx].name, (int)parts[idx].size);

exit:
    for(y = 0; y < i; ++y)
        free(parts[y].name);
    return res;
}

int main(int argc, char *argv[])
{
    int i;
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

    int ok = 1;
    if(wait_for_file(BOOT_DEV, 5) < 0)
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
        char data_dev[128];
        mkdir(REALDATA, 0755);
        if (find_data_dev(data_dev) == 0 &&
            mount(data_dev, REALDATA, "ext4", MS_RELATIME | MS_NOATIME,
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

    chmod("/main_init", EXEC_MASK);

    // run the main init
    char *cmd[] = { "/main_init", (char *)0 };
    int res = execve("/main_init", cmd, NULL);
    ERROR("execve returned %d %d %s\n", res, errno, strerror(errno));
    return 0;
}