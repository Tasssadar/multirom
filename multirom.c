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

#include <sys/stat.h> 
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/klog.h>
#include <linux/loop.h>
#include <ctype.h>

// clone libbootimg to /system/extras/ from
// https://github.com/Tasssadar/libbootimg.git
#include <libbootimg.h>

#include "multirom.h"
#include "multirom_ui.h"
#include "framebuffer.h"
#include "input.h"
#include "log.h"
#include "util.h"
#include "version.h"
#include "hooks.h"

#define REALDATA "/realdata"
#define BUSYBOX_BIN "busybox"
#define KEXEC_BIN "kexec"
#define NTFS_BIN "ntfs-3g"
#define EXFAT_BIN "exfat-fuse"
#define INTERNAL_ROM_NAME "Internal"
#define MAX_ROM_NAME_LEN 26
#define LAYOUT_VERSION "/data/.layout_version"
#define SECOND_BOOT_KMESG "MultiromSaysNextBootShouldBeSecondMagic108"

#define BATTERY_CAP "/sys/class/power_supply/battery/capacity"

#define T_FOLDER 4

static char busybox_path[64] = { 0 };
static char multirom_dir[64] = { 0 };
static char kexec_path[64] = { 0 };
static char ntfs_path[64] = { 0 };
static char exfat_path[64] = { 0 };

static volatile int run_usb_refresh = 0;
static pthread_t usb_refresh_thread;
static pthread_mutex_t parts_mutex = PTHREAD_MUTEX_INITIALIZER;
static void (*usb_refresh_handler)(void) = NULL;

int multirom_find_base_dir(void)
{
    int i;
    struct stat info;

    static const char *paths[] = {
        REALDATA"/media/0/multirom", // 4.2
        REALDATA"/media/multirom",
        "/data/media/0/multirom",
        "/data/media/multirom",
        NULL,
    };

    for(i = 0; paths[i]; ++i)
    {
        if(stat(paths[i], &info) < 0)
            continue;

        strcpy(multirom_dir, paths[i]);
        sprintf(busybox_path, "%s/%s", paths[i], BUSYBOX_BIN);
        sprintf(kexec_path, "%s/%s", paths[i], KEXEC_BIN);
        sprintf(ntfs_path, "%s/%s", paths[i], NTFS_BIN);
        sprintf(exfat_path, "%s/%s", paths[i], EXFAT_BIN);

        chmod(kexec_path, 0755);
        chmod(ntfs_path, 0755);
        chmod(exfat_path, 0755);
        return 0;
    }
    return -1;
}

int multirom(const char *rom_to_boot)
{
    if(multirom_find_base_dir() == -1)
    {
        ERROR("Could not find multirom dir");
        return -1;
    }

    struct multirom_status s;
    memset(&s, 0, sizeof(struct multirom_status));

    multirom_load_status(&s);
    multirom_dump_status(&s);

    struct multirom_rom *to_boot = NULL;
    int exit = (EXIT_REBOOT | EXIT_UMOUNT);

    if(rom_to_boot != NULL)
    {
        struct multirom_rom *rom = multirom_get_rom(&s, rom_to_boot, NULL);
        if(rom)
        {
            // Two possible scenarios: this ROM has kexec-hardboot and target
            // ROM has boot image, so kexec it immediatelly or
            // reboot and then proceed as usuall
            if(((M(rom->type) & MASK_KEXEC) || rom->has_bootimg) && rom->type != ROM_DEFAULT && multirom_has_kexec() == 0)
            {
                to_boot = rom;
                s.is_second_boot = 0;
                INFO("Booting ROM %s...\n", rom_to_boot);
            }
            else
            {
                s.current_rom = rom;
                s.auto_boot_type |= AUTOBOOT_FORCE_CURRENT;
                INFO("Setting ROM %s to force autoboot\n", rom_to_boot);
            }
        }
        else
        {
            ERROR("ROM %s was not found, force autoboot was not set!\n", rom_to_boot);
            exit = EXIT_UMOUNT;
        }
    }
    else if(s.is_second_boot != 0 || (s.auto_boot_type & AUTOBOOT_FORCE_CURRENT))
    {
        ERROR("Skipping ROM selection, is_second_boot=%d, auto_boot_type=0x%x\n", s.is_second_boot, s.auto_boot_type);
        to_boot = s.current_rom;
    }
    else
    {
        // just to cache the result so that it does not take
        // any time when the UI is up
        multirom_has_kexec();

        switch(multirom_ui(&s, &to_boot))
        {
            case UI_EXIT_BOOT_ROM: break;
            case UI_EXIT_REBOOT:
                exit = (EXIT_REBOOT | EXIT_UMOUNT);
                break;
            case UI_EXIT_REBOOT_RECOVERY:
                exit = (EXIT_REBOOT_RECOVERY | EXIT_UMOUNT);
                break;
            case UI_EXIT_REBOOT_BOOTLOADER:
                exit = (EXIT_REBOOT_BOOTLOADER | EXIT_UMOUNT);
                break;
            case UI_EXIT_SHUTDOWN:
                exit = (EXIT_SHUTDOWN | EXIT_UMOUNT);
                break;
        }
    }

    if(to_boot)
    {
        s.auto_boot_type &= ~(AUTOBOOT_FORCE_CURRENT);

        if(rom_to_boot == NULL)
            multirom_run_scripts("run-on-boot", to_boot);

        exit = multirom_prepare_for_boot(&s, to_boot);

        // Something went wrong, exit/reboot
        if(exit == -1)
        {
            if(rom_to_boot == NULL)
            {
                multirom_emergency_reboot();
                exit = EXIT_REBOOT;
            }
            else
                exit = EXIT_UMOUNT;
            goto finish;
        }

        s.current_rom = to_boot;

        free(s.curr_rom_part);
        s.curr_rom_part = NULL;

        if(to_boot->partition)
            s.curr_rom_part = strdup(to_boot->partition->uuid);

        if(s.is_second_boot == 0 && (M(to_boot->type) & MASK_ANDROID) && (exit & EXIT_KEXEC))
        {
            s.is_second_boot = 1;

            // mrom_kexecd=1 param might be lost if kernel does not have kexec patches
            ERROR(SECOND_BOOT_KMESG);
        }
        else
            s.is_second_boot = 0;
    }

finish:
    multirom_save_status(&s);
    multirom_free_status(&s);

    sync();

    return exit;
}

int multirom_init_fb(int rotation)
{
    vt_set_mode(1);

    if(fb_open(rotation) < 0)
    {
        ERROR("Failed to open framebuffer!");
        return -1;
    }

    fb_fill(BLACK);
    return 0;
}

void multirom_emergency_reboot(void)
{
    if(multirom_init_fb(0) < 0)
    {
        ERROR("Failed to init framebuffer in emergency reboot");
        return;
    }

    char *klog = multirom_get_klog();

    fb_add_text(0, 120, WHITE, 2,
                "An error occured.\nShutting down MultiROM to avoid data corruption.\n"
                "Report this error to the developer!\nDebug info: /sdcard/multirom/error.txt\n\n"
                "Press POWER button to reboot.");

    fb_add_text(0, 370, GRAYISH, 1, "Last lines from klog:");
    fb_add_rect(0, 390, fb_width, 1, GRAYISH);

    char *tail = klog+strlen(klog);
    int count = 0;
    while(tail > klog && count < 50)
    {
        --tail;
        if(*tail == '\n')
            ++count;
    }

    fb_add_text_long(0, 395, GRAYISH, 1, ++tail);

    fb_draw();

    multirom_copy_log(klog);
    free(klog);

    // Wait for power key
    start_input_thread();
    while(wait_for_key() != KEY_POWER);
    stop_input_thread();

    fb_clear();
    fb_close();
}

static int find_idx(int c)
{
    static const char *capital = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char *normal  = "abcdefghijklmnopqrstuvwxyz";

    char *p;
    if((p = strchr(capital, c)))
        return p - capital;
    else if((p = strchr(normal, c)))
        return p - normal;

    return -128 + c;
}

static int compare_rom_names(const void *a, const void *b)
{
    struct multirom_rom *rom_a = *((struct multirom_rom **)a);
    struct multirom_rom *rom_b = *((struct multirom_rom **)b);

    if(rom_a->type == ROM_DEFAULT)
        return -1;
    else if(rom_b->type == ROM_DEFAULT)
        return 1;

    char *itr_a = rom_a->name;
    char *itr_b = rom_b->name;

    while(1)
    {
        if(*itr_a == 0)
            return -1;
        else if(*itr_b == 0)
            return 1;

        if(*itr_a == *itr_b)
        {
            ++itr_a;
            ++itr_b;
            continue;
        }

        int idx_a = find_idx(*itr_a);
        int idx_b = find_idx(*itr_b);

        if(idx_a == idx_b)
        {
            ++itr_a;
            ++itr_b;
            continue;
        }

        return idx_a < idx_b ? -1 : 1;
    }
    return 0;
}

