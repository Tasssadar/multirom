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
#include "tetris.h"

#define MISC_COUNT 5
static const char *names[] = {
    "Battery charging",
    "Tetris",
    "Reboot",
    "Reboot to recovery",
    "Reboot to bootloader",
    NULL
};

uint8_t current_selection = 0;

void bootmgr_misc_init()
{
    bootmgr_clear();
    bootmgr_set_time_thread(0);
    bootmgr_phase = BOOTMGR_MISC;
    bootmgr_display->bg_img = 0;

    bootmgr_misc_draw_items();
}

void bootmgr_misc_destroy()
{
    bootmgr_clear();

    bootmgr_display->bg_img = 1;
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_draw();

    bootmgr_set_time_thread(1);
}

void bootmgr_misc_draw_items()
{
    int y = current_selection*ISO_CHAR_HEIGHT*3;
    int itr = 0;

    for(; names[itr] != NULL; ++itr)
        bootmgr_printf(10, 1 + itr*3, current_selection == itr ? BLACK : WHITE, names[itr]);

    bootmgr_print_fill(0, y, BOOTMGR_DIS_W, 3*ISO_CHAR_HEIGHT, WHITE, 1);

    bootmgr_draw();
}

uint8_t bootmgr_misc_key(int key)
{
    switch(key)
    {
        case KEY_BACK:
            bootmgr_misc_destroy();
            break;
        case KEY_MENU:
            misc_callbacks[current_selection]();
            break;
        case KEY_VOLUMEDOWN:
        {
            if(names[++current_selection] == NULL)
                current_selection = 0;
            bootmgr_misc_draw_items();
            break;
        }
        case KEY_VOLUMEUP:
        {
            if(!current_selection)
                current_selection = MISC_COUNT-1;
            else
                --current_selection;
            bootmgr_misc_draw_items();
            break;
        }
    }
    return 0;
}

int bootmgr_misc_charger()
{
    bootmgr_charger_init();
    return TCALL_DELETE;
}

int bootmgr_misc_tetris()
{
    tetris_init();
    return TCALL_DELETE;
}

int bootmgr_misc_reboot()
{
    bootmgr_printf(-1, 25, WHITE, "Rebooting...");
    bootmgr_draw();
    reboot(RB_AUTOBOOT);
    return TCALL_DELETE;
}

int bootmgr_misc_recovery()
{
    bootmgr_printf(-1, 25, WHITE, "Rebooting to recovery...");
    bootmgr_draw();
    __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "recovery");
    return TCALL_DELETE;
}

int bootmgr_misc_bootloader()
{
    bootmgr_printf(-1, 25, WHITE, "Rebooting to bootloader...");
    bootmgr_draw();
    __reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, "bootloader");
    return TCALL_DELETE;
}