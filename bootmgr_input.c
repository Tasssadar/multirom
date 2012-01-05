#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <pthread.h>
#include <dirent.h>

#include "bootmgr.h"
#include "bootmgr_shared.h"

#define MAX_DEVICES 16

static struct pollfd ev_fds[MAX_DEVICES];
static unsigned ev_count = 0;

int bootmgr_key_queue[10];
uint16_t bootmgr_touch_queue[64][2];
int8_t bootmgr_key_itr = 10;
int8_t bootmgr_touch_itr = 64;
pthread_mutex_t *bootmgr_input_mutex;

void *bootmgr_input_thread(void *cookie)
{
    ev_init();
    struct input_event ev;
    uint16_t x, y;
    pthread_mutex_init(bootmgr_input_mutex, NULL);
    struct timeval tv;
    struct timeval tv_last;
    gettimeofday(&tv_last, NULL);

    while(bootmgr_input_run)
    {
        ev_get(&ev, 0);
        if(ev.type == EV_KEY && !ev.value && ev.code <= KEY_MAX)
        {
            pthread_mutex_lock(bootmgr_input_mutex);
            if(bootmgr_key_itr > 0)
                bootmgr_key_queue[--bootmgr_key_itr] = ev.code;
            pthread_mutex_unlock(bootmgr_input_mutex);
        }
        else if(ev.type == EV_ABS && ev.code == 0x30 && ev.value) //#define ABS_MT_TOUCH_MAJOR  0x30    /* Major axis of touching ellipse */
        {
            gettimeofday(&tv, NULL);
            int32_t ms = (tv.tv_sec - tv_last.tv_sec)*1000+(tv.tv_usec-tv_last.tv_usec)/1000;
            if(ms >= 200)
            {
                do { ev_get(&ev, 0); } while(ev.type != EV_ABS);
                x = ev.value;
                do { ev_get(&ev, 0); } while(ev.type != EV_ABS);
                y = ev.value;
                do { ev_get(&ev, 0); } while(ev.type != EV_SYN && ev.code != SYN_REPORT); // Wait for touch seq end

                pthread_mutex_lock(bootmgr_input_mutex);
                if(bootmgr_touch_itr > 0)
                {
                    bootmgr_touch_queue[--bootmgr_touch_itr][0] = x;
                    bootmgr_touch_queue[  bootmgr_touch_itr][1] = y;
                }
                pthread_mutex_unlock(bootmgr_input_mutex);
                tv_last.tv_sec = tv.tv_sec;
                tv_last.tv_usec = tv.tv_usec;
            }
            else
                do { ev_get(&ev, 0); } while(ev.type != EV_SYN && ev.code != SYN_REPORT); // Wait for touch seq end
        }
    }
    ev_exit();
    pthread_mutex_destroy(bootmgr_input_mutex);
    return NULL;
}

int bootmgr_get_last_key()
{
    int res = -1;
    pthread_mutex_lock(bootmgr_input_mutex);
    if(bootmgr_key_itr != 10)
        res = bootmgr_key_queue[bootmgr_key_itr++];
    pthread_mutex_unlock(bootmgr_input_mutex);
    return res;
}

uint8_t bootmgr_get_last_touch(uint16_t *x, uint16_t *y)
{
    uint8_t res = 0;
    pthread_mutex_lock(bootmgr_input_mutex);
    if(bootmgr_touch_itr != 64)
    {
        *x = bootmgr_touch_queue[bootmgr_touch_itr  ][0];
        *y = bootmgr_touch_queue[bootmgr_touch_itr++][1];
        res = 1;
    }
    pthread_mutex_unlock(bootmgr_input_mutex);
    return res;
}

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
        closedir(dir);
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

void bootmgr_setup_touch()
{
    if(!settings.touch_ui)
        return;

    bootmgr_set_touches_count(0);
    switch(bootmgr_phase)
    {
        case BOOTMGR_MAIN:
            bootmgr_add_touch(40,  125, 160, 245, bootmgr_touch_int,    1);
            bootmgr_add_touch(160, 125, 280, 245, bootmgr_touch_sd,     2);
            bootmgr_add_touch(40,  245, 160, 365, bootmgr_touch_ums,    3);
            bootmgr_add_touch(160, 245, 280, 365, bootmgr_touch_tetris, 4);
            break;
        case BOOTMGR_UMS:
            bootmgr_add_touch(80, 370, 240, 410, bootmgr_touch_exit_ums, 1);
            bootmgr_print_fill(80, 370, 160, 40, WHITE, 24);
            bootmgr_printf(-1, 24, BLACK, "Exit");
            break;
        case BOOTMGR_SD_SEL:
        {
            bootmgr_add_touch(0,   430, 78,  480, bootmgr_touch_sd_up,     1);
            bootmgr_add_touch(80,  430, 158, 480, bootmgr_touch_sd_down,   2);
            bootmgr_add_touch(160, 430, 238, 480, bootmgr_touch_sd_select, 3);
            bootmgr_add_touch(240, 430, 320, 480, bootmgr_touch_sd_exit,   4);
            bootmgr_printf(31, 28, BLACK, "Up       Down     Select     Exit");
            bootmgr_print_fill(0,   430, 78, 49, WHITE, 28);
            bootmgr_print_fill(80,  430, 78, 49, WHITE, 29);
            bootmgr_print_fill(160, 430, 78, 49, WHITE, 30);
            bootmgr_print_fill(240, 430, 78, 49, WHITE, 31);
            break;
        }
    }
}

// Touch callbacks
int bootmgr_touch_int()
{
    bootmgr_selected = 0;
    bootmgr_boot_internal();
    return TCALL_EXIT_MGR;
}

int bootmgr_touch_sd()
{
    bootmgr_selected = 1;
    if(bootmgr_show_rom_list())
        return TCALL_EXIT_MGR;
    return TCALL_DELETE;
}

int bootmgr_touch_ums()
{
    bootmgr_selected = 2;
    if(bootmgr_toggle_ums())
        bootmgr_phase = BOOTMGR_UMS;
    return TCALL_DELETE;
}

int bootmgr_touch_tetris()
{
    bootmgr_selected = 3;
    bootmgr_set_time_thread(0);
    bootmgr_phase = BOOTMGR_TETRIS;
    tetris_init();
    return TCALL_DELETE;
}

int bootmgr_touch_exit_ums()
{
    bootmgr_toggle_ums();
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_erase_text(24);
    bootmgr_erase_fill(24);
    return TCALL_DELETE;
}

int bootmgr_touch_sd_up()
{
    if(!total_backups)
        return TCALL_NONE;
    if(selected == 2 || (!backups_has_active && selected == 5))
        bootmgr_select(total_backups+4);
    else if(selected == 5)
        bootmgr_select(2);
    else
        bootmgr_select(selected-1);
    bootmgr_draw();
    return TCALL_NONE;
}

int bootmgr_touch_sd_down()
{
    if(!total_backups)
        return TCALL_NONE;
    if(selected == 2 || (!backups_has_active && selected == total_backups+4))
        bootmgr_select(5);
    else if(selected == total_backups+4)
            bootmgr_select(2);
    else
        bootmgr_select(selected+1);
    bootmgr_draw();
    return TCALL_NONE;
}

int bootmgr_touch_sd_select()
{
    if(bootmgr_boot_sd())
        return TCALL_EXIT_MGR;
    return TCALL_NONE;
}

int bootmgr_touch_sd_exit()
{
    selected = -1;
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_display->bg_img = 1;
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_draw();
    bootmgr_set_time_thread(1);
    return TCALL_NONE;
}