int multirom_default_status(struct multirom_status *s)
{
    s->is_second_boot = 0;
    s->current_rom = NULL;
    s->roms = NULL;
    s->colors = 0;
    s->brightness = 40;
    s->enable_adb = 0;
    s->rotation = MULTIROM_DEFAULT_ROTATION;

    s->fstab = fstab_auto_load();
    if(!s->fstab)
        return -1;

    char roms_path[256];
    sprintf(roms_path, "%s/roms/"INTERNAL_ROM_NAME, multirom_dir);
    DIR *d = opendir(roms_path);
    if(!d)
    {
        ERROR("Failed to open Internal ROM's folder, creating one with ROM from internal memory...\n");
        multirom_import_internal();
    }
    else
        closedir(d);

    sprintf(roms_path, "%s/roms", multirom_dir);
    d = opendir(roms_path);
    if(!d)
    {
        ERROR("Failed to open roms dir!\n");
        return -1;
    }

    struct dirent *dr;
    char path[256];
    struct multirom_rom **add_roms = NULL;
    while((dr = readdir(d)))
    {
        if(dr->d_name[0] == '.')
            continue;

        if(dr->d_type != T_FOLDER)
            continue;

        if(strlen(dr->d_name) > MAX_ROM_NAME_LEN)
        {
            ERROR("Skipping ROM %s, name is too long (max %d chars allowed)", dr->d_name, MAX_ROM_NAME_LEN);
            continue;
        }

        INFO("Adding ROM %s\n", dr->d_name);

        struct multirom_rom *rom = malloc(sizeof(struct multirom_rom));
        memset(rom, 0, sizeof(struct multirom_rom));

        rom->id = multirom_generate_rom_id();
        rom->name = strdup(dr->d_name);

        sprintf(path, "%s/%s", roms_path, rom->name);
        rom->base_path = strdup(path);

        rom->type = multirom_get_rom_type(rom);

        sprintf(path, "%s/boot.img", rom->base_path);
        rom->has_bootimg = access(path, R_OK) == 0 ? 1 : 0;

        list_add(rom, &add_roms);
    }

    closedir(d);

    if(add_roms)
    {
        // sort roms
        qsort(add_roms, list_item_count(add_roms), sizeof(struct multirom_rom*), compare_rom_names);

        // add them to main list
        list_swap(&add_roms, &s->roms);
    }

    s->current_rom = multirom_get_internal(s);
    if(!s->current_rom)
    {
        ERROR("No internal rom found!\n");
        return -1;
    }

    s->auto_boot_rom = s->current_rom;
    s->auto_boot_seconds = 5;
    s->auto_boot_type = AUTOBOOT_NAME;

    return 0;
}

int multirom_load_status(struct multirom_status *s)
{
    INFO("Loading MultiROM status...\n");

    multirom_default_status(s);

    char arg[256];
    sprintf(arg, "%s/multirom.ini", multirom_dir);

    FILE *f = fopen(arg, "r");
    if(!f)
    {
        ERROR("Failed to open config file, using defaults!\n");
        return -1;
    }

    char line[1024];
    char current_rom[256] = { 0 };
    char auto_boot_rom[256] = { 0 };

    char name[64];
    char *pch;

    if(multirom_get_bootloader_cmdline(s, line, sizeof(line)) != 0)
    {
        ERROR("Failed to get cmdline!\n");
        fclose(f);
        return -1;
    }

    if(multirom_search_last_kmsg(SECOND_BOOT_KMESG) == 0)
        s->is_second_boot = 1;

    while((fgets(line, sizeof(line), f)))
    {
        pch = strtok (line, "=\n");
        if(!pch) continue;
        strcpy(name, pch);
        pch = strtok (NULL, "=\n");
        if(!pch) continue;
        strcpy(arg, pch);

        if(strstr(name, "current_rom"))
            strcpy(current_rom, arg);
        else if(strstr(name, "auto_boot_seconds"))
            s->auto_boot_seconds = atoi(arg);
        else if(strstr(name, "auto_boot_rom"))
            strcpy(auto_boot_rom, arg);
        else if(strstr(name, "auto_boot_type"))
            s->auto_boot_type = atoi(arg);
        else if(strstr(name, "curr_rom_part"))
            s->curr_rom_part = strdup(arg);
        else if(strstr(name, "colors"))
            s->colors = atoi(arg);
        else if(strstr(name, "brightness"))
            s->brightness = atoi(arg);
        else if(strstr(name, "enable_adb"))
            s->enable_adb = atoi(arg);
        else if(strstr(name, "hide_internal"))
            s->hide_internal = atoi(arg);
        else if(strstr(name, "int_display_name"))
            s->int_display_name = strdup(arg);
        else if(strstr(name, "rotation"))
            s->rotation = atoi(arg);
    }

    fclose(f);

    // find USB drive if we're booting from it
    if(s->curr_rom_part && s->is_second_boot)
    {
        struct usb_partition *p = NULL;
        int tries = 0;
        while(!p && tries < 10)
        {
            multirom_update_partitions(s);
            p = multirom_get_partition(s, s->curr_rom_part);

            if(p)
            {
                multirom_scan_partition_for_roms(s, p);
                break;
            }

            ++tries;
            ERROR("part %s not found, waiting 1s (%d)\n", s->curr_rom_part, tries);
            sleep(1);
        }
    }

    s->current_rom = multirom_get_rom(s, current_rom, s->curr_rom_part);
    if(!s->current_rom)
    {
        ERROR("Failed to select current rom (%s, part %s), using Internal!\n", current_rom, s->curr_rom_part);
        s->current_rom = multirom_get_internal(s);
        if(!s->current_rom)
        {
            ERROR("No internal rom found!\n");
            return -1;
        }
    }

    if((s->auto_boot_type & AUTOBOOT_LAST) && !s->curr_rom_part)
    {
        s->auto_boot_rom = s->current_rom;
    }
    else
    {
        s->auto_boot_rom = multirom_get_rom(s, auto_boot_rom, NULL);
        if(!s->auto_boot_rom)
            ERROR("Could not find rom %s to auto-boot", auto_boot_rom);
    }

    if(s->int_display_name)
    {
        struct multirom_rom *r = multirom_get_internal(s);
        r->name = realloc(r->name, strlen(s->int_display_name)+1);
        strcpy(r->name, s->int_display_name);
    }

    return 0;
}

int multirom_save_status(struct multirom_status *s)
{
    INFO("Saving multirom status\n");

    char buff[256];
    sprintf(buff, "%s/multirom.ini", multirom_dir);

    FILE *f = fopen(buff, "w");
    if(!f)
    {
        ERROR("Failed to open/create status file!\n");
        return -1;
    }


    char *auto_boot_name = buff;
    if(s->auto_boot_rom)
    {
        if(s->auto_boot_rom->type == ROM_DEFAULT)
            strcpy(buff, INTERNAL_ROM_NAME);
        else
            auto_boot_name = s->auto_boot_rom->name;
    }
    else
        buff[0] = 0;

    fprintf(f, "current_rom=%s\n", s->current_rom ? s->current_rom->name : multirom_get_internal(s)->name);
    fprintf(f, "auto_boot_seconds=%d\n", s->auto_boot_seconds);
    fprintf(f, "auto_boot_rom=%s\n", auto_boot_name);
    fprintf(f, "auto_boot_type=%d\n", s->auto_boot_type);
    fprintf(f, "curr_rom_part=%s\n", s->curr_rom_part ? s->curr_rom_part : "");
    fprintf(f, "colors=%d\n", s->colors);
    fprintf(f, "brightness=%d\n", s->brightness);
    fprintf(f, "enable_adb=%d\n", s->enable_adb);
    fprintf(f, "hide_internal=%d\n", s->hide_internal);
    fprintf(f, "int_display_name=%s\n", s->int_display_name ? s->int_display_name : "");
    fprintf(f, "rotation=%d\n", s->rotation);

    fclose(f);
    return 0;
}

void multirom_dump_status(struct multirom_status *s)
{
    INFO("Dumping multirom status:\n");
    INFO("  is_second_boot=%d\n", s->is_second_boot);
    INFO("  current_rom=%s\n", s->current_rom ? s->current_rom->name : "NULL");
    INFO("  colors=%d\n", s->colors);
    INFO("  brightness=%d\n", s->brightness);
    INFO("  enable_adb=%d\n", s->enable_adb);
    INFO("  hide_internal=%d\n", s->hide_internal);
    INFO("  int_display_name=%s\n", s->int_display_name ? s->int_display_name : "NULL");
    INFO("  auto_boot_seconds=%d\n", s->auto_boot_seconds);
    INFO("  auto_boot_rom=%s\n", s->auto_boot_rom ? s->auto_boot_rom->name : "NULL");
    INFO("  auto_boot_type=%d\n", s->auto_boot_type);
    INFO("  curr_rom_part=%s\n", s->curr_rom_part ? s->curr_rom_part : "NULL");
    INFO("\n");

    int i;
    for(i = 0; s->roms && s->roms[i]; ++i)
    {
        INFO("  ROM: %s\n", s->roms[i]->name);
        INFO("    base_path: %s\n", s->roms[i]->base_path);
        INFO("    type: %d\n", s->roms[i]->type);
        INFO("    has_bootimg: %d\n", s->roms[i]->has_bootimg);
        if(s->roms[i]->partition)
            INFO("    partition: %s (%s)\n", s->roms[i]->partition->name, s->roms[i]->partition->fs);
    }
}

