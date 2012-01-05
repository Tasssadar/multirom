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
#include "keywords.h"

#define SD_EXT_BLOCK "/dev/block/mmcblk0p99"
#define SD_FAT_BLOCK "/dev/block/mmcblk0p98"

static const char* BRIGHT_FILE = "/sys/devices/platform/i2c-gpio.2/i2c-2/2-0060/leds/lcd-backlight/brightness";

int8_t bootmgr_selected = 0;
volatile uint8_t bootmgr_input_run = 1;
volatile uint8_t bootmgr_time_run = 1;
volatile uint8_t bootmgr_run = 1;
uint8_t bootmgr_phase = BOOTMGR_MAIN;
uint8_t total_backups = 0;
char *backups[BOOTMGR_BACKUPS_MAX];
uint8_t backups_loaded = 0;
uint8_t backups_has_active = 0;
int8_t selected;

bootmgr_settings_t settings;
uint8_t ums_enabled = 0;
pthread_t t_time;

void bootmgr_start()
{
    bootmgr_selected = 0;
    bootmgr_input_run = 1;
    bootmgr_run = 1;
    bootmgr_time_run = 1;
    bootmgr_phase = BOOTMGR_MAIN;
    total_backups = 0;
    backups_loaded = 0;
    backups_has_active = 0;

    bootmgr_load_settings();
    bootmgr_init_display();

    bootmgr_set_brightness(settings.brightness);

    int key = 0;
    int8_t last_selected = -1;
    int8_t last_phase = -1;
    uint8_t key_pressed = (settings.timeout_seconds == -1);
    int16_t timer = settings.timeout_seconds*10;
    uint16_t x, y;
    uint8_t touch;
    selected = -1;
    pthread_t t_input;
    pthread_create(&t_input, NULL, bootmgr_input_thread, NULL);
    bootmgr_set_time_thread(1);

    while(bootmgr_run)
    {
        if(last_selected != bootmgr_selected)
        {
            bootmgr_draw();
            last_selected = bootmgr_selected;
        }

        if(last_phase != bootmgr_phase)
        {
            bootmgr_setup_touch();
            bootmgr_draw();
            last_phase = bootmgr_phase;
        }

        key = bootmgr_get_last_key();
        touch = bootmgr_get_last_touch(&x, &y);
        if(key != -1 || touch)
        {
            if(!key_pressed)
            {
                bootmgr_erase_text(25);
                bootmgr_draw();
                key_pressed = 1;
            }

            if(bootmgr_handle_key(key))
                break;

            if(touch)
            {
                key = bootmgr_check_touch(x, y);
                if(key & TCALL_EXIT_MGR)
                    break;
            }
        }

        usleep(100000);
        if(!key_pressed)
        {
            if(timer%10 == 0)
            {
                bootmgr_printf(-1, 25, WHITE, "Boot from internal mem in %us", timer/10);
                bootmgr_draw();
            }

            if(--timer <= 0)
            {
                bootmgr_boot_internal();
                break;
            }
        }
    }
    bootmgr_exit();
}

void bootmgr_exit()
{
    bootmgr_set_time_thread(0);

    bootmgr_destroy_display();
    bootmgr_input_run = 0;
}

void bootmgr_set_time_thread(uint8_t start)
{
    if(start)
    {
        bootmgr_time_run = 1;
        pthread_create(&t_time, NULL, bootmgr_time_thread, NULL);
    }
    else
    {
        bootmgr_time_run = 0;
        pthread_join(t_time, NULL);
    }
}

void *bootmgr_time_thread(void *cookie)
{
    time_t tm;

    char pct[5];
    char status[50];
    int8_t hours;
    int8_t mins;

    const uint16_t update_val = settings.show_seconds ? 10 : 600;
    uint16_t timer = update_val;

    while(bootmgr_time_run)
    {
        if(timer == update_val)
        {
            time(&tm);
            bootmgr_get_file(battery_pct, &pct, 4);
            char *n = strchr(&pct, '\n');
            *n = NULL;
            bootmgr_get_file(battery_status, &status, 50);

            // Timezone lame handling
            hours = (tm%86400/60/60) + settings.timezone;
            mins = tm%3600/60 + settings.timezone_mins;

            if     (mins >= 60) { mins -= 60; ++hours; }
            else if(mins < 0)   { mins = 60 - mins; --hours; }

            if     (hours >= 24) hours -= 24;
            else if(hours < 0)   hours = 24 + hours;

            if(settings.show_seconds)
                bootmgr_printf(0, 0, WHITE, "%2u:%02u:%02u    Battery: %s%%, %s", hours, mins, tm%60, &pct, &status);
            else
                bootmgr_printf(0, 0, WHITE, "%2u:%02u         Battery: %s%%, %s", hours, mins, &pct, &status);

            bootmgr_draw();
            timer = 0;
        }
        usleep(100000);
        ++timer;
    }
    return NULL;
}

