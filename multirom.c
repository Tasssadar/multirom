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

#include "multirom.h"
#include "multirom_ui.h"
#include "framebuffer.h"
#include "input.h"
#include "log.h"
#include "util.h"

#define REALDATA "/realdata"
#define BUSYBOX_BIN "busybox"
#define INTERNAL_ROM_NAME "Internal"
#define BOOT_BLK "/dev/block/mmcblk0p2"
#define IN_ROOT "is_in_root"
#define MAX_ROM_NAME_LEN 26

#define T_FOLDER 4

static char multirom_dir[64] = { 0 };
static char busybox_path[128] = { 0 };

int multirom_find_base_dir(void)
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

        strcpy(multirom_dir, paths[i]);
        sprintf(busybox_path, "%s/%s", paths[i], BUSYBOX_BIN);
        return 0;
    }
    return -1;
}

int multirom(void)
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

    if(s.is_second_boot == 0)
    {
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
    else
    {
        ERROR("Skipping ROM selection beacause of is_second_boot==1");
        s.is_second_boot = 0;
        to_boot = s.current_rom;
    }

    if(to_boot)
    {
        exit = multirom_prepare_for_boot(&s, to_boot);

        // Something went wrong, reboot
        if(exit == -1)
        {
            multirom_emergency_reboot();
            return EXIT_REBOOT;
        }

        s.current_rom = to_boot;
        s.is_second_boot = (exit & EXIT_REBOOT) ? 1 : 0;
    }

    multirom_save_status(&s);
    multirom_free_status(&s);

    sync();

    return exit;
}

void multirom_emergency_reboot(void)
{
    if(multirom_init_fb() < 0)
    {
        ERROR("Failed to init framebuffer in emergency reboot");
        return;
    }

    fb_add_text(0, 150, WHITE, SIZE_NORMAL, 
                "An error occured.\nShutting down MultiROM to avoid data corruption.\n"
                "Report this error to the developer!\nDebug info: /sdcard/multirom/error.txt\n\n"
                "Press POWER button to reboot.");

    fb_draw();
    fb_clear();
    fb_close();

    // dump klog
    int len = klogctl(10, NULL, 0);
    if      (len < 16*1024)      len = 16*1024;
    else if (len > 16*1024*1024) len = 16*1024*1024;

    char *buff = malloc(len);
    klogctl(3, buff, len);
    if(len > 0)
    {
        FILE *f = fopen(REALDATA"/media/multirom/error.txt", "w");
        if(f)
        {
            fwrite(buff, 1, len, f);
            fclose(f);
            chmod(REALDATA"/media/multirom/error.txt", 0777);
        }
    }
    free(buff);

    // Wait for power key
    start_input_thread();
    while(wait_for_key() != KEY_POWER);
    stop_input_thread();
}

static int compare_rom_names(const void *a, const void *b)
{
    struct multirom_rom *rom_a = *((struct multirom_rom **)a);
    struct multirom_rom *rom_b = *((struct multirom_rom **)b);
    return strcoll(rom_a->name, rom_b->name);
}

int multirom_default_status(struct multirom_status *s)
{
    s->is_second_boot = 0;
    s->current_rom = NULL;
    s->roms = NULL;

    char roms_path[256];
    sprintf(roms_path, "%s/roms", multirom_dir);
    DIR *d = opendir(roms_path);
    if(!d)
    {
        fb_debug("failed to open roms folder, creating one with ROM from internal memory...\n");
        if(multirom_import_internal() == -1)
            return -1;

        d = opendir(roms_path);
        if(!d)
        {
            fb_debug("Failed to open roms folder, for second time!\n");
            return -1;
        }
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

        fb_debug("Adding ROM %s\n", dr->d_name);

        struct multirom_rom *rom = malloc(sizeof(struct multirom_rom));
        memset(rom, 0, sizeof(struct multirom_rom));

        rom->id = multirom_generate_rom_id();
        rom->name = malloc(strlen(dr->d_name)+1);
        strcpy(rom->name, dr->d_name);

        rom->type = multirom_get_rom_type(rom);

        sprintf(path, "%s/%s/%s", roms_path, rom->name, IN_ROOT);
        rom->is_in_root = access(path, R_OK) == 0 ? 1 : 0;

        rom->has_bootimg = multirom_get_rom_bootid(rom, roms_path) == 0 ? 1 : 0;
        if(!rom->has_bootimg && (rom->type == ROM_UBUNTU_INTERNAL || rom->type == ROM_DEFAULT))
        {
            fb_debug("Rom %s is type %d, but has no boot image - skipping!\n", rom->name, rom->type);
            free(rom->name);
            free(rom);
            continue;
        }

        list_add(rom, &add_roms);
    }

    closedir(d);

    if(add_roms)
    {
        // sort roms
        qsort(add_roms, list_item_count(add_roms), sizeof(struct multirom_rom*), compare_rom_names);

        //add them to main list
        int i;
        for(i = 0; add_roms[i]; ++i)
            list_add(add_roms[i], &s->roms);
        list_clear(&add_roms, NULL);
    }

    s->current_rom = multirom_get_rom(s, INTERNAL_ROM_NAME);
    if(!s->current_rom)
    {
        fb_debug("No internal rom found!\n");
        return -1;
    }
    return 0;
}

