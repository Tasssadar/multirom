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
#include "tetris.h"
#include "keywords.h"
#include "iso_font.h"

uint8_t bootmgr_selected = 0;
volatile uint8_t bootmgr_input_run = 1;
volatile uint8_t bootmgr_time_run = 1;
int bootmgr_key_queue[10];
int8_t bootmgr_key_itr = 10;
static pthread_mutex_t bootmgr_input_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t bootmgr_draw_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char* bootmgr_bg0 = "/bmgr_imgs/init_0.rle";
static const char* bootmgr_bg1 = "/bmgr_imgs/init_1.rle";
static const char* bootmgr_img_folder = "/bmgr_imgs/%s";
uint8_t bootmgr_phase = BOOTMGR_MAIN;
uint8_t total_backups = 0;
char *backups[BOOTMGR_BACKUPS_MAX];
uint8_t backups_loaded = 0;
uint8_t backups_has_active = 0;
int8_t selected = -1;

bootmgr_settings_t settings;
uint8_t ums_enabled = 0;

struct FB fb;
struct stat s0, s1;
int fd0, fd1;
unsigned max_fb_size;

bootmgr_display_t *bootmgr_display = NULL;
pthread_t t_time;

inline void __bootmgr_boot()
{
    bootmgr_printf(-1, 20, WHITE, "Booting from %s...", bootmgr_selected ? "SD-card" : "internal memory");
    bootmgr_draw();

    bootmgr_set_time_thread(0);

    bootmgr_destroy_display();
    bootmgr_input_run = 0;
}

void bootmgr_start()
{
    bootmgr_load_settings();
    bootmgr_init_display();

    int key = 0;
    int8_t last_selected = -1;
    uint8_t key_pressed = (settings.timeout_seconds == -1);
    int16_t timer = settings.timeout_seconds*10;

    pthread_t t_input;
    pthread_create(&t_input, NULL, bootmgr_input_thread, NULL);
    bootmgr_set_time_thread(1);

    while(1)
    {
        if(last_selected != bootmgr_selected)
        {
            bootmgr_draw();
            last_selected = bootmgr_selected;
        }

        key = bootmgr_get_last_key();
        if(key != -1)
        {
            if(!key_pressed)
            {
                bootmgr_erase_text(20);
                bootmgr_draw();
                key_pressed = 1;
            }

            if(bootmgr_handle_key(key))
                return;
        }

        usleep(100000);
        if(!key_pressed)
        {
            if(timer%10 == 0)
            {
                bootmgr_printf(-1, 20, WHITE, "Boot from internal mem in %us", timer/10);
                bootmgr_draw();
            }

            if(--timer <= 0)
            {
                __bootmgr_boot();
                return;
            }
        }
    }
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
            if(hours >= 24)    hours -= 24;
            else if(hours < 0) hours = 24 + hours;

            if(settings.show_seconds)
                bootmgr_printf(0, 0, WHITE, "%2u:%02u:%02u    Battery: %s%%, %s", hours, tm%3600/60, tm%60, &pct, &status);
            else
                bootmgr_printf(0, 0, WHITE, "%2u:%02u         Battery: %s%%, %s", hours, tm%3600/60, &pct, &status);
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
                case KEY_VOLUMEUP:
                    bootmgr_selected = !bootmgr_selected;
                    break;
                case KEY_BACK:
                    bootmgr_printf(-1, 20, WHITE, "Rebooting...");
                    bootmgr_draw();
                case KEY_POWER:
                    bootmgr_close_framebuffer();
                    bootmgr_input_run = 0;
                    reboot(key == KEY_POWER ? RB_POWER_OFF : RB_AUTOBOOT);
                    return 1;
                case KEY_MENU:
                    if(bootmgr_selected)
                    {
                        bootmgr_set_time_thread(0);
                        bootmgr_phase = BOOTMGR_SD_SEL;
                        bootmgr_display->bg_img = 0;
                        bootmgr_printf(-1, 20, WHITE, "Mounting sd-ext...");
                        bootmgr_draw();
                        bootmgr_show_rom_list();
                        while(bootmgr_get_last_key() != -1); // clear key queue
                        if(!total_backups && backups_has_active)
                            return bootmgr_boot_sd();
                        bootmgr_draw();
                        break;
                    }
                    else
                        __bootmgr_boot();
                    return 1;
                case KEY_HOME:
                    bootmgr_set_time_thread(0);
                    bootmgr_phase = BOOTMGR_TETRIS;
                    tetris_init();
                    break;
                case KEY_SEARCH:
                {
                    if(bootmgr_toggle_ums())
                        bootmgr_phase = BOOTMGR_UMS;
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
                {
                    if(!total_backups)
                        break;
                    if(selected == 2 || (!backups_has_active && selected == total_backups+4))
                        bootmgr_select(5);
                    else if(selected == total_backups+4)
                            bootmgr_select(2);
                    else
                        bootmgr_select(selected+1);
                    bootmgr_draw();
                    break;
                }
                case KEY_VOLUMEUP:
                {
                    if(!total_backups)
                        break;
                    if(selected == 2 || (!backups_has_active && selected == 5))
                        bootmgr_select(total_backups+4);
                    else if(selected == 5)
                        bootmgr_select(2);
                    else
                        bootmgr_select(selected-1);
                    bootmgr_draw();
                    break;
                }
                case KEY_MENU:
                    return bootmgr_boot_sd();
                case KEY_BACK:
                {
                    selected = -1;
                    bootmgr_set_lines_count(0);
                    bootmgr_set_fills_count(0);
                    bootmgr_display->bg_img = 1;
                    bootmgr_phase = BOOTMGR_MAIN;
                    bootmgr_draw();
                    bootmgr_set_time_thread(1);
                    break;
                }
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
            bootmgr_toggle_ums();
            bootmgr_phase = BOOTMGR_MAIN;
            break;
        }
    }
    return 0;
}