uint8_t bootmgr_handle_key(int key)
{
    switch(bootmgr_phase)
    {
        case BOOTMGR_MAIN:
        {
            switch(key)
            {
                case KEY_VOLUMEDOWN:
                {
                   if(++bootmgr_selected == 4)
                       bootmgr_selected = 0;
                   break;
                }
                case KEY_VOLUMEUP:
                {
                   if(--bootmgr_selected == -1)
                       bootmgr_selected = 3;
                   break;
                }
                case KEY_BACK:
                    bootmgr_printf(-1, 25, WHITE, "Rebooting...");
                    bootmgr_draw();
                case KEY_POWER:
                    bootmgr_close_framebuffer();
                    bootmgr_input_run = 0;
                    reboot(key == KEY_POWER ? RB_POWER_OFF : RB_AUTOBOOT);
                    return 1;
                case KEY_MENU:
                {
                    switch(bootmgr_selected)
                    {
                        case 0: bootmgr_boot_internal(); return 1;
                        case 1:
                            if(bootmgr_show_rom_list())
                                return 1;
                            break;
                        case 2: bootmgr_touch_ums();    break;
                        case 3: bootmgr_touch_tetris(); break;
                    }
                    break;
                }
                default:break;
            }
            break;
        }
        case BOOTMGR_SD_SEL:
        {
            switch(key)
            {
                case KEY_VOLUMEDOWN:
                    bootmgr_touch_sd_down();
                    break;
                case KEY_VOLUMEUP:
                    bootmgr_touch_sd_up();
                    break;
                case KEY_MENU:
                    return bootmgr_boot_sd();
                case KEY_BACK:
                    bootmgr_touch_sd_exit();
                    break;
                default:break;
            }
            break;
        }
        case BOOTMGR_TETRIS:
        {
            tetris_key(key);
            break;
        }
        case BOOTMGR_UMS:
        {
            if(key != KEY_SEARCH)
                break;
            bootmgr_touch_exit_ums();
            break;
        }
    }
    return 0;
}

void bootmgr_select(int8_t line)
{
    bootmgr_line *ln = NULL;
    if(selected != -1 && (ln = _bootmgr_get_line(selected)))
       ln->color = WHITE;
    ln = _bootmgr_get_line(line);
    if(ln)
        ln->color = BLACK;

    if(line == -1)
        bootmgr_erase_fill(BOOTMGR_FILL_SELECT);
    else
        bootmgr_print_fill(0, line*ISO_CHAR_HEIGHT, BOOTMGR_DIS_W, ISO_CHAR_HEIGHT, WHITE, BOOTMGR_FILL_SELECT);
    selected = line;
}

uint8_t bootmgr_show_rom_list()
{
    bootmgr_set_time_thread(0);
    bootmgr_phase = BOOTMGR_SD_SEL;
    bootmgr_display->bg_img = 0;
    bootmgr_printf(-1, 20, WHITE, "Mounting sd-ext...");
    bootmgr_draw();

    if(!backups_loaded)
    {
        // mknod
        mknod(SD_EXT_BLOCK, (0666 | S_IFBLK), makedev(179, 2));

        //mkdir
        mkdir("/sdroot", (mode_t)0775);
        uid_t uid = decode_uid("system");
        gid_t gid = decode_uid("system");
        chown("/sdroot", uid, gid);

        //mount
        static const char *mount_args[] = { NULL, "ext4", SD_EXT_BLOCK, "/sdroot" };
        int res = do_mount(4, mount_args);
        if(res < 0)
        {
            bootmgr_printf(-1, 20, WHITE, "Failed to mount sd-ext!");
            bootmgr_printf(-1, 21, WHITE, "Press back to return.");
            return 0;
        }

        DIR *dir = opendir("/sdroot/multirom/backup");
        if(dir)
        {
            struct dirent * de = NULL;
            while ((de = readdir(dir)) != NULL)
            {
                if (de->d_name[0] == '.')
                    continue;
                backups[total_backups] = (char*)malloc(128);
                strcpy(backups[total_backups++], de->d_name);

                if(total_backups >= BOOTMGR_BACKUPS_MAX-1)
                    break;
            }
            closedir(dir);
            backups[total_backups] = NULL;
        }
        dir = opendir("/sdroot/multirom/rom");
        if(dir)
            backups_has_active = 1;
    }

    backups_loaded = 1;

    bootmgr_printf(0, 0, (0x3F << 11), "Select ROM to boot. Press back to return");
    if(backups_has_active)
    {
        bootmgr_printf(0, 2, WHITE, "Current active ROM");
        bootmgr_select(2);
    }
    bootmgr_printf(0, 4, (0x3F << 11), "Backup folder:");

    uint16_t i = 0;
    for(; i <= 25 && i < total_backups; ++i)
        bootmgr_printf(0, i + 5, WHITE, "%s", backups[i]);

    if(total_backups)
    {
        if(!backups_has_active)
        {
            bootmgr_printf(-1, 2, WHITE, "No active ROM");
            bootmgr_select(5);
        }
        bootmgr_erase_text(20);
    }
    // Useless to print this, because it will be deleted immediately
    //else if(backups_has_active)
    //    bootmgr_printf(-1, 19, WHITE, "No backups present.");
    else
    {
        bootmgr_printf(-1, 20, WHITE, "No active ROM nor backups present.");
        bootmgr_printf(-1, 21, WHITE, "Press \"back\" to return");
    }

    while(bootmgr_get_last_key() != -1); // clear key queue
    while(bootmgr_get_last_touch(&i, &i));     // clear touch queue
    if(!total_backups && backups_has_active)
        return bootmgr_boot_sd();
    bootmgr_draw();
    return 0;
}

