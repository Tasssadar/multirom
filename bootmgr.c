#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cutils/memory.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/reboot.h>

#include <linux/input.h>
#include <linux/kd.h>
#include <pthread.h>

#include "init.h"
#include "bootmgr.h"

char bootmgr_selected = 0;
volatile char bootmgr_input_run = 1;
int bootmgr_key_queue[10];
char bootmgr_key_itr = 10;
static pthread_mutex_t bootmgr_input_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *bootmgr_input_thread(void *cookie)
{
    ev_init();
    struct input_event ev;
    while(bootmgr_input_run)
    {
        do
        {
            ev_get(&ev, 0);
        } while (bootmgr_input_run && (ev.type != EV_KEY || ev.value != 1 || ev.code > KEY_MAX));
        pthread_mutex_lock(&bootmgr_input_mutex);
        if(ev.type == EV_KEY && bootmgr_key_itr > 0)
            bootmgr_key_queue[--bootmgr_key_itr] = ev.code;
        pthread_mutex_unlock(&bootmgr_input_mutex);
    }
    ev_exit();
    return NULL;
}

void bootmgr_start(unsigned short timeout_seconds)
{
    int key = 0;
    char last_selected = -1;
    char key_pressed = (timeout_seconds == 0);
    unsigned short timer = timeout_seconds*10;

    pthread_t t;
    pthread_create(&t, NULL, bootmgr_input_thread, NULL);

    while(1)
    {
        if(last_selected != bootmgr_selected)
        {
            if(bootmgr_selected) load_565rle_image("/init_1.rle");
            else                 load_565rle_image("/init_0.rle");
            last_selected = bootmgr_selected;
        }

        key = bootmgr_get_last_key();
        if(key != -1)
        {
            key_pressed = 1;
            switch(key)
            {
                case KEY_VOLUMEDOWN:
                case KEY_VOLUMEUP:
                    bootmgr_selected = !bootmgr_selected;
                    break;
                case KEY_POWER:
                case KEY_BACK:
                    bootmgr_input_run = 0;
                    reboot(key == KEY_POWER ? RB_POWER_OFF : RB_AUTOBOOT);
                    return;
                case KEY_MENU:
                    bootmgr_input_run = 0;
                    return;
                default:break;
            }
        }

        usleep(100000);
        if(!key_pressed)
        {
            if(--timer <= 0)
            {
                bootmgr_input_run = 0;
                return;
            }
        }
    }
}

int bootmgr_get_last_key()
{
    pthread_mutex_lock(&bootmgr_input_mutex);
    int res = -1;
    if(bootmgr_key_itr != 10)
        res = bootmgr_key_queue[bootmgr_key_itr++];
    pthread_mutex_unlock(&bootmgr_input_mutex);
    return res;
}

#define MAX_DEVICES 16

static struct pollfd ev_fds[MAX_DEVICES];
static unsigned ev_count = 0;

int ev_init(void)
{
    DIR *dir;
    struct dirent *de;
    int fd;

    dir = opendir("/dev/input");
    if(dir != 0) {
        while((de = readdir(dir))) {
//            fprintf(stderr,"/dev/input/%s\n", de->d_name);
            if(strncmp(de->d_name,"event",5)) continue;
            fd = openat(dirfd(dir), de->d_name, O_RDONLY);
            if(fd < 0) continue;

            ev_fds[ev_count].fd = fd;
            ev_fds[ev_count].events = POLLIN;
            ev_count++;
            if(ev_count == MAX_DEVICES) break;
        }
    }

    return 0;
}

void ev_exit(void)
{
    while (ev_count > 0) {
        close(ev_fds[--ev_count].fd);
    }
}

int ev_get(struct input_event *ev, unsigned dont_wait)
{
    int r;
    unsigned n;

    do {
        r = poll(ev_fds, ev_count, dont_wait ? 0 : -1);

        if(r > 0) {
            for(n = 0; n < ev_count; n++) {
                if(ev_fds[n].revents & POLLIN) {
                    r = read(ev_fds[n].fd, ev, sizeof(*ev));
                    if(r == sizeof(*ev)) return 0;
                }
            }
        }
    } while(dont_wait == 0);

    return -1;
}