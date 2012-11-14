#include <stdlib.h>
#include <unistd.h>
#include <cutils/android_reboot.h>
#include <fcntl.h>

#include "multirom.h"
#include "framebuffer.h"
#include "log.h"

#define EXEC_MASK (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define KEEP_REALDATA "/dev/.keep_realdata"

int main()
{
    klog_init();

    int exit = multirom();

    if(exit >= 0)
    {
        if(exit & EXIT_REBOOT)
        {
            sync();
            usleep(300000);
            android_reboot(ANDROID_RB_RESTART, 0, 0);
            while(1);
        }

        // indicates trampoline to keep /realdata mounted
        if(!(exit & EXIT_UMOUNT))
            close(open(KEEP_REALDATA, O_WRONLY | O_CREAT, 0000));
    }

    vt_set_mode(0);

    return 0;
}