void multirom_free_status(struct multirom_status *s)
{
    list_clear(&s->partitions, &multirom_destroy_partition);
    list_clear(&s->roms, &multirom_free_rom);
    free(s->curr_rom_part);
    free(s->int_display_name);
    fstab_destroy(s->fstab);
}

void multirom_free_rom(void *rom)
{
    free(((struct multirom_rom*)rom)->name);
    free(((struct multirom_rom*)rom)->base_path);
    free(rom);
}

void multirom_find_usb_roms(struct multirom_status *s)
{
    // remove USB roms
    int i;
    for(i = 0; s->roms && s->roms[i];)
    {
        if(s->roms[i]->partition)
        {
            list_rm_at(i, &s->roms, &multirom_free_rom);
            i = 0;
        }
        else ++i;
    }

    char path[256];
    struct usb_partition *p;

    pthread_mutex_lock(&parts_mutex);
    for(i = 0; s->partitions && s->partitions[i]; ++i)
        multirom_scan_partition_for_roms(s, s->partitions[i]);
    pthread_mutex_unlock(&parts_mutex);

    multirom_dump_status(s);
}

int multirom_scan_partition_for_roms(struct multirom_status *s, struct usb_partition *p)
{
    char path[256];
    int i;
    struct dirent *dr;
    struct multirom_rom **add_roms = NULL;

#ifdef MR_MOVE_USB_DIR
    // groupers will have old "multirom" folder on USB drive instead of "multirom-grouper".
    // We have to move it.
    sprintf(path, "%s/multirom", p->mount_path);
    if(access(path, F_OK) >= 0)
    {
        char dest[256];
        sprintf(dest, "%s/multirom-"TARGET_DEVICE, p->mount_path);

        INFO("Moving usb dir %s to %s!\n", path, dest);

        mkdir(dest, 0777);

        char *cmd[] = { busybox_path, "sh", "-c", malloc(1024), NULL };
        sprintf(cmd[3], "%s mv \"%s\"/* \"%s\"/", busybox_path, path, dest);

        run_cmd(cmd);

        rmdir(path);
        free(cmd[3]);

        sync();
    }
#endif

    sprintf(path, "%s/multirom-"TARGET_DEVICE, p->mount_path);
    if(access(path, F_OK) < 0)
        return -1;

    DIR *d = opendir(path);
    if(!d)
        return -1;

    while((dr = readdir(d)) != NULL)
    {
        if(dr->d_name[0] == '.')
            continue;

        struct multirom_rom *rom = malloc(sizeof(struct multirom_rom));
        memset(rom, 0, sizeof(struct multirom_rom));

        rom->id = multirom_generate_rom_id();
        rom->name = strdup(dr->d_name);

        sprintf(path, "%s/multirom-"TARGET_DEVICE"/%s", p->mount_path, rom->name);
        rom->base_path = strdup(path);

        rom->partition = p;
        rom->type = multirom_get_rom_type(rom);

        sprintf(path, "%s/boot.img", rom->base_path);
        rom->has_bootimg = access(path, R_OK) == 0 ? 1 : 0;

        list_add(rom, &add_roms);
    }
    closedir(d);

    if(add_roms)
    {
        // sort roms
        qsort(add_roms, list_item_count(add_roms), sizeof(struct multirom_rom*), compare_rom_names);

        list_add_from_list(add_roms, &s->roms);
        list_clear(&add_roms, NULL);
    }
    return 0;
}

int multirom_path_exists(char *base, char *filename)
{
    char path[256];
    sprintf(path, "%s/%s", base, filename);
    if(access(path, R_OK) < 0)
        return -1;
    return 0;
}

int multirom_get_rom_type(struct multirom_rom *rom)
{
    if(!rom->partition && strcmp(rom->name, INTERNAL_ROM_NAME) == 0)
        return ROM_DEFAULT;

    char *b = rom->base_path;

    // Handle android ROMs
    if(!multirom_path_exists(b, "boot"))
    {
        if (!multirom_path_exists(b, "system") && !multirom_path_exists(b, "data") &&
            !multirom_path_exists(b, "cache"))
        {
            if(!rom->partition) return ROM_ANDROID_INTERNAL;
            else                return ROM_ANDROID_USB_DIR;
        }
        else if(!multirom_path_exists(b, "system.img") && !multirom_path_exists(b, "data.img") &&
                !multirom_path_exists(b, "cache.img"))
        {
            return ROM_ANDROID_USB_IMG;
        }
    }

    // handle linux ROMs
    if(!multirom_path_exists(b, "rom_info.txt"))
    {
        if(!rom->partition)
            return ROM_LINUX_INTERNAL;
        else
            return ROM_LINUX_USB;
    }

    // Handle Ubuntu 13.04 - deprecated
    if ((!multirom_path_exists(b, "root") && multirom_path_exists(b, "boot.img")) ||
       (!multirom_path_exists(b, "root.img") && rom->partition))
    {
        // try to copy rom_info.txt in there, ubuntu is deprecated
        ERROR("Found deprecated Ubuntu 13.04, trying to copy rom_info.txt...\n");
        char *cmd[] = { busybox_path, "cp", malloc(256), malloc(256), NULL };
        sprintf(cmd[2], "%s/infos/ubuntu.txt", multirom_dir);
        sprintf(cmd[3], "%s/rom_info.txt", b);

        int res = run_cmd(cmd);

        free(cmd[2]);
        free(cmd[3]);

        if(res != 0)
        {
            ERROR("Failed to copy rom_info for Ubuntu!\n");
            if(!rom->partition) return ROM_UNSUPPORTED_INT;
            else                return ROM_UNSUPPORTED_USB;
        }
        else
        {
            if(!rom->partition) return ROM_LINUX_INTERNAL;
            else                return ROM_LINUX_USB;
        }
    }

    // Handle ubuntu 12.10
    if(!multirom_path_exists(b, "root") && !multirom_path_exists(b, "boot.img"))
    {
        if(!rom->partition) return ROM_UNSUPPORTED_INT;
        else                return ROM_UNSUPPORTED_USB;
    }

    return ROM_UNKNOWN;
}

void multirom_import_internal(void)
{
    char path[256];

    // multirom
    mkdir(multirom_dir, 0777); 

    // roms
    sprintf(path, "%s/roms", multirom_dir);
    mkdir(path, 0777);

    // internal rom
    sprintf(path, "%s/roms/%s", multirom_dir, INTERNAL_ROM_NAME);
    mkdir(path, 0777);
}

struct multirom_rom *multirom_get_internal(struct multirom_status *s)
{
    int i;
    for(i = 0; s->roms && s->roms[i]; ++i)
    {
        if(s->roms[i]->type == ROM_DEFAULT)
            return s->roms[i];
    }
    ERROR(" Something is wrong, multirom_get_internal returns NULL!\n");
    return NULL;
}

struct multirom_rom *multirom_get_rom(struct multirom_status *s, const char *name, const char *part_uuid)
{
    int i = 0;
    struct multirom_rom *r;
    for(; s->roms && s->roms[i]; ++i)
    {
        r = s->roms[i];
        if (strcmp(r->name, name) == 0 && 
           (!part_uuid || (r->partition && strcmp(r->partition->uuid, part_uuid) == 0)))
        {
            return r;
        }
    }

    return NULL;
}

int multirom_generate_rom_id(void)
{
    static int id = 0;
    return id++;
}

struct multirom_rom *multirom_get_rom_by_id(struct multirom_status *s, int id)
{
    int i = 0;
    for(; s->roms && s->roms[i]; ++i)
        if(s->roms[i]->id == id)
            return s->roms[i];
    return NULL;
}

int multirom_prepare_for_boot(struct multirom_status *s, struct multirom_rom *to_boot)
{
    int exit = EXIT_UMOUNT;
    int type = to_boot->type;

    if(((M(type) & MASK_KEXEC) || to_boot->has_bootimg) && type != ROM_DEFAULT && s->is_second_boot == 0)
    {
        if(multirom_load_kexec(s, to_boot) != 0)
            return -1;
        exit |= EXIT_KEXEC;
    }

    switch(type)
    {
        case ROM_DEFAULT:
        case ROM_LINUX_INTERNAL:
        case ROM_LINUX_USB:
            break;
        case ROM_ANDROID_USB_IMG:
        case ROM_ANDROID_USB_DIR:
        case ROM_ANDROID_INTERNAL:
        {
            if(!(exit & (EXIT_REBOOT | EXIT_KEXEC)))
            {
                exit &= ~(EXIT_UMOUNT);

                if(multirom_prep_android_mounts(to_boot) == -1)
                    return -1;

                if(multirom_create_media_link() == -1)
                    return -1;
            }

            if(to_boot->partition)
                to_boot->partition->keep_mounted = 1;
            break;
        }
        default:
            ERROR("Unknown ROM type\n");
            return -1;
    }

    return exit;
}

