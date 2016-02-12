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

#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/klog.h>
#include <linux/loop.h>
#include <unistd.h>

#include <fcntl.h>
#include "adb.h"
#include "../lib/util.h"
#include "../lib/log.h"

static pthread_t adb_thread;
static volatile int run_thread = 0;
static pid_t adb_pid = -1;

static char busybox_path[64] = { 0 };
static char adbd_path[64] = { 0 };

static char * const ENV[] = {
    "PATH=/sbin:/bin:/usr/bin:/usr/sbin:/mrom_bin",
    "LD_LIBRARY_PATH=.:/sbin",
    "ANDROID_ROOT=/system",
    "ANDROID_DATA=/data",
    "EXTERNAL_STORAGE=/sdcard",
    "ANDROID_PROPERTY_WORKSPACE=8,49152",
    "SHELL=/mrom_bin/sh",

    NULL
};

static void *adb_thread_work(void *mrom_path)
{
    int enabled = adb_is_enabled((char*)mrom_path);
    free(mrom_path);

    if(enabled == 0)
        return NULL;

    adb_init_usb();

    if(adb_init_busybox() < 0)
        return NULL;

    adb_init_fs();

    chmod(adbd_path, 0755);

    while(run_thread)
    {
        adb_pid = fork();
        if(adb_pid == 0) // child
        {
            umask(077);
            setsid();
            stdio_to_null();
            setpgid(0, getpid());

            static char * const cmd[] = { adbd_path, NULL };
            execve(cmd[0], cmd, ENV);
            exit(0);
        }
        else
        {
            int status = 0;
            waitpid(adb_pid, &status, 0);
        }
        usleep(300000);
    }

    adb_cleanup();

    return NULL;
}

void adb_init(char *mrom_path)
{
    if(run_thread)
        return;

    sprintf(busybox_path, "%s/busybox", mrom_path);
    sprintf(adbd_path, "%s/adbd", mrom_path);

    INFO("Starting adbd\n");
    run_thread = 1;
    pthread_create(&adb_thread, NULL, adb_thread_work, strdup(mrom_path));
}

void adb_quit(void)
{
    if(!run_thread)
        return;

    INFO("Stopping adbd\n");

    run_thread = 0;

    if(adb_pid != -1)
    {
        kill(adb_pid, 9);
        adb_pid = -1;
    }

    pthread_join(adb_thread, NULL);
}

void adb_init_usb(void)
{
    mkdir_with_perms("/dev/usb-ffs", 0770, "shell", "shell");
    mkdir_with_perms("/dev/usb-ffs/adb", 0770, "shell", "shell");
    mount("adb", "/dev/usb-ffs/adb", "functionfs", 0, "uid=2000,gid=2000");

    write_file("/sys/class/android_usb/android0/enable", "0");

    char serial[64] = { 0 };
    adb_get_serial(serial, sizeof(serial));

    // this vid and pid is used in TWRP, CWM and AOSP recovery
    // for all devices, so I guess it is universal
    write_file("/sys/class/android_usb/android0/idVendor", "18d1");
    write_file("/sys/class/android_usb/android0/idProduct", "d001");
    write_file("/sys/class/android_usb/android0/f_ffs/aliases", "adb");
    write_file("/sys/class/android_usb/android0/functions", "adb");
    write_file("/sys/class/android_usb/android0/iManufacturer", PRODUCT_MANUFACTURER);
    write_file("/sys/class/android_usb/android0/iProduct", PRODUCT_MODEL);
    write_file("/sys/class/android_usb/android0/iSerial", serial);

    write_file("/sys/class/android_usb/android0/enable", "1");
    write_file("/sys/devices/platform/android_usb/usb_function_switch", "3");
}

int adb_init_busybox(void)
{
    mkdir("/mrom_bin", 0777);

    copy_file(busybox_path, "/mrom_bin/busybox");
    chmod("/mrom_bin/busybox", 0755);

    static const char *install_cmd[] = {
        "/mrom_bin/busybox", "--install", "/mrom_bin/", NULL
    };

    if(run_cmd((char**)install_cmd) != 0)
    {
        ERROR("adb: failed to --install busybox\n");
        return -1;
    }

    mkdir("/dev/pts", 0666);
    if(mount("devpts", "/dev/pts", "devpts", 0, NULL) < 0)
    {
        ERROR("Failed to mount devpts: %d (%s)\n", errno, strerror(errno));
        return -1;
    }

    return 0;
}

void adb_init_fs(void)
{
    mkdir("/sdcard", 0777);
    if(strstr(adbd_path, "/realdata/media/0/multirom"))
        mount("/realdata/media/0/", "/sdcard/", "auto", MS_BIND, "");
    else
        mount("/realdata/media/", "/sdcard/", "auto", MS_BIND, "");
}

void adb_cleanup(void)
{
    if(umount("/sdcard") >= 0)
        remove_dir("/sdcard");

    remove_dir("/mrom_bin");

    umount("/dev/pts");
    rmdir("/dev/pts");

    umount("/dev/usb-ffs/adb");
    rmdir("/dev/usb-ffs/adb");
    rmdir("/dev/usb-ffs");
}

int adb_get_serial(char *serial, int maxlen)
{
    FILE *f = fopen("/proc/cmdline", "re");
    if(!f)
        return -1;

    int res = -1;

    char cmdline[1024];
    static const char *tag = "androidboot.serialno=";
    if(fgets(cmdline, sizeof(cmdline), f))
    {
        char *start = strstr(cmdline, tag);
        if(start)
        {
            start += strlen(tag);
            char *end = strchr(start, ' ');
            if(end && end-start < maxlen)
            {
                strncpy(serial, start, end-start);
                res = 0;
            }
        }
    }
    fclose(f);
    return res;
}

int adb_is_enabled(char *mrom_path)
{
    char cfg[64];
    char *cmd[] = { busybox_path, "grep", "^enable_adb=1$", cfg, NULL };
    sprintf(cfg, "%s/multirom.ini", mrom_path);

    return run_cmd(cmd) == 0 ? 1 : 0;
}