int multirom_load_status(struct multirom_status *s)
{
    fb_debug("Loading MultiROM status...\n");

    multirom_default_status(s);

    char arg[256];
    sprintf(arg, "%s/multirom.ini", multirom_dir);

    FILE *f = fopen(arg, "r");
    if(!f)
    {
        fb_debug("Failed to open config file, using defaults!\n");
        return -1;
    }

    char current_rom[256] = { 0 };
    char auto_boot_rom[256] = { 0 };

    char line[512];
    char name[64];
    char *pch;

    while((fgets(line, sizeof(line), f)))
    {
        pch = strtok (line, "=\n");
        if(!pch) continue;
        strcpy(name, pch);
        pch = strtok (NULL, "=\n");
        if(!pch) continue;
        strcpy(arg, pch);

        if(strstr(name, "is_second_boot"))
            s->is_second_boot = atoi(arg);
        else if(strstr(name, "current_rom"))
            strcpy(current_rom, arg);
        else if(strstr(name, "auto_boot_seconds"))
            s->auto_boot_seconds = atoi(arg);
        else if(strstr(name, "auto_boot_rom"))
            strcpy(auto_boot_rom, arg);
    }

    fclose(f);

    s->current_rom = multirom_get_rom(s, current_rom);
    if(!s->current_rom)
    {
        fb_debug("Failed to select current rom (%s), using Internal!\n", current_rom);
        s->current_rom = multirom_get_rom(s, INTERNAL_ROM_NAME);
        if(!s->current_rom)
        {
            fb_debug("No internal rom found!\n");
            return -1;
        }
    }

    s->auto_boot_rom = multirom_get_rom(s, auto_boot_rom);
    if(!s->auto_boot_rom)
        ERROR("Could not find rom %s to auto-boot", auto_boot_rom);

    return 0;
}

int multirom_save_status(struct multirom_status *s)
{
    fb_debug("Saving multirom status\n");

    char path[256];
    sprintf(path, "%s/multirom.ini", multirom_dir);

    FILE *f = fopen(path, "w");
    if(!f)
    {
        fb_debug("Failed to open/create status file!\n");
        return -1;
    }

    fprintf(f, "is_second_boot=%d\n", s->is_second_boot);
    fprintf(f, "current_rom=%s\n", s->current_rom ? s->current_rom->name : INTERNAL_ROM_NAME);
    fprintf(f, "auto_boot_seconds=%d\n", s->auto_boot_seconds);
    fprintf(f, "auto_boot_rom=%s\n", s->auto_boot_rom ? s->auto_boot_rom->name : "");

    fclose(f);
    return 0;
}

void multirom_find_usb_roms(struct multirom_status *s)
{
    // remove USB roms
    int i;
    for(i = 0; s->roms && s->roms[i];)
    {
        if(M(s->roms[i]->type) & MASK_USB_ROMS)
        {
            list_rm(s->roms[i], &s->roms, &multirom_free_rom);
            i = 0;
        }
        else ++i;
    }

    // TODO
}