#define EXEC_MASK (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP)

char *multirom_find_fstab_in_rc(const char *rcfile)
{
    FILE *f = fopen(rcfile, "r");
    if(!f)
    {
        ERROR("Failed to open rcfile %s\n", rcfile);
        return NULL;
    }

    char *p,*e;
    char line[1024];
    while(fgets(line, sizeof(line), f))
    {
        for(p = line; isspace(*p); ++p) { }

        for(e = p+strlen(p)-1; isspace(*e); --e)
            *e = 0;

        if(*p == '#' || *p == 0)
            continue;

        if(strstr(p, "mount_all") == p)
        {
            fclose(f);

            p += sizeof("mount_all")-1;
            for(; isspace(*p); ++p) { }

            return strdup(p);
        }
    }
    fclose(f);
    return NULL;
}

int multirom_prep_android_mounts(struct multirom_rom *rom)
{
    char in[128];
    char out[128];
    char path[256];
    char *fstab_name = NULL;
    int has_fw = 0;

    sprintf(path, "%s/firmware.img", rom->base_path);
    has_fw = (access(path, R_OK) >= 0);

    sprintf(path, "%s/boot", rom->base_path);

    DIR *d = opendir(path);
    if(!d)
    {
        ERROR("Failed to open rom path %s", path);
        return -1;
    }

    struct dirent *dp = NULL;

    while((dp = readdir(d)))
    {
        if(dp->d_name[0] == '.' && (dp->d_name[1] == '.' || dp->d_name[1] == 0))
            continue;

        sprintf(in, "%s/%s", path, dp->d_name);
        sprintf(out, "/%s", dp->d_name);

        copy_file(in, out);

        if(strstr(dp->d_name, ".rc"))
        {
            // set permissions for .rc files
            chmod(out, EXEC_MASK);

            if(!fstab_name && strcmp(dp->d_name, "init."TARGET_DEVICE".rc") == 0)
                fstab_name = multirom_find_fstab_in_rc(out);
        }
    }
    closedir(d);

    if(multirom_process_android_fstab(fstab_name, has_fw) != 0)
        return -1;

    mkdir_with_perms("/system", 0755, NULL, NULL);
    mkdir_with_perms("/data", 0771, "system", "system");
    mkdir_with_perms("/cache", 0770, "system", "cache");
    if(has_fw)
        mkdir_with_perms("/firmware", 0771, "system", "system");

    static const char *folders[2][3] = 
    {
        { "system", "data", "cache" },
        { "system.img", "data.img", "cache.img" },
    };

    unsigned long flags[2][3] = {
        { MS_BIND | MS_RDONLY, MS_BIND, MS_BIND },
        { MS_RDONLY | MS_NOATIME, MS_NOATIME, MS_NOATIME },
    };

    uint32_t i;
    char from[256];
    char to[256];
    int img = (int)(rom->type == ROM_ANDROID_USB_IMG);
    for(i = 0; i < ARRAY_SIZE(folders[0]); ++i)
    {
        sprintf(from, "%s/%s", rom->base_path, folders[img][i]);
        sprintf(to, "/%s", folders[0][i]);

        if(img == 0)
        {
            if(mount(from, to, "ext4", flags[img][i], "discard,nomblk_io_submit") < 0)
            {
                ERROR("Failed to mount %s to %s (%d: %s)", from, to, errno, strerror(errno));
                return -1;
            }
        }
        else
        {
            if(multirom_mount_loop(from, to, "ext4", flags[img][i], NULL) < 0)
                return -1;
        }
    }

    if(has_fw)
    {
        INFO("Mounting ROM's FW image instead of FW partition\n");
        sprintf(from, "%s/firmware.img", rom->base_path);
        if(multirom_mount_loop(from, "/firmware", "vfat", MS_RDONLY,
            "shortname=lower,uid=1000,gid=1000,dmask=227,fmask=337") < 0)
        {
            return -1;
        }
    }

#if MR_DEVICE_HOOKS >= 1
    int res = mrom_hook_after_android_mounts(busybox_path, rom->base_path, rom->type);
    if(res < 0)
    {
        ERROR("mrom_hook_after_android_mounts returned %d!\n", res);
        return -1;
    }
#endif

    return 0;
}

int multirom_process_android_fstab(char *fstab_name, int has_fw)
{
    if(fstab_name != NULL)
        INFO("Using fstab %s from rc files\n", fstab_name);
    else
    {
        DIR *d = opendir("/");
        if(!d)
        {
            ERROR("Failed to open root folder!\n");
            return -1;
        }

        struct dirent *dp = NULL;
        while((dp = readdir(d)))
        {
            if(strstr(dp->d_name, "fstab.") == dp->d_name && strcmp(dp->d_name, "fstab.goldfish") != 0)
            {
                fstab_name = realloc(fstab_name, strlen(dp->d_name)+1);
                strcpy(fstab_name, dp->d_name);
                // try to find specifically fstab.device
                if(strcmp(fstab_name, "fstab."TARGET_DEVICE) == 0)
                    break;
            }
        }
        closedir(d);

        if(!fstab_name)
        {
            ERROR("Failed to find fstab file in root!\n");
            return -1;
        }
    }

    ERROR("Modifying fstab: %s\n", fstab_name);

    FILE *fstab = NULL;
    long len = 0;
    char *line = NULL;
    char *out = NULL;
    int res = -1;
    int counter = 0;
    int has_fstab_line = 0;

    static const char *dummy_fstab_line =
        "# Android considers empty fstab invalid, so MultiROM has to add _something_ to process triggers\n"
        "tmpfs\t/dummy_tmpfs\ttmpfs\tro,nosuid,nodev\tdefaults\n";

    fstab = fopen(fstab_name, "r");
    if(!fstab)
    {
        ERROR("Failed to open %s\n", fstab_name);
        goto exit;
    }

    fseek(fstab, 0, SEEK_END);
    len = ftell(fstab);
    fseek(fstab, 0, SEEK_SET);

#define FSTAB_LINE_LEN 2048
    line = malloc(FSTAB_LINE_LEN);
    out = malloc(len + 5 + sizeof(dummy_fstab_line));
    out[0] = 0;

    while((fgets(line, FSTAB_LINE_LEN, fstab)))
    {
        if(line[0] != '#')
        {
            if (strstr(line, "/system") || strstr(line, "/cache") || strstr(line, "/data") ||
                (has_fw && strstr(line, "/firmware")))
            {
                strcat(out, "#");
                if(++counter > 3 + has_fw)
                {
                    ERROR("Commented %u lines instead of 3 in fstab, stopping boot!\n", counter);
                    fclose(fstab);
                    goto exit;
                }
            }
            else if(line[0] != '\n' && line[0] != ' ')
                has_fstab_line = 1;
        }

        strcat(out, line);
    }
    fclose(fstab);

    // Android considers empty fstab invalid
    if(has_fstab_line == 0)
    {
        INFO("fstab would be empty, adding dummy line\n");
        strcat(out, dummy_fstab_line);
        mkdir("/dummy_tmpfs", 0644);
    }

    fstab = fopen(fstab_name, "w");
    if(!fstab)
    {
        ERROR("Failed to open %s for writing!", fstab_name);
        goto exit;
    }

    fputs(out, fstab);
    fclose(fstab);

    res = 0;
exit:
    free(line);
    free(out);
    free(fstab_name);
    return res;
}

int multirom_create_media_link(void)
{
    int media_new = 0;
    int api_level = multirom_get_api_level("/system/build.prop");
    if(api_level <= 0)
        return -1;

    struct stat info;
    if(stat(REALDATA"/media/0", &info) >= 0)
        media_new = 1;

    static const char *paths[] = {
        REALDATA"/media",      // 0
        REALDATA"/media/0",    // 1

        "/data/media",         // 2
        "/data/media/0",       // 3
    };

    int from, to;

    if(api_level <= 16)
    {
        to = 2;
        if(!media_new) from = 0;
        else           from = 1;
    }
    else if(api_level >= 17)
    {
        from = 0;
        if(!media_new) to = 3;
        else           to = 2;
    }

    ERROR("Making media dir: api %d, media_new %d, %s to %s", api_level, media_new, paths[from], paths[to]);
    if(mkdir_recursive(paths[to], 0775) == -1)
    {
        ERROR("Failed to make media dir");
        return -1;
    }

    if(mount(paths[from], paths[to], "ext4", MS_BIND, "") < 0)
    {
        ERROR("Failed to bind media folder %d (%s)", errno, strerror(errno));
        return -1;
    }

    if(api_level >= 17)
    {
        FILE *f = fopen(LAYOUT_VERSION, "w");
        if(!f)
        {
            ERROR("Failed to create .layout_version!\n");
            return -1;
        }
        fputs("2", f);
        fclose(f);
        chmod(LAYOUT_VERSION, 0600);
    }
    return 0;
}