uint8_t bootmgr_boot_sd()
{
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);

    char *path = (char*)malloc(200);
    if(selected == 2)
    {
        bootmgr_printf(-1, 20, WHITE, "Booting from SD-card...");
        sprintf(path, "/sdroot/multirom/rom");
    }
    else
    {
        sprintf(path, "/sdroot/multirom/backup/%s", backups[selected-5]);
        bootmgr_printf(-1, 20, WHITE, "Booting \"%s\"...", backups[selected-5]);
    }

    selected = -1;
    bootmgr_draw();

    char *p = (char*)malloc(200);
    char *s = (char*)malloc(50);
    sprintf(p, "%s/boot", path);
    bootmgr_import_boot(p);

    char * mount_args[] = { NULL, "ext4", p, s, "bind" };

    // /system
    sprintf(p, "%s/system", path);
    strcpy(s, "/system");
    if(do_mount(5, mount_args) < 0)
    {
        bootmgr_printf(-1, 20, WHITE, "Mount %s failed", mount_args[2]);
        bootmgr_draw();
        return 0;
    }

    // /data
    sprintf(p, "%s/data", path);
    strcpy(s, "/data");
    do_mount(5, mount_args);

    // /cache
    sprintf(p, "%s/cache", path);
    strcpy(s, "/cache");
    do_mount(5, mount_args);

    free(p);
    free(s);
    free(path);

    return 1;
}

void bootmgr_import_boot(char *path)
{
    DIR *d = opendir(path);
    if(d == NULL)
        return;
    struct dirent *dp;
    char to[100];
    char from[100];

    // copy init binary
    INFO("Copy init binary to ramdisk");
    sprintf(from, "%s/init", path);
    __copy(from, "/main_init");
    chmod("/main_init", 0750);

    // /default.prop
    sprintf(from, "%s/default.prop", path);
    __copy(from, "/default.prop");

    // /sbin/adbd
    sprintf(from, "%s/adbd", path);
    __copy(from, "/sbin/adbd");

    while(dp = readdir(d))
    {
        if(strstr(dp->d_name, ".rc") == NULL)
            continue;

        // copy to our ramdisk
        INFO("Copy %s to ramdisk", dp->d_name);
        sprintf(from, "%s/%s", path, dp->d_name);
        sprintf(to, "/%s", dp->d_name);
        __copy(from, to);
        chmod(to, 0750);
    }
    closedir(d);
}

void bootmgr_load_settings()
{
    settings.timezone = 0;
    settings.timeout_seconds = 3;
    settings.show_seconds = 0;
    settings.touch_ui = 1;
    settings.tetris_max_score = 0;
    settings.brightness = 100;

    if(!bootmgr_toggle_sdcard(1, 0))
    {
        FILE *f = fopen("/sdrt/multirom.txt", "r");
        if(f)
        {
            fseek (f, 0, SEEK_END);
            int size = ftell(f);
            char *con = (char*)malloc(size+1);
            rewind(f);
            if(fread(con, 1, size, f))
            {
                con[size] = 0;
                char *p = strtok (con, "=\n");
                char *n = p;

                for(; p != NULL; n = p)
                {
                    if(!(p = strtok (NULL, "=\n")))
                        break;

                    if(strstr(n, "timeout"))
                        settings.timeout_seconds = atoi(p);
                    else if(strstr(n, "timezone"))
                    {
                        double timezone = atof(p);
                        settings.timezone = (int8_t)timezone;
                        settings.timezone_mins = (timezone - settings.timezone)*60;
                    }
                    else if(strstr(n, "show_seconds"))
                        settings.show_seconds = atoi(p);
                    else if(strstr(n, "touch_ui"))
                        settings.touch_ui = atoi(p);
                    else if(strstr(n, "tetris_max_score"))
                        settings.tetris_max_score = atoi(p);
                    else if(strstr(n, "brightness"))
                        settings.brightness = atoi(p);

                    p = strtok (NULL, "=\n");
                }
            }
            free(con);
            fclose(f);
        }
    }
    bootmgr_toggle_sdcard(0, 0);
}