int multirom_get_rom_type(struct multirom_rom *rom)
{
    if(strcmp(rom->name, INTERNAL_ROM_NAME) == 0)
        return ROM_DEFAULT;

#define FOLDERS 2
    static const char *folders[][FOLDERS] = 
    {
         { "system", "data" },
         { "root", NULL },
    };
    static const int types[] = { ROM_ANDROID_INTERNAL, ROM_UBUNTU_INTERNAL };

    char path[256];
    uint32_t i, y, okay;
    for(i = 0; i < ARRAY_SIZE(folders); ++i)
    {
        okay = 1;
        for(y = 0; folders[i][y] && y < FOLDERS && okay; ++y)
        {
            sprintf(path, "%s/roms/%s/%s/", multirom_dir, rom->name, folders[i][y]);
            if(access(path, R_OK) < 0)
                okay = 0;
        }
        if(okay)
            return types[i];
    }
    return ROM_UNKNOWN;
}

int multirom_get_rom_bootid(struct multirom_rom *rom, const char *roms_root_path)
{
    char path[256];
    sprintf(path, "%s/%s/boot.img", roms_root_path, rom->name);

    boot_img_hdr header;
    if(multirom_load_bootimg_header(path, &header) == -1)
        return -1;

    memcpy(rom->boot_image_id, header.id, sizeof(rom->boot_image_id));

    fb_debug("Got ROM's boot id:\n");
    int i = 0; 
    for(; i < 8; ++i)
        fb_debug("%X ", rom->boot_image_id[i]);
    fb_debug("\n");
    return 0;
}

int multirom_load_bootimg_header(const char *path, struct boot_img_hdr *header)
{
    FILE *boot_img = fopen(path, "r");
    if(!boot_img)
    {
        fb_debug("Failed to open boot image at %s\n",  path);
        return -1;
    }

    fread(header->magic, 1, sizeof(struct boot_img_hdr), boot_img);
    fclose(boot_img);

    return 0;
}

int multirom_import_internal(void)
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

    // boot image
    sprintf(path, "%s/roms/%s/boot.img", multirom_dir, INTERNAL_ROM_NAME);
    int res = multirom_dump_boot(path);

    // is_in_root file
    sprintf(path, "%s/roms/%s/%s", multirom_dir, INTERNAL_ROM_NAME, IN_ROOT);
    FILE *f = fopen(path, "w");
    if(f)
        fclose(f);
    return res;
}

int multirom_dump_boot(const char *dest)
{
    fb_debug("Dumping boot image...");

    //              0            1     2             3
    char *cmd[] = { busybox_path, "dd", "if="BOOT_BLK, NULL, NULL };
    cmd[3] = malloc(256);
    sprintf(cmd[3], "of=%s", dest);

    int res = run_cmd(cmd);
    free(cmd[3]);

    fb_debug("done, result: %d\n", res);
    return res;
}

struct multirom_rom *multirom_get_rom(struct multirom_status *s, const char *name)
{
    int i = 0;
    for(; s->roms && s->roms[i]; ++i)
        if(strcmp(s->roms[i]->name, name) == 0)
            return s->roms[i];

    return NULL;
}

struct multirom_rom *multirom_get_rom_in_root(struct multirom_status *s)
{
    int i = 0;
    for(; s->roms && s->roms[i]; ++i)
        if(s->roms[i]->is_in_root)
            return s->roms[i];

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

void multirom_dump_status(struct multirom_status *s)
{
    fb_debug("Dumping multirom status:\n");
    fb_debug("  is_second_boot=%d\n", s->is_second_boot);
    fb_debug("  current_rom=%s\n", s->current_rom ? s->current_rom->name : "NULL");
    fb_debug("  auto_boot_seconds=%d\n", s->auto_boot_seconds);
    fb_debug("  auto_boot_rom=%s\n", s->auto_boot_rom ? s->auto_boot_rom->name : "NULL");
    fb_debug("\n");

    int i, y;
    for(i = 0; s->roms && s->roms[i]; ++i)
    {
        fb_debug("  ROM: %s\n", s->roms[i]->name);
        fb_debug("    type: %d\n", s->roms[i]->type);
        fb_debug("    is_in_root: %d\n", s->roms[i]->is_in_root);
        fb_debug("    has_bootimg: %d\n", s->roms[i]->has_bootimg);
        fb_debug("    bootid: ");

        for(y = 0; y < 8; ++y)
            fb_debug("%X-", s->roms[i]->boot_image_id[y]);
        fb_debug("\n");
    }
}

struct multirom_rom *multirom_select_rom(struct multirom_status *s)
{
    fb_printf("\nChoose ROM to boot with volume up/down keys and then select with power button:\n");