int multirom_get_api_level(const char *path)
{
    FILE *f = fopen(path, "r");
    if(!f)
    {
        ERROR("Could not open %s to read api level!", path);
        return -1;
    }

    int res = -1;
    char line[256];
    while(res == -1 && (fgets(line, sizeof(line), f)))
    {
        if(strstr(line, "ro.build.version.sdk=") == line)
            res = atoi(strchr(line, '=')+1);
    }
    fclose(f);

    if(res == 0)
        ERROR("Invalid ro.build.version.sdk line in build.prop");

    return res;
}

void multirom_take_screenshot(void)
{
    char *buffer = NULL;
    int len = fb_clone(&buffer);

    int counter;
    char path[256];
    struct stat info;
    FILE *f = NULL;

    for(counter = 0; 1; ++counter)
    {
        sprintf(path, "%s/screenshot_%02d.raw", multirom_dir, counter);
        if(stat(path, &info) >= 0)
            continue;

        f = fopen(path, "w");
        if(f)
        {
            fwrite(buffer, 1, len, f);
            fclose(f);
        }
        break;
    }

    free(buffer);

    fb_fill(WHITE);
    fb_update();
    usleep(100000);
    fb_draw();
}

int multirom_get_trampoline_ver(void)
{
    static int ver = -2;
    if(ver == -2)
    {
        ver = -1;

        char *cmd[] = { "/init", "-v", NULL };
        char *res = run_get_stdout(cmd);
        if(res)
        {
            ver = atoi(res);
            free(res);
        }
    }
    return ver;
}

int multirom_has_kexec(void)
{
    static int has_kexec = -2;
    if(has_kexec != -2)
        return has_kexec;

    if(access("/proc/config.gz", F_OK) >= 0)
    {
        char *cmd_cp[] = { busybox_path, "cp", "/proc/config.gz", "/config.gz", NULL };
        run_cmd(cmd_cp);

        char *cmd_gzip[] = { busybox_path, "gzip", "-d", "/config.gz", NULL };
        run_cmd(cmd_gzip);

        has_kexec = 0;

        uint32_t i;
        static const char *checks[] = {
            "CONFIG_KEXEC_HARDBOOT=y",
#ifndef MR_KEXEC_DTB
            "CONFIG_ATAGS_PROC=y",
#else
            "CONFIG_PROC_DEVICETREE=y",
#endif
        };
        //                   0             1       2     3
        char *cmd_grep[] = { busybox_path, "grep", NULL, "/config", NULL };
        for(i = 0; i < ARRAY_SIZE(checks); ++i)
        {
            cmd_grep[2] = (char*)checks[i];
            if(run_cmd(cmd_grep) != 0)
            {
                has_kexec = -1;
                ERROR("%s not found in /proc/config.gz!\n", checks[i]);
            }
        }

        remove("/config");
    }
    else
    {
        // Kernel without /proc/config.gz enabled - check for /proc/atags file,
        // if it is present, there is good change kexec-hardboot is enabled too.
        ERROR("/proc/config.gz is not available!\n");
#ifndef MR_KEXEC_DTB
        const char *checkfile = "/proc/atags";
#else
        const char *checkfile = "/proc/device-tree";
#endif
        if(access(checkfile, R_OK) < 0)
        {
            ERROR("%s was not found!\n", checkfile);
            has_kexec = -1;
        }
        else
            has_kexec = 0;
    }

    return has_kexec;
}

int multirom_get_bootloader_cmdline(struct multirom_status *s, char *str, size_t size)
{
    FILE *f;
    char *c, *e, *l;
    int res = -1;
    struct boot_img_hdr hdr;
    struct fstab_part *boot;

    f = fopen("/proc/cmdline", "r");
    if(!f)
        return -1;

    str[0] = 0;

    if(fgets(str, size, f) == NULL)
        goto exit;

    for(c = str; *c; ++c)
        if(*c == '\n')
            *c = ' ';

    // Remove the part from boot.img
    boot = fstab_find_by_path(s->fstab, "/boot");
    if(boot && libbootimg_load_header(&hdr, boot->device) >= 0)
    {
        l = (char*)hdr.cmdline;
        hdr.cmdline[BOOT_ARGS_SIZE-1] = 0;

#ifdef FLO_CMDLINE_HACK
        // Flo's bootloader (at least 03.15) removes first 26 characters
        // from boot.img's cmdline because of reasons. On stock
        // boot.img, those 26 characters are "console=ttyHSL0,115200,n8 "
        l += 26;
#endif

        if(*l != 0 && (c = strstr(str, l)))
        {
            e = c + strlen(l);
            if(*e == ' ')
                ++e;
            memmove(c, e, strlen(e)+1); // plus NULL
        }
    }

    res = 0;
exit:
    fclose(f);
    return res;
}

int multirom_find_file(char *res, const char *name_part, const char *path)
{
    DIR *d = opendir(path);
    if(!d)
        return -1;

    int wild = 0;
    int len = strlen(name_part);
    char *name = (char*)name_part;
    char *i;
    if((i = strchr(name_part, '*')))
    {
        wild = 1;
        name = strndup(name_part, i-name);
    }

    int ret= -1;
    struct dirent *dr;
    while(ret == -1 && (dr = readdir(d)))
    {
        if(dr->d_name[0] == '.')
            continue;

        if ((!wild && strcmp(dr->d_name, name)) ||
           ((wild && !strstr(dr->d_name, name))))
            continue;

        sprintf(res, "%s/%s", path, dr->d_name);
        ret = 0;
    }
    closedir(d);
    if(wild)
        free(name);
    return ret;
}