void bootmgr_save_settings()
{
    if(!bootmgr_toggle_sdcard(1, 0))
    {
        FILE *f = fopen("/sdrt/multirom.txt", "w");
        if(f)
        {
            char *line = (char*)malloc(30);
            sprintf(line, "timeout = %u\r\n", settings.timeout_seconds);
            fputs(line, f);
            float timezone = settings.timezone + settings.timezone_mins/60.f;
            sprintf(line, "timezone = %.2f\r\n", timezone);
            fputs(line, f);
            sprintf(line, "show_seconds = %u\r\n", (uint8_t)settings.show_seconds);
            fputs(line, f);
            sprintf(line, "touch_ui = %u\r\n", (uint8_t)settings.touch_ui);
            fputs(line, f);
            sprintf(line, "tetris_max_score = %u\r\n", settings.tetris_max_score);
            fputs(line, f);
            sprintf(line, "brightness = %u\r\n", settings.brightness);
            fputs(line, f);
            fclose(f);
            free(line);
        }
    }
    bootmgr_toggle_sdcard(0, 0);
}

int8_t bootmgr_get_file(char *name, char *buffer, uint8_t len)
{
    FILE *f = fopen(name, "r");
    if(!f)
        return 0;

    int res = fread(buffer, 1, len, f);
    fclose(f);
    if(res > 0)
        buffer[res] = 0;
    return res;
}

uint8_t bootmgr_toggle_ums()
{
    bootmgr_printf(-1, 21, WHITE, "%sabling USB mass storage...", ums_enabled ? "dis" : "en");
    bootmgr_draw();

    sync();

    FILE *f = fopen("/sys/devices/platform/msm_hsusb/gadget/lun0/file", "w+");
    if(!f)
    {
        bootmgr_erase_text(21);
        return 0;
    }

    if(!ums_enabled)
    {
        bootmgr_toggle_sdcard(1, 1);
        fputs(SD_FAT_BLOCK, f);
        bootmgr_printf(-1, 20, WHITE, "USB mass storage enabled");
        bootmgr_printf(-1, 21, WHITE, "Press \"search\" again to exit");
    }
    else
    {
        fputc(0, f);
        bootmgr_erase_text(20);
        bootmgr_erase_text(21);
        bootmgr_toggle_sdcard(0, 1);
    }
    fclose(f);

    bootmgr_display->bg_img = ums_enabled;
    bootmgr_draw();

    ums_enabled = !ums_enabled;
    return 1;
}

int bootmgr_toggle_sdcard(uint8_t on, uint8_t mknod_only)
{
    if(on)
    {
        int res = mknod(SD_FAT_BLOCK, (0666 | S_IFBLK), makedev(179, 1));
        if(mknod_only)
            return res;

        mkdir("/sdrt", (mode_t)0775);

        uint8_t i = 0;
        for(; i < 20; ++i)
        {
            res = mount(SD_FAT_BLOCK, "/sdrt", "vfat", 0, NULL);
            if(!res)
                break;
            usleep(500000);
        }
        return res;
    }
    else
    {
        if(!mknod_only)
        {
            umount("/sdrt");
            rmdir("/sdrt");
        }
        unlink(SD_FAT_BLOCK);
    }
    return 0;
}

void bootmgr_clear()
{
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_set_imgs_count(0);
    bootmgr_set_touches_count(0);
}

void bootmgr_set_brightness(uint8_t pct)
{
    FILE *f = fopen(BRIGHT_FILE, "w");
    if(!f)
        return;
    unsigned short value = 30 + (225*pct)/100;
    if(value > 255)
        value = 255;
    fprintf(f, "%u", value);
    fclose(f);
}

void bootmgr_boot_internal()
{
    bootmgr_printf(-1, 25, WHITE, "Booting from internal memory...");
    bootmgr_draw();
}