    int i, y, current = -1;
    for(i = 0; s->roms && s->roms[i]; ++i)
    {
        if(s->roms[i]->type == ROM_UNKNOWN)
        {
            ERROR("Skipping ROM %s in selection because it has unknown type\n", s->roms[i]->name);
            continue;
        }

        fb_printf("  %d) %s", i, s->roms[i]->name);
        if(s->roms[i] == s->current_rom)
        {
            fb_printf(" (Current)\n");
            current = i;
        }
        else
            fb_printf("\n");
    }

    int reboot = i++;
    fb_printf("\n  %d) Reboot\n", reboot);

    start_input_thread();

    int user_choice = -1;
    int timer = 5000;

    if(current == -1)
    {
        current = 0;
        timer = -1;
    }

    while(user_choice == -1)
    {
        while((y = get_last_key()) != -1)
        {
            switch(y)
            {
                case KEY_VOLUMEDOWN:
                    if(++current >= i)
                        current = 0;
                    break;
                case KEY_VOLUMEUP:
                    if(--current < 0)
                        current = i-1;
                    break;
                case KEY_POWER:
                    user_choice = current;
                    break;
                default:
                    continue;
            }
            timer = -1;
            fb_printf("\rSelected ROM: %d                    ", current);
        }

        if(timer > 0)
        {
            timer -= 10;
            if(timer <= 0)
            {
                user_choice = current;
                fb_printf("\rAuto booting rom %d                   \n", current);
                break;
            }
            else if((timer+10)/1000 != timer/1000)
                fb_printf("\rSelected ROM: %d (auto-boot in %ds)", current, timer/1000);
        }
        usleep(10000);
    }
    stop_input_thread();

