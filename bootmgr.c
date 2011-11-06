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
static const char* bootmgr_bg0 = "/bmgr_imgs/init_0.rle";
static const char* bootmgr_bg1 = "/bmgr_imgs/init_1.rle";
static const char* bootmgr_img_folder = "/bmgr_imgs/%s";

struct FB fb;
struct stat s0, s1;
int fd0, fd1;
unsigned max_fb_size;

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

inline void __bootmgr_boot()
{
    char *img = (char*)malloc(20);
    sprintf(img, "init_%u_sel.rle", bootmgr_selected);
    bootmgr_show_img(0, 320, img);
    free(img);

    bootmgr_close_framebuffer();
    bootmgr_input_run = 0;
}

void bootmgr_start(unsigned short timeout_seconds)
{
    if(bootmgr_open_framebuffer() != 0)
    {
        ERROR("BootMgr: Cant open framebuffer");
        return;
    }

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
            bootmgr_show_img(0, 0, NULL);
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
                case KEY_BACK:
                    bootmgr_show_img(0, 320, "init_rbt.rle");
                case KEY_POWER:
                    bootmgr_close_framebuffer();
                    bootmgr_input_run = 0;
                    reboot(key == KEY_POWER ? RB_POWER_OFF : RB_AUTOBOOT);
                    return;
                case KEY_MENU:
                    __bootmgr_boot();
                    return;
                default:break;
            }
        }

        usleep(100000);
        if(!key_pressed)
        {
            if(--timer <= 0)
            {
                __bootmgr_boot();
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

int bootmgr_show_img(unsigned short start_x, unsigned short start_y, char *custom_img)
{
    //struct FB fb;
    //struct stat s;
    unsigned short *data, *bits, *ptr;
    uint32_t rgb32, red, green, blue, alpha;
    unsigned count, max;
    //int fd;
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
                if (fb_bpp(&fb) == 16) {
                        android_memset16(bits, ptr[1], n << 1);
                        bits += n;
                } else {
                        /* convert 16 bits to 32 bits */
                        rgb32 = ((ptr[1] >> 11) & 0x1F);
                        red = (rgb32 << 3) | (rgb32 >> 2);
                        rgb32 = ((ptr[1] >> 5) & 0x3F);
                        green = (rgb32 << 2) | (rgb32 >> 4);
                        rgb32 = ((ptr[1]) & 0x1F);
                        blue = (rgb32 << 3) | (rgb32 >> 2);
                        alpha = 0xff;
                        rgb32 = (alpha << 24) | (blue << 16)
                        | (green << 8) | (red);
                        android_memset32((uint32_t *)bits, rgb32, n << 2);
                        bits += (n * 2);
               }
        max -= n;
        ptr += 2;
        count -= 4;
    }

    munmap(data, s->st_size);
    fb_update(&fb);

    if(custom_img)
    {
        close(*fd);
        free(s);
        free(fd);
    }
    return 0;
}
// TESTING framebuffer write code
/*

uint32_t charA[20];
uint32_t charO[20];
void initFont()
{
    int i = 2;
    charA[0] = 0x1FFFFFF;
    charA[1] = 0x1FFFFFF;
    for(;i<20; ++i)
        charA[i] = 0x7C00;
    charA[18] = 0x1FFFFFF;
    charA[19] = 0x1FFFFFF;

    charO[0] = 0x1FFFFFF;
    charO[1] = 0x1FFFFFF;
    for(i = 2; i < 20; ++i)
        charO[i] = 0x1F0001F;
    charO[18] = 0x1FFFFFF;
    charO[19] = 0x1FFFFFF;
}

void bootmgr_write_time()
{
    initFont();
    uint32_t *charsPo[2];
    charsPo[0] = &charA;
    charsPo[1] = &charO;
    unsigned short startX = 100;
    unsigned short startY = 320;
    unsigned short endX = 125;
    unsigned short endY = 340;
    unsigned short curX = startX;
    unsigned short curY = startY;
    unsigned short *bits = fb.bits;
    unsigned short *curDraw = NULL;
    bits += 320*startY + startX;
    int line = 0;
    uint32_t charL;
    int drawL = 0;
    int row = 0;
    int charW = 0;
    for(; line < endY - startY; ++line)
    {
        row = 0;
        int chars = 0;
        for(; chars < 2; ++chars)
        {
            charL = 0x1000000;
            drawL = 0;
            charW = 0;
            while(1)
            {
                if(charsPo[chars][line] & charL)
                    ++drawL;
                else if(drawL)
                {
                    curDraw = bits + row + (charW - drawL);
                    ERROR("Draw %u %u", drawL, charW);
                    android_memset32((uint32_t *)curDraw, 0xFFFFFFFF, drawL*2);
                    drawL = 0;
                }
                ++charW;
                if(charW >= 25)
                {
                    if(drawL)
                    {
                        ERROR("Draw %u %u", drawL, charW);
                        curDraw = bits + row + (charW - drawL);
                        android_memset32((uint32_t *)curDraw, 0xFFFFFFFF, drawL*2);
                    }
                    break;
                }
                charL /= 2;
            }
            row += 30;
        }
        bits += (320 - startX) + startX;
    }
    fb_update(&fb); 
}*/
