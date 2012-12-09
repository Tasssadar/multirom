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
#include "framebuffer.h"
#include "log.h"
#include "version.h"
#include "util.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define KEEP_REALDATA "/dev/.keep_realdata"
#define REALDATA "/realdata"

static void do_reboot(int exit)
{
    sync();
    umount(REALDATA);

    if(exit & EXIT_REBOOT_RECOVERY)         android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
    else if(exit & EXIT_REBOOT_BOOTLOADER)  android_reboot(ANDROID_RB_RESTART2, 0, "bootloader");
    else if(exit & EXIT_SHUTDOWN)           android_reboot(ANDROID_RB_POWEROFF, 0, 0);
    else                                    android_reboot(ANDROID_RB_RESTART, 0, 0);

    while(1);
}

static void do_kexec(void)
{
    sync();
    umount(REALDATA);

    execl("/kexec", "/kexec", "-e", NULL);

    ERROR("kexec -e failed! (%d: %s)", errno, strerror(errno));
    while(1);
}

int main(int argc, char *argv[])
{
    int i;
    for(i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-v") == 0)
        {
            printf("%d\n", VERSION_MULTIROM);
            return 0;
        }
    }

    srand(time(0));
    klog_init();

    int exit = multirom();

    if(exit >= 0)
    {
        if(exit & EXIT_REBOOT_MASK)
        {
            do_reboot(exit);
            return 0;
        }

        if(exit & EXIT_KEXEC)
        {
            do_kexec();
            return 0;
        }

        // indicates trampoline to keep /realdata mounted
        if(!(exit & EXIT_UMOUNT))
            close(open(KEEP_REALDATA, O_WRONLY | O_CREAT, 0000));
    }

    vt_set_mode(0);

    return 0;
}