    if(user_choice == reboot)
    {
        fb_printf("\rRebooting...                         \n");
        return NULL;
    }
    fb_printf("\nROM \"%s\" selected\n", s->roms[user_choice]->name);
    return s->roms[user_choice];
}

int multirom_prepare_for_boot(struct multirom_status *s, struct multirom_rom *to_boot)
{
    int exit = EXIT_UMOUNT;

    int res = multirom_check_bootimg(s, to_boot);
    if(res == -1)
        return -1;

    if(res == 1)
        exit |= EXIT_REBOOT;

    if(to_boot == s->current_rom)
        fb_debug("To-boot rom is the same as previous rom.\n");

    int type_now = s->current_rom->type;
    int type_to = to_boot->type;

    // move root if needed
    if (!to_boot->is_in_root &&
        (to_boot->type == ROM_UBUNTU_INTERNAL || to_boot->type == ROM_DEFAULT))
    {
        struct multirom_rom *in_root = multirom_get_rom_in_root(s);
        if(!in_root)
        {
            ERROR("No rom in root!");
            return -1;
        }

        if (multirom_move_out_of_root(in_root) == -1 ||
            multirom_move_to_root(to_boot) == -1)
            return -1;
    }

    // fix ubuntu ramdisk permissions
    switch(type_to)
    {
        case ROM_UBUNTU_INTERNAL:
            multirom_fix_ubuntu_permissions();
            break;
        case ROM_ANDROID_INTERNAL:
        {
            if(!(exit & EXIT_REBOOT))
                exit &= ~(EXIT_UMOUNT);

            if(multirom_prep_android_mounts(to_boot) == -1)
                return -1;

            if(multirom_create_media_link() == -1)
                return -1;
            break;
        }
    }

    return exit;
}

int multirom_check_bootimg(struct multirom_status *s, struct multirom_rom *rom)
{
    if(rom->has_bootimg == 0)
    {
        if(rom->type == ROM_ANDROID_INTERNAL && (rom = multirom_get_rom(s, INTERNAL_ROM_NAME)))
            fb_debug("This ROM does not have boot image, using image from internal rom...\n");
        else
        {
            fb_debug("Skipping boot image check, ROM %s does not have boot img.\n", rom->name);
            return 0;
        }
    }

    char path[256];
    boot_img_hdr h_mem;
    boot_img_hdr h_rom;
    if(multirom_load_bootimg_header(BOOT_BLK, &h_mem) != 0)
        return -1;

    sprintf(path, "%s/roms/%s/boot.img", multirom_dir, rom->name);
    if(multirom_load_bootimg_header(path, &h_rom) != 0)
        return -1;

    int i;
    int id_matches = 1;
    for(i = 0; id_matches && i < 8; ++i)
        id_matches = (int)(h_mem.id[i] == h_rom.id[i]);

    if(id_matches == 1)
    {
        fb_debug("Boot image id's are the same, leaving boot as-is.\n");
        return 0;
    }
    else
    {
        if(h_mem.ramdisk_size == h_rom.ramdisk_size)
        {
            fb_debug("Boot image id's does not match, but ramdisk_size is the same.\n");
            fb_debug("Assuming kernel update, dumping boot image to folder of rom %s\n", rom->name);

            sprintf(path, "%s/roms/%s/boot.img", multirom_dir, rom->name);
            multirom_dump_boot(path);
            return 0;
        }
        else
        {
            fb_debug("Boot image id's does not match and ramdisk_size differs.\n");
            fb_debug("Flashing my boot image..");

            //              0            1     2         3     4
            char *cmd[] = { busybox_path, "dd", "bs=4096", NULL, "of="BOOT_BLK, NULL };

            cmd[3] = malloc(256);
            sprintf(cmd[3], "if=%s/roms/%s/boot.img", multirom_dir, rom->name);
            int res = run_cmd(cmd);
            free(cmd[3]);

            fb_debug("done, res: %d\n", res);
            return 1;
        }
    }
    return 0;
}

int multirom_move_out_of_root(struct multirom_rom *rom)
{
    fb_debug("Moving ROM %s out of root...\n", rom->name);

    char path_to[256];
    sprintf(path_to, "%s/roms/%s/root/", multirom_dir, rom->name);

    mkdir(path_to, 0777);

    DIR *d = opendir(REALDATA);
    if(!d)
    {
        fb_debug("Failed to open /realdata!\n");
        return -1;
    }

    //              0           1     2            3
    char *cmd[] = { busybox_path, "mv", malloc(256), path_to, NULL };
    struct dirent *dr;
    while((dr = readdir(d)))
    {
        if(dr->d_name[0] == '.' && (dr->d_name[1] == '.' || dr->d_name[1] == 0))
            continue;

        if(strcmp(dr->d_name, "media") == 0)
            continue;

        sprintf(cmd[2], REALDATA"/%s", dr->d_name);
        int res = run_cmd(cmd);
        if(res != 0)
        {
            fb_debug("Move failed %d\n%s\n%s\n%s\n%s\n", res, cmd[0], cmd[1], cmd[2], cmd[3]);
            free(cmd[2]);
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    free(cmd[2]);

    sprintf(path_to, "%s/roms/%s/%s", multirom_dir, rom->name, IN_ROOT);
    unlink(path_to);

    return 0;
}

int multirom_move_to_root(struct multirom_rom *rom)
{
    fb_debug("Moving ROM %s to root...\n", rom->name);

    char path_from[256];
    sprintf(path_from, "%s/roms/%s/root/", multirom_dir, rom->name);

    DIR *d = opendir(path_from);
    if(!d)
    {
        fb_debug("Failed to open %s!\n", path_from);
        return -1;
    }

    //              0           1     2            3
    char *cmd[] = { busybox_path, "mv", malloc(256), "/realdata/", NULL };
    struct dirent *dr;
    while((dr = readdir(d)))
    {
        if(dr->d_name[0] == '.' && (dr->d_name[1] == '.' || dr->d_name[1] == 0))
            continue;

        if(strcmp(dr->d_name, "media") == 0)
            continue;

        sprintf(cmd[2], "%s%s", path_from, dr->d_name);
        int res = run_cmd(cmd);
        if(res != 0)
        {
            fb_debug("Move failed %d\n%s\n%s\n%s\n%s\n", res, cmd[0], cmd[1], cmd[2], cmd[3]);
            free(cmd[2]);
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    free(cmd[2]);
    sprintf(path_from, "%s/roms/%s/%s", multirom_dir, rom->name, IN_ROOT);
    FILE *f = fopen(path_from, "w");
    if(f)
        fclose(f);

    return 0;
}

void multirom_free_status(struct multirom_status *s)
{
    list_clear(&s->roms, &multirom_free_rom);
}

void multirom_free_rom(void *rom)
{
    free(((struct multirom_rom*)rom)->name);
    free(rom);
}

int multirom_init_fb(void)
{
    vt_set_mode(1);

    if(fb_open() < 0)
    {
        ERROR("Failed to open framebuffer!");
        return -1;
    }

    fb_fill(BLACK);
    return 0;
}

#define EXEC_MASK (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP)

int multirom_prep_android_mounts(struct multirom_rom *rom)
{
    char in[128];
    char out[128];
    char folder[256];
    sprintf(folder, "%s/roms/%s/boot", multirom_dir, rom->name);

    DIR *d = opendir(folder);
    if(!d)
    {
        ERROR("Failed to open rom folder %s", folder);
        return -1;
    }

    struct dirent *dp = NULL;

    char line[1024];
    FILE *f_in = NULL;
    FILE *f_out = NULL;
    int add_dummy = 0;

    while((dp = readdir(d)))
    {
        sprintf(in, "%s/%s", folder, dp->d_name);
        sprintf(out, "/%s", dp->d_name);

        // just copy the file if not rc
        if(!strstr(dp->d_name, ".rc"))
        {
            copy_file(in, out);
            continue;
        }

        f_in = fopen(in, "r");
        if(!f_in)
            continue;

        f_out = fopen(out, "w");
        if(!f_out)
        {
            fclose(f_in);
            continue;
        }

        while((fgets(line, sizeof(line), f_in)))
        {
            if (strstr(line, "on "))
                add_dummy = 1;
            // Remove mounts from RCs
            else if (strstr(line, "mount_all") || 
               (strstr(line, "mount ") && (strstr(line, "/data") || strstr(line, "/system"))))
            {
                if(add_dummy == 1)
                {
                    add_dummy = 0;
                    fputs("    export DUMMY_LINE_INGORE_IT 1\n", f_out);
                }
                fputc((int)'#', f_out);
            }
            // fixup sdcard tool
            else if(strstr(line, "service sdcard") == line)
            {
                // not needed, now I use bind insead of link
                // so just put the line as is
                fputs(line, f_out);
                /*char *p = strtok(line, " ");
                while(p)
                {
                    if(strcmp(p, "/data/media") == 0)
                        fputs("/realdata/media", f_out);
                    else
                        fputs(p, f_out);

                    if((p = strtok(NULL, " ")))
                        fputc((int)' ', f_out);
                }*/

                // put it to main class and skip next line,
                // it does not start on CM10 when it is in late_start, wtf?
                fputs("    class main\n", f_out);
                fgets(line, sizeof(line), f_in); // skip next line, should be "class late_start"
                continue;
            }
            fputs(line, f_out);
        }

        fclose(f_out);
        fclose(f_in);

        chmod(out, EXEC_MASK);
    }
    closedir(d);

    mkdir_with_perms("/system", 0755, NULL, NULL);
    mkdir_with_perms("/data", 0771, "system", "system");
    mkdir_with_perms("/cache", 0770, "system", "cache");

    static const char *folders[] = { "system", "data", "cache" };
    unsigned long flags[] = { MS_BIND | MS_RDONLY, MS_BIND, MS_BIND };
    uint32_t i;
    char from[256];
    char to[256];
    for(i = 0; i < ARRAY_SIZE(folders); ++i)
    {
        sprintf(from, "%s/roms/%s/%s", multirom_dir, rom->name, folders[i]);
        sprintf(to, "/%s", folders[i]);

        if(mount(from, to, "ext4", flags[i], "") < 0)
        {
            ERROR("Failed to mount %s to %s (%d: %s)", from, to, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
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

void multirom_fix_ubuntu_permissions(void)
{
    fb_debug("Fixing ubuntu ramdisk permissions...\n");

    chmod("/etc", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/ld.so.cache", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/modprobe.d/blacklist-framebuffer.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist-watchdog.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/iwlwifi.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist-ath_pci.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/alsa-base.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist-rare-network.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist-modem.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/modprobe.d/blacklist-firewire.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/console-setup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/console-setup/Uni2-Fixed16.psf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/console-setup/cached.kmap.gz", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/ld.so.conf.d", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/ld.so.conf.d/libc.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/ld.so.conf.d/arm-linux-gnueabihf.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/casper.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/udev", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/udev/udev.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/default", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/etc/default/console-setup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/default/keyboard", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/etc/ld.so.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/main_init", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/libply.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/ld-linux-armhf.so.3", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/brltty", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/brltty/brltty.sh", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/arm-linux-gnueabihf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/arm-linux-gnueabihf/libcom_err.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/librt.so.1", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libc.so.6", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/arm-linux-gnueabihf/libuuid.so.1", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libblkid.so.1", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libe2p.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libext2fs.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libudev.so.0", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libpthread.so.0", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/arm-linux-gnueabihf/libselinux.so.1", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libntfs-3g.so.835", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libdl.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libfuse.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/arm-linux-gnueabihf/libgcc_s.so.1", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/ata_id", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/firmware", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/cdrom_id", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/rules.d", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/rules.d/60-cdrom_id.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/rules.d/80-drivers.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/rules.d/50-udev-default.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/rules.d/50-firmware.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/rules.d/95-udev-late.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/rules.d/60-persistent-storage.rules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/udev/blkid", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/udev/scsi_id", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/casper", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/libply-boot-client.so.2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/klibc-6nLb5AjYTRZ6C8D-jIJ18a17wug.so", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/modules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/modules/3.1.10-6-nexus7", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.dep", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.softdep", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.order", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.dep.bin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.devname", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.symbols", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.alias.bin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.symbols.bin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/lib/modules/3.1.10-6-nexus7/modules.alias", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/sbin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/mount.ntfs", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/brltty-setup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/wait-for-root", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/mount.ntfs-3g", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/dumpe2fs", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/mount.cifs", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH | S_ISUID));
    chmod("/sbin/mount.fuse", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/losetup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/rmmod", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/udevadm", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/modprobe", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/udevd", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/blkid", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/sbin/hwclock", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/nfs-top", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/nfs-top/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/nfs-top/udev", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-premount", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-premount/resume", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-premount/fixrtc", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-premount/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/local-premount/ntfs_3g", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-premount/tarball-installer", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-functions", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/panic", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/panic/console_setup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/panic/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/panic/keymap", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/console_setup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/init-top/all_generic_ide", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/framebuffer", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/blacklist", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/brltty", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/udev", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-top/keymap", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-bottom", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-bottom/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/init-bottom/udev", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/12fstab", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/36disable_trackerd", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/40install_driver_updates", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/30accessibility", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/20xconfig", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/26serialtty", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/22desktop_settings", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/43disable_updateinitramfs", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/25adduser", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/45jackd2", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/35fix_language_selector", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/24preseed", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/31disable_update_notifier", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/22sslcert", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/23networking", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/casper-bottom/48kubuntu_disable_restart_notifications", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/25configure_init", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/07remove_oem_config", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/15autologin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/19keyboard", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/05mountpoints", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/16gdmnopasswd", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/13swap", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/49kubuntu_mobile_session", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/34disable_kde_services", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/18hostname", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/32disable_hibernation", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/50ubiquity-bluetooth-agent", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/23etc_modules", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/44pk_allow_ubuntu", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/41apt_cdrom", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/01integrity_check", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/14locales", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-bottom/33enable_apport_crashes", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/nfs", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/casper-premount", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-premount/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/casper-premount/10driver_updates", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper-helpers", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/init-premount", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/init-premount/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/init-premount/brltty", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/casper", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/local-bottom", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local-bottom/ORDER", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/local-bottom/ntfs_3g", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/scripts/local", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/scripts/functions", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/conf/conf.d", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/conf/uuid.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/conf/arch.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/conf/initramfs.conf", (S_IRWXU | S_IRUSR | S_IWUSR | S_IRWXG | S_IRGRP | S_IRWXO | S_IROTH));
    chmod("/run", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/dd", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/reboot", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/kbd_mode", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/resume", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/tar", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/insmod", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/pivot_root", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/loadkeys", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/setfont", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/nfsmount", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/date", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/mount", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/poweroff", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/casper-set-selections", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/ipconfig", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/fstype", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/eject", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/losetup", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/run-init", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/sleep", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/busybox", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/cpio", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/halt", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/casper-preseed", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/ntfs-3g", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/casper-reconfigure", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/casper-md5check", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/sh", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
    chmod("/bin/dmesg", (S_IRWXU | S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRGRP | S_IXGRP | S_IRWXO | S_IROTH | S_IXOTH));
}