int multirom_load_kexec(struct multirom_status *s, struct multirom_rom *rom)
{
    // to find /data partition
    if(!rom->partition && multirom_update_partitions(s) < 0)
    {
        ERROR("Failed to update partitions\n");
        return -1;
    }

    int res = -1;
    // kexec --load-hardboot ./zImage --command-line="$(cat /proc/cmdline)" --mem-min=0xA0000000 --initrd=./rd.img
    // --mem-min should be somewhere in System RAM (see /proc/iomem). Location just above kernel seems to work fine.
    // It must not conflict with vmalloc ram. Vmalloc area seems to be allocated from top of System RAM.
    char *cmd[] = {
        kexec_path,                      // 0
        "--load-hardboot",               // 1
        malloc(1024),                    // 2 - path to zImage
        "--mem-min="MR_KEXEC_MEM_MIN,    // 3
        malloc(1024),                    // 4 - --initrd=<path to initrd>
        malloc(2048),                    // 5 - --command-line=<cmdline>
#ifdef MR_KEXEC_DTB
        "--dtb",                         // 6
#endif
        NULL
    };

    int loop_mounted = 0;
    switch(rom->type)
    {
        case ROM_ANDROID_INTERNAL:
        case ROM_ANDROID_USB_DIR:
        case ROM_ANDROID_USB_IMG:
            if(multirom_fill_kexec_android(s, rom, cmd) != 0)
                goto exit;
            break;
        case ROM_LINUX_INTERNAL:
        case ROM_LINUX_USB:
            loop_mounted = multirom_fill_kexec_linux(s, rom, cmd);
            if(loop_mounted < 0)
                goto exit;
            break;
        default:
            ERROR("Unsupported rom type to kexec (%d)!\n", rom->type);
            goto exit;
    }

    ERROR("Loading kexec: %s %s %s %s %s\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
    ERROR("With cmdline: ");
    char *itr = cmd[5];
    int len;
    for(len = strlen(itr); len > 0; len = strlen(itr))
    {
       if(len > 450)
           len = 450;
       char *b = strndup(itr, len);
       ERROR("  %s\n", b);
       free(b);
       itr += len;
    }

    if(run_cmd(cmd) == 0)
        res = 0;
    else
    {
        ERROR("kexec call failed, re-running it to get info:\n");
        char *r = run_get_stdout(cmd);
        if(!r)
            ERROR("run_get_stdout returned NULL!\n");
        char *p = strtok(r, "\n\r");
        while(p)
        {
            ERROR("  %s\n", p);
            p = strtok(NULL, "\n\r");
        }
        free(r);
    }

    char *cmd_cp[] = { busybox_path, "cp", kexec_path, "/kexec", NULL };
    run_cmd(cmd_cp);
    chmod("/kexec", 0755);

    if(loop_mounted)
        umount("/mnt/image");

    multirom_copy_log(NULL);

exit:
    free(cmd[2]);
    free(cmd[4]);
    free(cmd[5]);
    return res;
}

int multirom_fill_kexec_android(struct multirom_status *s, struct multirom_rom *rom, char **cmd)
{
    int res = -1;
    char img_path[256];
    sprintf(img_path, "%s/boot.img", rom->base_path);

    struct bootimg img;
    if(libbootimg_init_load(&img, img_path) < 0)
    {
        ERROR("fill_kexec could not open boot image (%s)!", img_path);
        return -1;
    }

    if(libbootimg_dump_kernel(&img, "/zImage") < 0)
        goto exit;
    if(libbootimg_dump_ramdisk(&img, "/initrd.img") < 0)
        goto exit;

    // Trampolines in ROM boot images may get out of sync, so we need to check it and
    // update if needed. I can't do that during ZIP installation because of USB drives.
    // That header.name is added by recovery.
    int ver = 0;
    if(strncmp((char*)img.hdr.name, "tr_ver", 6) == 0)
        ver = atoi((char*)img.hdr.name + 6);

    if(ver < multirom_get_trampoline_ver())
    {
        if(multirom_update_rd_trampoline("/initrd.img") != 0)
            goto exit;
        else
        {
            // Update the boot.img
            snprintf((char*)img.hdr.name, BOOT_NAME_SIZE, "tr_ver%d", multirom_get_trampoline_ver());
            img.size = 0; // reset to enable any size

            if(libbootimg_load_ramdisk(&img, "/initrd.img") >= 0)
            {
                char tmp[256];
                strcpy(tmp, img_path);
                strcat(tmp, ".new");
                if(libbootimg_write_img(&img, tmp) >= 0)
                {
                    INFO("Writing boot.img updated with trampoline v%d\n", multirom_get_trampoline_ver());
                    rename(tmp, img_path);
                }
            }
        }
    }

    char cmdline[1024];
    if(multirom_get_bootloader_cmdline(s, cmdline, sizeof(cmdline)) == -1)
    {
        ERROR("Failed to get cmdline\n");
        goto exit;
    }

    strcpy(cmd[2], "/zImage");
    strcpy(cmd[4], "--initrd=/initrd.img");

    strcpy(cmd[5], "--command-line=");
    if(img.hdr.cmdline[0] != 0)
    {
        img.hdr.cmdline[BOOT_ARGS_SIZE-1] = 0;

        // see multirom_get_bootloader_cmdline
#ifdef FLO_CMDLINE_HACK
        strcat(cmd[5], (char*)img.hdr.cmdline+26);
#else
        strcat(cmd[5], (char*)img.hdr.cmdline);
#endif
        strcat(cmd[5], " ");
    }
    if(cmdline[0] != 0)
    {
        strcat(cmd[5], cmdline);
        strcat(cmd[5], " ");
    }
    strcat(cmd[5], "mrom_kexecd=1");

    res = 0;
exit:
    libbootimg_destroy(&img);
    return res;
}

static char *find_boot_file(char *path, char *root_path, char *base_path)
{
    if(!path)
        return NULL;

    struct stat info;
    char cmd[256];
    char *root = strstr(path, "%r");
    if(root)
        sprintf(cmd, "%s/%s", root_path, root+2);
    else
        sprintf(cmd, "%s/%s", base_path, path);

    char *last = strrchr(cmd, '/');
    if(!last)
    {
        ERROR("Failed to find boot file: %s\n", cmd);
        return NULL;
    }

    *last = 0;

    char *name = strdup(last+1);
    char res[256];
    if(multirom_find_file(res, name, cmd) < 0)
    {
        ERROR("Failed to find boot file: %s\n", cmd);
        free(name);
        return NULL;
    }
    return strdup(res);
}

int multirom_fill_kexec_linux(struct multirom_status *s, struct multirom_rom *rom, char **cmd)
{
    struct rom_info *info = multirom_parse_rom_info(s, rom);
    if(!info)
        return -1;

    int res = -1;
    int root_type = -1; // 0 = dir, 1 = img
    int loop_mounted = 0;
    char root_path[256];
    char base_path[64];

    if(!rom->partition)
        strcpy(base_path, REALDATA);
    else
        strcpy(base_path, rom->partition->mount_path);

    struct stat st;
    char path[256];
    char *tmp;

    if((tmp = map_get_val(info->str_vals, "root_img")))
    {
        sprintf(path, "%s/%s", base_path, tmp);
        if(stat(path, &st) >= 0)
        {
            root_type = 1;

            char *img_fs = map_get_val(info->str_vals, "root_img_fs");

            // mount the image file
            mkdir("/mnt/image", 0777);
            if(multirom_mount_loop(path, "/mnt/image", img_fs ? img_fs : "ext4", MS_NOATIME, NULL) < 0)
                goto exit;

            loop_mounted = 1;
            strcpy(root_path, "/mnt/image");
        }
        else
            ERROR("Path %s not found!\n", path);
    }

    if(root_type == -1 && (tmp = map_get_val(info->str_vals, "root_dir")))
    {
        sprintf(path, "%s/%s", base_path, tmp);
        if(stat(path, &st) >= 0)
        {
            root_type = 0;
            strcpy(root_path, path);
        }
        else
            ERROR("Path %s not found!\n", path);
    }

    if(root_type == -1)
    {
        ERROR("Failed to find root of the ROM!\n");
        goto exit;
    }

    char *str = find_boot_file(map_get_val(info->str_vals, "kernel_path"), root_path, rom->base_path);
    if(!str)
        goto exit;

    cmd[2] = str;

    str = find_boot_file(map_get_val(info->str_vals, "initrd_path"), root_path, rom->base_path);
    if(str)
    {
        sprintf(cmd[4], "--initrd=%s", str);
        free(str);
    }

    sprintf(cmd[5], "--command-line=%s ", (char*)map_get_val(info->str_vals, "base_cmdline"));

    if(root_type == 0 && (str = map_get_val(info->str_vals, "dir_cmdline")))
        strcat(cmd[5], str);
    else if(root_type == 1 && (str = map_get_val(info->str_vals, "img_cmdline")))
        strcat(cmd[5], str);

    res = loop_mounted;
exit:
    multirom_destroy_rom_info(info);
    return res;
}

#define INFO_LINE_BUFF 4096
struct rom_info *multirom_parse_rom_info(struct multirom_status *s, struct multirom_rom *rom)
{
    char path[256];

    sprintf(path, "%s/rom_info.txt", rom->base_path);
    ERROR("Parsing %s...\n", path);

    FILE *f = fopen(path, "r");
    if(!f)
    {
        ERROR("Failed to open %s!\n", path);
        return NULL;
    }

    struct rom_info *i = malloc(sizeof(struct rom_info));
    memset(i, 0, sizeof(struct rom_info));
    i->str_vals = map_create();

    char *line = malloc(INFO_LINE_BUFF);
    char key[32];
    int line_cnt = 1;
    for(; fgets(line, INFO_LINE_BUFF, f); ++line_cnt)
    {
        if(line[0] == '#')
            continue;

        char *val = strchr(line, '=');
        if(!val || val-line >= (int)(sizeof(key)-1))
            continue;

        strncpy(key, line, val-line);
        key[val-line] = 0;
        ++val; // skip '=' char

        // if string value
        {
            char *str = parse_string(val);
            if(str)
                map_add(i->str_vals, key, str, &free);
            else
                ERROR("Line %d: failed to parse string\n", line_cnt);
        }
    }
    free(line);
    fclose(f);

    static const char *roots[] = { "root_dir", "root_img", NULL };
    int found_root = 0;
    int y;
    for(y = 0; roots[y] && !found_root; ++y)
    {
        if(map_find(i->str_vals, (char*)roots[y]) >= 0)
            found_root = 1;
    }

    if(!found_root)
        ERROR("Failed to find any root key in %s\n", path);

    static const char *req_keys[] = { "type", "kernel_path", "base_cmdline", NULL};
    int failed = !found_root;
    for(y = 0; req_keys[y]; ++y)
    {
        if(map_find(i->str_vals, (char*)req_keys[y]) < 0)
        {
            ERROR("Key \"%s\" key not found in %s\n", req_keys[y], path);
            failed = 1;
        }
    }

    // Only supported type is kexec, check just to make sure older releases
    // can't try to run newer ROMs
    if(failed == 0)
    {
        char *val = map_get_val(i->str_vals, "type");
        if(strcmp(val, "kexec") != 0)
        {
            ERROR("Only supported rom_info type is \"kexec\", this rom_info has type \"%s\"!", val);
            failed = 1;
        }
    }

    if(failed == 1)
    {
        multirom_destroy_rom_info(i);
        return NULL;
    }

    char **ref;
    ERROR("Replacing aliases in root paths...\n");
    for(y = 0; roots[y]; ++y)
        if((ref = map_get_ref(i->str_vals, (char*)roots[y])))
            multirom_replace_aliases_root_path(ref, rom);

    ERROR("Replacing aliases in the cmdline...\n");
    static const char *cmdlines[] = { "base_cmdline", "img_cmdline", "dir_cmdline", NULL };
    for(y = 0; cmdlines[y]; ++y)
        if((ref = map_get_ref(i->str_vals, (char*)cmdlines[y])))
            multirom_replace_aliases_cmdline(ref, i, s, rom);

    return i;
}

void multirom_destroy_rom_info(struct rom_info *info)
{
    if(!info)
        return;

    map_destroy(info->str_vals, &free);
    free(info);
}

/*
# Set up the cmdline
# img_cmdline and dir_cmdline are appended to base_cmdline.
# Several aliases are used:
#  - %b - base command line from bootloader. You want this as first thing in cmdline.
#  - %d - root device. is either "UUID=..." (USB drive) or "/dev/mmcblk0p9" or "/dev/mmcblk0p10"
#  - %r - root fs type
#  - %s - root directory, from root of the root device
#  - %i - root image, from root of the root device
#  - %f - fs of the root image
*/
int multirom_replace_aliases_cmdline(char **s, struct rom_info *i, struct multirom_status *status, struct multirom_rom *rom)
{
    size_t c = strcspn (*s, "%");

    if(strlen(*s) == c)
        return 0;

    struct fstab_part *data_part = NULL;
    if(!rom->partition)
        data_part = fstab_find_by_path(status->fstab, "/data");

    char *buff = malloc(4096);
    memset(buff, 0, 4096);

    char *itr_o = buff;
    char *itr_i = *s;
    int res = -1;

    while(1)
    {
        memcpy(itr_o, itr_i, c);
        itr_o += c;
        itr_i += c;

        if(*itr_i != '%')
            break;

        ++itr_i;
        switch(*itr_i)
        {
            // base command line from bootloader. You want this as first thing in cmdline.
            case 'b':
            {
                if(multirom_get_bootloader_cmdline(status, itr_o, 1024) == -1)
                {
                    ERROR("Failed to get cmdline\n");
                    goto fail;
                }
                break;
            }
            // root device. is either "UUID=..." (USB drive) or "/dev/mmcblk0p9" or "/dev/mmcblk0p10"
            case 'd':
            {
                if(data_part)
                {
                    // Only android's ueventd creates /dev/block, so try to remove it
                    // for _real_ linux OS. We can't use UUID, because it's the same for
                    // /system, /data and /cache partitions
                    char *blk = strstr(data_part->device, "/dev/block/");
                    if(blk)
                    {
                        strcpy(itr_o, "/dev/");
                        strcat(itr_o, blk+sizeof("/dev/block/")-1);
                    }
                    else
                        strcpy(itr_o, data_part->device);
                }
                else if(rom->partition)
                    sprintf(itr_o, "UUID=%s", rom->partition->uuid);
                else
                    ERROR("Failed to set root device\n");
                break;
            }
            // root fs type
            case 'r':
            {
                if(data_part)
                    strcpy(itr_o, data_part->type);
                else if(rom->partition)
                {
                    if(!strcmp(rom->partition->fs, "ntfs"))
                        strcpy(itr_o, "ntfs-3g");
                    else
                        strcpy(itr_o, rom->partition->fs);
                }
                else
                    ERROR("Failed to set root fs type\n");
                break;
            }
            // root directory, from root of the root device
            case 's':
            {
                char *d = map_get_val(i->str_vals, "root_dir");
                if(!d)
                {
                    ERROR("%%s alias found in cmdline, but root_dir key was not found!\n");
                    break;
                }
                sprintf(itr_o, "%s", d);
                break;
            }
            // root image, from root of the root device
            case 'i':
            {
                char *d = map_get_val(i->str_vals, "root_img");
                if(!d)
                {
                    ERROR("%%s alias found in cmdline, but root_img key was not found!\n");
                    break;
                }
                sprintf(itr_o, "%s", d);
                break;
            }
            // fs of the root image
            case 'f':
            {
                char *d = map_get_val(i->str_vals, "root_img_fs");
                if(!d)
                {
                    ERROR("%%s alias found in cmdline, but root_img_fs key was not found!\n");
                    break;
                }
                strcpy(itr_o, d);
                break;
            }
        }
        itr_o += strlen(itr_o);
        c = strcspn (++itr_i, "%");
    }

    free(*s);
    *s = realloc(buff, strlen(buff)+1);

    ERROR("Alias-replaced cmdline: %s\n", *s);
    return 0;

fail:
    free(buff);
    return -1;
}

// - %m - ROMs folder (eg. /sdcard/multirom/roms/*rom_name*)
int multirom_replace_aliases_root_path(char **s, struct multirom_rom *rom)
{
    char *alias = strstr(*s, "%m");
    if(!alias)
        return 0;

    char buff[256] = { 0 };
    memcpy(buff, *s, alias-*s);
    strcat(buff, rom->base_path+strlen(rom->partition ? rom->partition->mount_path : REALDATA));
    strcat(buff, alias+2);

    ERROR("Alias-replaced path: %s\n", buff);

    free(*s);
    *s = strdup(buff);
    return 0;
}

int multirom_extract_bytes(const char *dst, FILE *src, size_t size)
{
    FILE *f = fopen(dst, "w");
    if(!f)
    {
        ERROR("Failed to open dest file %s\n", dst);
        return -1;
    }

    char *buff = malloc(size);

    fread(buff, 1, size, src);
    fwrite(buff, 1, size, f);

    fclose(f);
    free(buff);
    return 0;
}

void multirom_destroy_partition(void *part)
{
    struct usb_partition *p = (struct usb_partition *)part;
    if(p->mount_path && p->keep_mounted == 0)
        umount(p->mount_path);

    free(p->name);
    free(p->uuid);
    free(p->mount_path);
    free(p->fs);
    free(p);
}

int multirom_update_partitions(struct multirom_status *s)
{
    pthread_mutex_lock(&parts_mutex);

    list_clear(&s->partitions, &multirom_destroy_partition);

    char *cmd[] = { busybox_path, "blkid", NULL };
    char *res = run_get_stdout(cmd);
    if(!res)
    {
        pthread_mutex_unlock(&parts_mutex);
        return -1;
    }

    char *tok;
    char *name;
    struct usb_partition *part;

    char *line = strtok(res, "\n");
    while(line != NULL)
    {
        if(strstr(line, "/dev/") != line)
        {
            ERROR("blkid line does not start with /dev/!\n");
            break;
        }

        tok = strrchr(line, '/')+1;
        name = strndup(tok, strchr(tok, ':') - tok);
        if(strstr(name, "mmcblk")) // ignore internal nand
        {
            free(name);
            goto next_itr;
        }

        part = mzalloc(sizeof(struct usb_partition));
        part->name = name;

        tok = strstr(line, "UUID=\"");
        if(tok)
        {
            tok += sizeof("UUID=\"")-1;
            part->uuid = strndup(tok, strchr(tok, '"') - tok);
        }
        else
        {
            ERROR("Part %s does not have UUID, line: \"%s\"\n", part->name, line);
            multirom_destroy_partition(part);
            goto next_itr;
        }

        tok = strstr(line, "TYPE=\"");
        if(tok)
        {
            tok += sizeof("TYPE=\"")-1;
            part->fs = strndup(tok, strchr(tok, '"') - tok);
        }

        if(part->fs && multirom_mount_usb(part) == 0)
        {
            list_add(part, &s->partitions);
            ERROR("Found part %s: %s, %s\n", part->name, part->uuid, part->fs);
        }
        else
        {
            ERROR("Failed to mount part %s %s, %s\n", part->name, part->uuid, part->fs);
            multirom_destroy_partition(part);
        }

next_itr:
        line = strtok(NULL, "\n");
    }
    pthread_mutex_unlock(&parts_mutex);
    free(res);

    return 0;
}

int multirom_mount_usb(struct usb_partition *part)
{
    mkdir("/mnt", 0777);

    char path[256];
    sprintf(path, "/mnt/%s", part->name);
    if(mkdir(path, 0777) != 0 && errno != EEXIST)
    {
        ERROR("Failed to create dir for mount %s\n", path);
        return -1;
    }

    char src[256];
    sprintf(src, "/dev/block/%s", part->name);

    if(strcmp(part->fs, "ntfs-3g") == 0)
    {
        char *cmd[] = { ntfs_path, src, path, NULL };
        if(run_cmd(cmd) != 0)
        {
            ERROR("Failed to mount %s with ntfs-3g\n", src);
            return -1;
        }
    }
    else if(strcmp(part->fs, "exfat") == 0)
    {
        char *cmd[] = { exfat_path, "-o", "big_writes,max_read=131072,max_write=131072,nonempty", src, path, NULL };
        if(run_cmd(cmd) != 0)
        {
            ERROR("Failed to mount %s with exfat\n", src);
            return -1;
        }
    }
    else if(mount(src, path, part->fs, MS_NOATIME, "") < 0)
    {
        ERROR("Failed to mount %s (%d: %s)\n", src, errno, strerror(errno));
        return -1;
    }

    part->mount_path = strdup(path);
    return 0;
}

void *multirom_usb_refresh_thread_work(void *status)
{
    uint32_t timer = 0;
    struct stat info;

    // stat.st_ctime is defined as unsigned long instead
    // of time_t in android
    unsigned long last_change = 0;

    while(run_usb_refresh)
    {
        if(timer <= 50)
        {
            if(stat("/dev/block", &info) >= 0 && info.st_ctime > last_change)
            {
                multirom_update_partitions((struct multirom_status*)status);

                if(usb_refresh_handler)
                    (*usb_refresh_handler)();

                last_change = info.st_ctime;
            }
            timer = 500;
        }
        else
            timer -= 50;
        usleep(50000);
    }
    return NULL;
}

void multirom_set_usb_refresh_thread(struct multirom_status *s, int run)
{
    if(run_usb_refresh == run)
        return;

    run_usb_refresh = run;
    if(run)
        pthread_create(&usb_refresh_thread, NULL, multirom_usb_refresh_thread_work, s);
    else
        pthread_join(usb_refresh_thread, NULL);
}

void multirom_set_usb_refresh_handler(void (*handler)(void))
{
    usb_refresh_handler = handler;
}

int multirom_mount_loop(const char *src, const char *dst, const char *fs, int flags, const void *data)
{
    int file_fd, device_fd, res = -1;

    file_fd = open(src, O_RDWR);
    if (file_fd < 0) {
        ERROR("Failed to open image %s\n", src);
        return -1;
    }

    static int loop_devs = 0;
    char path[64];
    sprintf(path, "/dev/loop%d", loop_devs);
    if(mknod(path, S_IFBLK | 0777, makedev(7, loop_devs)) < 0)
    {
        ERROR("Failed to create loop file (%d: %s)\n", errno, strerror(errno));
        goto close_file;
    }

    ++loop_devs;

    device_fd = open(path, O_RDWR);
    if (device_fd < 0)
    {
        ERROR("Failed to open loop file (%d: %s)\n", errno, strerror(errno));
        goto close_file;
    }

    if (ioctl(device_fd, LOOP_SET_FD, file_fd) < 0)
    {
        ERROR("ioctl LOOP_SET_FD failed on %s\n", path);
        goto close_dev;
    }

    if(mount(path, dst, fs, flags, data) < 0)
        ERROR("Failed to mount loop (%d: %s)\n", errno, strerror(errno));
    else
        res = 0;

close_dev:
    close(device_fd);
close_file:
    close(file_fd);
    return res;
}

char *multirom_get_klog(void)
{
    int len = klogctl(10, NULL, 0);
    if      (len < 16*1024)      len = 16*1024;
    else if (len > 16*1024*1024) len = 16*1024*1024;

    char *buff = malloc(len);
    klogctl(3, buff, len);
    if(len <= 0)
    {
        ERROR("Could not get klog!\n");
        free(buff);
        return NULL;
    }
    return buff;
}

int multirom_copy_log(char *klog)
{
    int res = 0;
    int freeLog = (klog == NULL);

    if(!klog)
        klog = multirom_get_klog();

    if(klog)
    {
        char path[256];
        sprintf(path, "%s/error.txt", multirom_dir);
        FILE *f = fopen(path, "w");
        if(f)
        {
            fwrite(klog, 1, strlen(klog), f);
            fclose(f);
            chmod(REALDATA"/media/multirom/error.txt", 0777);
        }
        else
        {
            ERROR("Failed to open %s!\n", path);
            res = -1;
        }
    }
    else
    {
        ERROR("Could not get klog!\n");
        res = -1;
    }

    if(freeLog)
        free(klog);
    return res;
}

struct usb_partition *multirom_get_partition(struct multirom_status *s, char *uuid)
{
    int i;
    for(i = 0; s->partitions && s->partitions[i]; ++i)
        if(strcmp(s->partitions[i]->uuid, uuid) == 0)
            return s->partitions[i];
    return NULL;
}

int multirom_search_last_kmsg(const char *expr)
{
    FILE *f = fopen("/proc/last_kmsg", "r");
    if(!f)
        return -1;

    int res = -1;
    char buff[2048];

    while(fgets(buff, sizeof(buff), f))
    {
        if(strstr(buff, expr))
        {
            res = 0;
            break;
        }
    }

    fclose(f);
    return res;
}

int multirom_get_battery(void)
{
    char buff[4];

    FILE *f = fopen(BATTERY_CAP, "r");
    if(!f)
        return -1;

    fgets(buff, sizeof(buff), f);
    fclose(f);

    return atoi(buff);
}

void multirom_set_brightness(int val)
{
#ifdef TW_BRIGHTNESS_PATH
    FILE *f = fopen(TW_BRIGHTNESS_PATH, "w");
    if(!f)
    {
        ERROR("Failed to set brightness: %s!\n", strerror(errno));
        return;
    }
    fprintf(f, "%d", val);
    fclose(f);
#endif
}

int multirom_run_scripts(const char *type, struct multirom_rom *rom)
{
    char buff[512];
    sprintf(buff, "%s/%s", rom->base_path, type);
    if(access(buff, (R_OK | X_OK)) < 0)
    {
        ERROR("No %s scripts for ROM %s\n", type, rom->name);
        return 0;
    }

    ERROR("Running %s scripts for ROM %s...\n", type, rom->name);

    char *cmd[] = { busybox_path, "sh", "-c", buff, NULL };
    sprintf(buff, "B=\"%s\"; P=\"%s\"; for x in $(\"$B\" ls \"$P/%s/\"*.sh); do echo Running script $x; \"$B\" sh $x \"$B\" \"$P\" || exit 1; done", busybox_path, rom->base_path, type);

    int res = run_cmd(cmd);
    if(res != 0)
    {
        ERROR("Error running scripts (%d)!\n", res);
        return res;
    }
    return 0;
}

#define RD_GZIP 1
#define RD_LZ4  2
int multirom_update_rd_trampoline(const char *path)
{
    int result = -1;
    uint32_t magic = 0;

    FILE *f = fopen(path, "r");
    if(!f)
    {
        ERROR("Couldn't open %s!\n", path);
        return -1;
    }
    fread(&magic, sizeof(magic), 1, f);
    fclose(f);

    remove_dir("/mrom_rd");
    mkdir("/mrom_rd", 0755);

    // Decompress initrd
    int type;
    char buff[256];
    char *cmd[] = { busybox_path, "sh", "-c", buff, NULL };

    if((magic & 0xFFFF) == 0x8B1F)
    {
        type = RD_GZIP;
        snprintf(buff, sizeof(buff), "B=\"%s\"; cd /mrom_rd; \"$B\" gzip -d -c \"%s\" | \"$B\" cpio -i", busybox_path, path);
    }
    else if(magic == 0x184C2102)
    {
        type = RD_LZ4;
        snprintf(buff, sizeof(buff), "cd /mrom_rd; \"%s/lz4\" -d \"%s\" stdout | \"%s\" cpio -i", multirom_dir, path, busybox_path);
    }
    else
    {
        ERROR("Unknown ramdisk magic 0x%08X, can't update trampoline\n", magic);
        goto success;
    }

    int r = run_cmd(cmd);
    if(r != 0)
    {
        ERROR("Failed to unpack ramdisk!\n");
        goto fail;
    }

    if(access("/mrom_rd/init", F_OK) < 0 || access("/mrom_rd/main_init", F_OK) < 0)
    {
        ERROR("This ramdisk is not injected, skipping\n");
        goto success;
    }

    // Check version
    char *cmd_t[] = { "/mrom_rd/init", "-v", NULL };
    char *res = run_get_stdout(cmd_t);
    int ver = 0;
    if(res)
    {
        ver = atoi(res);
        free(res);
    }
    else
    {
        ERROR("Failed to run trampoline!\n");
        goto fail;
    }

    if(ver >= multirom_get_trampoline_ver())
    {
        INFO("No need to update trampoline for this rd\n");
        goto success;
    }

    INFO("Updating trampoline in %s from ver %d to %d\n", path, ver, multirom_get_trampoline_ver());

    if(copy_file("/init", "/mrom_rd/init") < 0)
    {
        ERROR("Failed to update trampoline\n");
        goto fail;
    }
    chmod("/mrom_rd/init", 0755);

    // Pack initrd again
    switch(type)
    {
        case RD_GZIP:
            snprintf(buff, sizeof(buff), "B=\"%s\"; cd /mrom_rd; \"$B\" find . | \"$B\" cpio -o -H newc | \"$B\" gzip > \"%s\"", busybox_path, path);
            break;
        case RD_LZ4:
            snprintf(buff, sizeof(buff), "B=\"%s\"; cd /mrom_rd; \"$B\" find . | \"$B\" cpio -o -H newc | \"%s/lz4\" stdin \"%s\"", busybox_path, multirom_dir, path);
            break;
    }

    r = run_cmd(cmd);
    if(r != 0)
    {
        ERROR("Failed to pack ramdisk!\n");
        goto fail;
    }

success:
    result = 0;
fail:
    remove_dir("/mrom_rd");
    return result;
}