void *bootmgr_input_thread(void *cookie)
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

void bootmgr_init_display()
{
    bootmgr_display = (bootmgr_display_t*)malloc(sizeof(bootmgr_display_t));
    bootmgr_display->ln_count = 0;
    bootmgr_display->fill_count = 0;
    bootmgr_display->img_count = 0;
    bootmgr_display->lines = NULL;
    bootmgr_display->fills = NULL;
    bootmgr_display->imgs = NULL;
    bootmgr_display->bg_img = 1;

    if(bootmgr_open_framebuffer() != 0)
    {
        ERROR("BootMgr: Cant open framebuffer");
        return;
    }
}

void bootmgr_destroy_display()
{
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_set_imgs_count(0);
    free(bootmgr_display);

    bootmgr_close_framebuffer();
}

void bootmgr_printf(int16_t x, uint8_t line, uint16_t color, char *what, ...)
{
    char txt[80];
    va_list ap;
    va_start(ap, what);
    vsnprintf(txt, 80, what, ap);
    va_end(ap);

    bootmgr_line *ln = NULL;
    if(ln = _bootmgr_get_line(line))
        free(ln->text);
    else
        ln = _bootmgr_new_line();

    int16_t text_len = strlen(txt);
    ln->text = malloc(text_len+1);
    strcpy(ln->text, txt);

    if(x == -1)
        ln->x = (BOOTMGR_DIS_W - text_len*8)/2;
    else
        ln->x = x;
    ln->line = line;
    ln->color = color;
}

void bootmgr_print_img(int16_t x, int16_t y, char *name)
{
    int16_t text_len = strlen(name);
    bootmgr_img *img = _bootmgr_new_img();
    img->name = malloc(text_len+1);
    strcpy(img->name, name);
    img->x = x;
    img->y = y;
}

