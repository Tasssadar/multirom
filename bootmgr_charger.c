#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <cutils/memory.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/reboot.h>
#include <stdarg.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <pthread.h>

#include "init.h"
#include "bootmgr.h"
#include "bootmgr_shared.h"

static const int battery_offset = 70;
static const int battery_fill_offset_x = 60;

pthread_t t_charger;
volatile char run_charger_thread = 1;

void bootmgr_charger_init()
{
    bootmgr_clear();
    bootmgr_set_time_thread(1);
    bootmgr_phase = BOOTMGR_CHARGER;
    bootmgr_display->bg_img = 0;

    bootmgr_print_img(0, battery_offset, "battery.rle", 1);
    bootmgr_print_img(0, battery_offset-20, "charger.rle", 2);
    bootmgr_update_battery_status();

    run_charger_thread = 1;
    pthread_create(&t_charger, NULL, bootmgr_charger_thread, NULL);
}

void bootmgr_charger_destroy()
{
    run_charger_thread = 0;
    bootmgr_clear();

    bootmgr_display->bg_img = 1;
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_draw();

    force_update_time = 1;
    pthread_join(t_charger, NULL);
}

uint8_t bootmgr_charger_key(int key)
{
    switch(key)
    {
        case KEY_END:
        {
            bootmgr_do_sleep(!sleep_mode);
            break;
        }
        case KEY_BACK:
        {
            bootmgr_charger_destroy();
            break;
        }
        case KEY_POWER:
        {
            reboot(RB_POWER_OFF);
            return 1;
        }
    }
    return 0;
}

void bootmgr_update_battery_status()
{
    static char status[50];
    bootmgr_get_file(battery_status, status, 50);

    int percent = bootmgr_get_battery_pct();

    bootmgr_printf(-1, 14, WHITE, "%u%%, %s", percent, status);
    bootmgr_update_battery_fill(percent);

    bootmgr_img *img = _bootmgr_get_img(2);
    img->show = (strstr(status, "Charging") == status);

    bootmgr_draw();
}

void bootmgr_update_battery_fill(int pct)
{
    int y = battery_offset + 35;
    int w = (pct * 200)/100;

    bootmgr_print_fill(battery_fill_offset_x, y, w, 80, WHITE, 1);
}

int bootmgr_get_battery_pct()
{
    static char pct[5];
    bootmgr_get_file(battery_pct, pct, 4);
    return atoi(pct);
}

void *bootmgr_charger_thread(void *cookie)
{
    uint8_t timer = 6;
    uint8_t blink = 0;

    while(run_charger_thread)
    {
        if(timer == 0)
        {
            if(!sleep_mode)
                bootmgr_update_battery_status();
            else if(!blink && bootmgr_get_battery_pct() == 100)
            {
                bootmgr_do_sleep(0);
                bootmgr_update_battery_status();
                blink = 1;
            }
            timer = 6;
        }else --timer;

        if(blink != 0)
        {
            bootmgr_fill *f = _bootmgr_get_fill(1);
            bootmgr_img *img = _bootmgr_get_img(1);
            if(!f || !img)
                break;

            if(blink == 1)
            {
                blink = 2;

                f->color = BLACK;
                img->show = 0;
            }
            else
            {
                blink = 1;
                f->color = WHITE;
                img->show = 1;
            }
            bootmgr_draw();
        }

        usleep(500000);
    }
    return NULL;
}