void bootmgr_print_fill(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color, int8_t id)
{
    bootmgr_fill *f = NULL;
    if(id == -1 || !(f = _bootmgr_get_fill(id)))
        f= _bootmgr_new_fill();
    f->x = x;
    f->y = y;
    f->width = width;
    f->height = height;
    f->color = color;
    f->id = id;
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

int bootmgr_open_framebuffer()
{
    if (vt_set_mode(1))
        return -1;

    fd0 = open(bootmgr_bg0, O_RDONLY);
    fd1 = open(bootmgr_bg1, O_RDONLY);
    if (fd0 < 0 || fd1 < 0)
        return -1;

    if (fstat(fd0, &s0) < 0 || fstat(fd1, &s1) < 0)
        return -1;

    if (fb_open(&fb))
        return -1;
    
    max_fb_size = fb_width(&fb) * fb_height(&fb);

    return 0;
}

void bootmgr_close_framebuffer()
{
    fb_close(&fb);
    close(fd0);
    close(fd1);
}

int bootmgr_show_img(uint16_t start_x, uint16_t start_y, char *custom_img)
{
    uint16_t *data, *bits, *ptr;
    uint32_t rgb32, red, green, blue, alpha;
    unsigned count, max;
    struct stat *s = NULL;
    int *fd = NULL;

    if(custom_img)
    {
        char *path = (char*)malloc(50);
        sprintf(path, bootmgr_img_folder, custom_img);

        fd = malloc(sizeof(*fd));

        *fd = open(path, O_RDONLY);
        free(path);

        if(*fd < 0)
            return -1;

        s = malloc(sizeof(*s));
        if (fstat(*fd, s) < 0)
            return -1;
    }
    else
    {
        s = bootmgr_selected ? &s1 : &s0;
        fd = bootmgr_selected ? &fd1 : &fd0;
    }

    data = mmap(0, s->st_size, PROT_READ, MAP_SHARED, *fd, 0);
    if (data == MAP_FAILED)
        return -1;

    max = max_fb_size;
    ptr = data;
    count = s->st_size;
    bits = fb.bits;

    if(start_x || start_y)
        bits += fb_width(&fb)*start_y + start_x;

    while (count > 3) {
        unsigned n = ptr[0];
        if (n > max)
            break;
        android_memset16(bits, ptr[1], n << 1);
        bits += n;
        max -= n;
        ptr += 2;
        count -= 4;
    }

    munmap(data, s->st_size);

    if(custom_img)
    {
        close(*fd);
        free(s);
        free(fd);
    }
    return 0;
}

void _bootmgr_set_px(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t *bits = fb.bits;
    bits += BOOTMGR_DIS_W*y + x;
    android_memset16(bits, color, 2);
}

void _bootmgr_draw_char(char c, uint16_t x, uint16_t y, uint16_t color)
{
    unsigned char line = 0;
    char bit = 0;
    for(; line < ISO_CHAR_HEIGHT; ++line)
    {
        int f = iso_font[ISO_CHAR_HEIGHT*c+line];
        for(bit = 0; bit < 8; ++bit)
            if(f & (1 << bit))
                _bootmgr_set_px(x+bit, y, color);
        ++y;
    }
}

void bootmgr_draw_text()
{
    uint16_t i,z,y;
    bootmgr_line *c = NULL;
    char *txt;

    for(i = 0; i < bootmgr_display->ln_count; ++i)
    {
        c = bootmgr_display->lines[i];
        txt = c->text;
        y = ISO_CHAR_HEIGHT*c->line;

        for(z = 0; *txt; ++txt,z+=8)
            _bootmgr_draw_char(*txt, c->x + z, y, c->color);
    }
}

void bootmgr_draw_fills()
{
    uint16_t i,z;
    bootmgr_fill *c = NULL;
    uint16_t *bits;
    for(i = 0; i < bootmgr_display->fill_count; ++i)
    {
        c = bootmgr_display->fills[i];
        bits = fb.bits;
        bits += BOOTMGR_DIS_W*c->y + c->x;

        for(z = 0; z <= c->height; ++z)
        {
            android_memset16(bits, c->color, c->width*2);
            bits += BOOTMGR_DIS_W;
        }
    }
}

void bootmgr_draw_imgs()
{
    bootmgr_img *c = NULL;
    uint16_t i = 0;
    for(; i < bootmgr_display->img_count; ++i)
    {
        c = bootmgr_display->imgs[i];
        bootmgr_show_img(c->x, c->y, c->name);
    }
}

void bootmgr_set_lines_count(uint16_t c)
{
    bootmgr_line **tmp_lines = bootmgr_display->lines;

    bootmgr_display->lines = (bootmgr_line**)malloc(sizeof(bootmgr_line*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_lines && itr < bootmgr_display->ln_count)
            bootmgr_display->lines[itr] = tmp_lines[itr];
        else
            bootmgr_display->lines[itr] = (bootmgr_line*)malloc(sizeof(bootmgr_line));
    }

    if(tmp_lines)
    {
        for(; itr < bootmgr_display->ln_count; ++itr)
        {
            free(tmp_lines[itr]->text);
            free(tmp_lines[itr]);
        }
        free(tmp_lines);
    }

    bootmgr_display->ln_count = c;
}

void bootmgr_set_fills_count(uint16_t c)
{
    bootmgr_fill **tmp_fills = bootmgr_display->fills;

    bootmgr_display->fills = (bootmgr_fill**)malloc(sizeof(bootmgr_fill*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_fills && itr < bootmgr_display->fill_count)
            bootmgr_display->fills[itr] = tmp_fills[itr];
        else
            bootmgr_display->fills[itr] = (bootmgr_fill*)malloc(sizeof(bootmgr_fill));
    }

    if(tmp_fills)
    {
        for(; itr < bootmgr_display->fill_count; ++itr)
            free(tmp_fills[itr]);
        free(tmp_fills);
    }

    bootmgr_display->fill_count = c;
}

void bootmgr_set_imgs_count(uint16_t c)
{
    bootmgr_img **tmp_imgs = bootmgr_display->imgs;

    bootmgr_display->imgs = (bootmgr_img**)malloc(sizeof(bootmgr_img*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_imgs && itr < bootmgr_display->img_count)
            bootmgr_display->imgs[itr] = tmp_imgs[itr];
        else
            bootmgr_display->imgs[itr] = (bootmgr_img*)malloc(sizeof(bootmgr_img));
    }

    if(tmp_imgs)
    {
        for(; itr < bootmgr_display->img_count; ++itr)
        {
            free(tmp_imgs[itr]->name);
            free(tmp_imgs[itr]);
        }
        free(tmp_imgs);
    }

    bootmgr_display->img_count = c;
}

void bootmgr_draw()
{
    pthread_mutex_lock(&bootmgr_draw_mutex);

    if(bootmgr_display->bg_img)
        bootmgr_show_img(0, 0, NULL);
    else
        android_memset16(fb.bits, BLACK, BOOTMGR_DIS_W*BOOTMGR_DIS_H*2);

    bootmgr_draw_imgs();
    bootmgr_draw_fills();
    bootmgr_draw_text();
    fb_update(&fb);

    pthread_mutex_unlock(&bootmgr_draw_mutex);
}

bootmgr_line *_bootmgr_new_line()
{
    bootmgr_set_lines_count(bootmgr_display->ln_count+1);
    return bootmgr_display->lines[bootmgr_display->ln_count-1];
}

bootmgr_fill *_bootmgr_new_fill()
{
    bootmgr_set_fills_count(bootmgr_display->fill_count+1);
    return bootmgr_display->fills[bootmgr_display->fill_count-1];
}

bootmgr_img *_bootmgr_new_img()
{
    bootmgr_set_imgs_count(bootmgr_display->img_count+1);
    return bootmgr_display->imgs[bootmgr_display->img_count-1];
}

void bootmgr_erase_text(uint8_t line)
{
    bootmgr_line **tmp_lines = (bootmgr_line**)malloc(sizeof(bootmgr_line*)*(bootmgr_display->ln_count-1));

    uint16_t i = 0;
    uint16_t z = 0;
    bootmgr_line *c = NULL;

    for(; i < bootmgr_display->ln_count; ++i)
    {
        c = bootmgr_display->lines[i];
        if(c->line == line)
        {
            free(c->text);
            free(c);
            continue;
        }
        tmp_lines[z++] = c;
    }

    if(i == z)
        return;

    free(bootmgr_display->lines );
    bootmgr_display->lines = tmp_lines;
    --bootmgr_display->ln_count;
}

void bootmgr_erase_fill(int8_t id)
{
    bootmgr_fill **tmp_fills = (bootmgr_fill**)malloc(sizeof(bootmgr_fill*)*(bootmgr_display->fill_count-1));

    uint16_t i = 0;
    uint16_t z = 0;
    bootmgr_fill *c = NULL;

    for(; i < bootmgr_display->fill_count; ++i)
    {
        c = bootmgr_display->fills[i];
        if(c->id == id)
        {
            free(c);
            continue;
        }
        tmp_fills[z++] = c;
    }

    if(i == z)
        return;

    free(bootmgr_display->fills);
    bootmgr_display->fills = tmp_fills;
    --bootmgr_display->fill_count;
}

bootmgr_line *_bootmgr_get_line(uint8_t line)
{
    uint16_t i = 0;

    for(; i < bootmgr_display->ln_count; ++i)
    {
        if(bootmgr_display->lines[i]->line == line)
             return bootmgr_display->lines[i];
    }
    return NULL;
}

bootmgr_fill *_bootmgr_get_fill(int8_t id)
{
    uint16_t i = 0;

    for(; i < bootmgr_display->fill_count; ++i)
    {
        if(bootmgr_display->fills[i]->id == id)
             return bootmgr_display->fills[i];
    }
    return NULL;
}

void bootmgr_show_rom_list()
{
    if(!backups_loaded)
    {
        // mknod
        mknod("/dev/block/mmcblk0p99", (0666 | S_IFBLK), makedev(179, 2));

        //mkdir
        mkdir("/sdroot", (mode_t)0775);
        uid_t uid = decode_uid("system");
        gid_t gid = decode_uid("system");
        chown("/sdroot", uid, gid);

        //mount
        static const char *mount_args[] = { NULL, "ext4", "/dev/block/mmcblk0p99", "/sdroot", "nosuid", "nodev" };
        int res = do_mount(6, mount_args);
        if(res < 0)
        {
            bootmgr_printf(-1, 20, WHITE, "Failed to mount sd-ext!");
            bootmgr_printf(-1, 21, WHITE, "Press back to return.");
            return;
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
                        settings.timezone = atoi(p);
                    else if(strstr(n, "show_seconds"))
                        settings.show_seconds = atoi(p);

                    p = strtok (NULL, "=\n");
                }
            }
            free(con);
            fclose(f);
        }
    }
    bootmgr_toggle_sdcard(0, 0);
}

int8_t bootmgr_get_file(char *name, char *buffer, uint8_t len)
{
    FILE *f = fopen(name, "r");
    if(!f)
        return NULL;

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
        fputs("/dev/block/mmcblk0p98", f);
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
        int res = mknod("/dev/block/mmcblk0p98", (0666 | S_IFBLK), makedev(179, 1));
        if(mknod_only)
            return res;

        mkdir("/sdrt", (mode_t)0775);

        uint8_t i = 0;
        for(; i < 20; ++i)
        {
            res = mount("/dev/block/mmcblk0p98", "/sdrt", "vfat", 0, NULL);
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
        unlink("/dev/block/mmcblk0p98");
    }
    return 0;
}
