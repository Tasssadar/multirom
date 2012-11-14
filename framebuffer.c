#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <cutils/memory.h>
#include <pthread.h>

#include "log.h"
#include "framebuffer.h"
#include "iso_font.h"
#include "util.h"

static struct FB framebuffers[2];
static int active_fb = 0;
static int fb_frozen = 0;

static fb_items_t fb_items = { NULL, NULL, NULL };
int fb_width = 0;
int fb_height = 0;
static pthread_mutex_t fb_mutex = PTHREAD_MUTEX_INITIALIZER;

struct FB *fb = &framebuffers[0];

void fb_destroy_item(void *item); // private!

int vt_set_mode(int graphics)
{
    int fd, r;
    mknod("/dev/tty0", (0600 | S_IFCHR), makedev(4, 0));
    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (fd < 0)
        return -1;
    r = ioctl(fd, KDSETMODE, (void*) (graphics ? KD_GRAPHICS : KD_TEXT));
    close(fd);
    unlink("/dev/tty0");
    return r;
}

struct FB *get_active_fb()
{
    return &framebuffers[active_fb];
}

int fb_open(void)
{
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0)
        return -1;

    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0)
        goto fail;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0)
        goto fail;

    uint32_t *bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (bits == MAP_FAILED)
        goto fail;

    fb_width = vi.xres;
    fb_height = vi.yres;
    fb_frozen = 0;

    uint32_t *b_store = malloc(vi.xres*vi.yres*4);
    android_memset32(b_store, BLACK, vi.xres*vi.yres*4);

    fb->fd = fd;
    fb->size = vi.xres*vi.yres*4;
    fb->vi = vi;
    fb->fi = fi;
    fb->bits = b_store;
    fb->mapped = bits;

    ++fb;

    fb->fd = fd;
    fb->size = vi.xres*vi.yres*4;
    fb->vi = vi;
    fb->fi = fi;
    fb->bits = b_store;
    fb->mapped = (void*) (((unsigned) bits) + vi.yres * fi.line_length);

    --fb;

    fb_update();

    return 0;

fail:
    close(fd);
    return -1;
}

void fb_close(void)
{
    munmap(fb->mapped, fb->fi.smem_len);
    close(fb->fd);
    free(fb->bits);
}

void fb_set_active_framebuffer(unsigned n)
{
    if (n > 1) return;
    fb->vi.yres_virtual = fb->vi.yres * 4;
    fb->vi.yoffset = n * fb->vi.yres;
    fb->vi.bits_per_pixel = 4 * 8;
    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0) {
        ERROR("active fb swap failed");
    }
}

void fb_update(void)
{
    active_fb = !active_fb;

    memcpy(get_active_fb()->mapped, fb->bits, fb->size);

    fb_set_active_framebuffer(active_fb);
}

void fb_fill(uint32_t color)
{
    android_memset32(fb->bits, color, fb->size);
}

void fb_draw_text(fb_text *t)
{
    int c_width = ISO_CHAR_WIDTH * t->size;
    int c_height = ISO_CHAR_HEIGHT * t->size; 

    int linelen = fb_width/(c_width);
    int x = t->head.x;
    int y = t->head.y;

    int i;
    for(i = 0; t->text[i] != 0; ++i)
    {
        switch(t->text[i])
        {
            case '\n':
                y += c_height;
                x = 0;
                continue;
            case '\r':
                x = 0;
                continue;
            case '\f':
                x = 0; 
                y = 0;
                continue;
        }
        fb_draw_char(x, y, t->text[i], t->color, t->size);
        x += c_width;
    }
}

void fb_draw_char(int x, int y, char c, int color, int size)
{
    int line = 0;
    uint8_t bit = 0;
    int f;

    for(; line < ISO_CHAR_HEIGHT; ++line)
    {
        f = iso_font[ISO_CHAR_HEIGHT*c+line];
        for(bit = 0; bit < ISO_CHAR_WIDTH; ++bit)
        {
            if(f & (1 << bit))
                fb_draw_square(x+(bit*size), y, color, size);
        }
        y += size;
    }
}

void fb_draw_square(int x, int y, int color, int size)
{
    uint32_t *bits = fb->bits + (fb_width*y) + x;
    int i;
    for(i = 0; i < size; ++i)
    {
        android_memset32(bits, color, size*4);
        bits += fb_width;
    }
}

void fb_remove_item(void *item)
{
    switch(((fb_item_header*)item)->type)
    {
        case FB_TEXT:
            fb_rm_text((fb_text*)item);
            break;
        case FB_RECT:
            fb_rm_rect((fb_rect*)item);
            break;
        case FB_BOX:
            // fb_destroy_msgbox must be used
            assert(0);
            break;
    }
}

void fb_destroy_item(void *item)
{
    switch(((fb_item_header*)item)->type)
    {
        case FB_TEXT:
            free(((fb_text*)item)->text);
            break;
        case FB_RECT:
            break;
        case FB_BOX:
            // fb_destroy_msgbox must be used
            assert(0);
            break;
    }
    free(item);
}

void fb_draw_rect(fb_rect *r)
{
    uint32_t *bits = fb->bits + (fb_width*r->head.y) + r->head.x;

    int i;
    for(i = 0; i < r->h; ++i)
    {
        android_memset32(bits, r->color, r->w*4);
        bits += fb_width;
    }
}

int fb_generate_item_id()
{
    pthread_mutex_lock(&fb_mutex);
    static int id = 0;
    int res = id++;
    pthread_mutex_unlock(&fb_mutex);

    return res;
}

static fb_text *fb_create_text_item(int x, int y, int color, int size, const char *txt)
{
    fb_text *t = malloc(sizeof(fb_text));
    t->head.id = fb_generate_item_id();
    t->head.type = FB_TEXT;
    t->head.x = x;
    t->head.y = y;

    t->color = color;
    t->size = size;

    t->text = malloc(strlen(txt)+1);
    strcpy(t->text, txt);

    return t;
}

fb_text *fb_add_text(int x, int y, int color, int size, const char *fmt, ...)
{
    char txt[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof(txt), fmt, ap);
    va_end(ap);

    fb_text *t = fb_create_text_item(x, y, color, size, txt);

    pthread_mutex_lock(&fb_mutex);
    list_add(t, &fb_items.texts);
    pthread_mutex_unlock(&fb_mutex);

    return t;
}

fb_rect *fb_add_rect(int x, int y, int w, int h, int color)
{
    fb_rect *r = malloc(sizeof(fb_rect));
    r->head.id = fb_generate_item_id();
    r->head.type = FB_RECT;
    r->head.x = x;
    r->head.y = y;

    r->w = w;
    r->h = h;
    r->color = color;

    pthread_mutex_lock(&fb_mutex);
    list_add(r, &fb_items.rects);
    pthread_mutex_unlock(&fb_mutex);

    return r;
}

void fb_rm_text(fb_text *t)
{
    if(!t)
        return;

    pthread_mutex_lock(&fb_mutex);
    list_rm(t, &fb_items.texts, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);
}

void fb_rm_rect(fb_rect *r)
{
    if(!r)
        return;

    pthread_mutex_lock(&fb_mutex);
    list_rm(r, &fb_items.rects, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);
}

#define BOX_BORDER 2
#define SHADOW 10
fb_msgbox *fb_create_msgbox(int w, int h)
{
    if(fb_items.msgbox)
        return fb_items.msgbox;

    fb_msgbox *box = malloc(sizeof(fb_msgbox));
    memset(box, 0, sizeof(fb_msgbox));

    int x = fb_width/2 - w/2;
    int y = fb_height/2 - h/2;

    box->head.id = fb_generate_item_id();
    box->head.type = FB_BOX;
    box->head.x = x;
    box->head.y = y;
    box->w = w;
    box->h = h;

    box->background[0] = fb_add_rect(x+SHADOW, y+SHADOW, w, h, GRAY); // shadow
    box->background[1] = fb_add_rect(x, y, w, h, WHITE); // border
    box->background[2] = fb_add_rect(x+BOX_BORDER, y+BOX_BORDER,
                                     w-BOX_BORDER*2, h-BOX_BORDER*2, DRED);

    pthread_mutex_lock(&fb_mutex);
    fb_items.msgbox = box;
    pthread_mutex_unlock(&fb_mutex);
    return box;
}

fb_text *fb_msgbox_add_text(int x, int y, int size, char *fmt, ...)
{
    char txt[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof(txt), fmt, ap);
    va_end(ap);

    fb_msgbox *box = fb_items.msgbox;

    if(x == -1)
        x = center_x(0, box->w, size, txt);

    if(y == -1)
        y = center_y(0, box->h, size);

    x += box->head.x;
    y += box->head.y;

    fb_text *t = fb_create_text_item(x, y, WHITE, size, txt);
    pthread_mutex_lock(&fb_mutex);
    list_add(t, &box->texts);
    pthread_mutex_unlock(&fb_mutex);

    return t;
}

void fb_msgbox_rm_text(fb_text *text)
{
    if(!text)
        return;

    pthread_mutex_lock(&fb_mutex);
    if(fb_items.msgbox)
        list_rm(text, &fb_items.msgbox->texts, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);
}

void fb_destroy_msgbox(void)
{
    pthread_mutex_lock(&fb_mutex);
    if(!fb_items.msgbox)
    {
        pthread_mutex_unlock(&fb_mutex);
        return;
    }

    fb_msgbox *box = fb_items.msgbox;
    fb_items.msgbox = NULL;
    pthread_mutex_unlock(&fb_mutex);

    list_clear(&box->texts, &fb_destroy_item);

    uint32_t i;
    for(i = 0; i < ARRAY_SIZE(box->background); ++i)
        fb_rm_rect(box->background[i]);

    free(box);
}

void fb_clear(void)
{
    pthread_mutex_lock(&fb_mutex);
    list_clear(&fb_items.texts, &fb_destroy_item);
    list_clear(&fb_items.rects, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);

    fb_destroy_msgbox();
}

#define ALPHA 220
#define BLEND_CLR 0x1B

static inline int blend(int value1, int value2) {
    int r = (0xFF-ALPHA)*value1 + ALPHA*value2;
    return (r+1 + (r >> 8)) >> 8; // divide by 255
}

void fb_draw_overlay(void)
{
    union clr_t
    {
        int i;
        uint8_t c[4];
    };

    int i;
    union clr_t *unions = (union clr_t *)fb->bits;
    const int size = fb_width*fb_height;
    for(i = 0; i < size; ++i)
    {
        unions->c[0] = blend(unions->c[0], BLEND_CLR);
        unions->c[1] = blend(unions->c[1], BLEND_CLR);
        unions->c[2] = blend(unions->c[2], BLEND_CLR);
        ++unions;
    }
}

void fb_draw(void)
{
    if(fb_frozen)
        return;

    uint32_t i;
    fb_fill(BLACK);

    pthread_mutex_lock(&fb_mutex);

    // rectangles
    for(i = 0; fb_items.rects && fb_items.rects[i]; ++i)
        fb_draw_rect(fb_items.rects[i]);

    // texts
    for(i = 0; fb_items.texts && fb_items.texts[i]; ++i)
        fb_draw_text(fb_items.texts[i]);

    // msg box
    if(fb_items.msgbox)
    {
        fb_draw_overlay();

        fb_msgbox *box = fb_items.msgbox;

        for(i = 0; i < ARRAY_SIZE(box->background); ++i)
            fb_draw_rect(box->background[i]);

        for(i = 0; box->texts && box->texts[i]; ++i)
            fb_draw_text(box->texts[i]);
    }

    pthread_mutex_unlock(&fb_mutex);

    fb_update();
}

void fb_freeze(int freeze)
{
    fb_frozen = freeze;
}

int center_x(int x, int width, int size, const char *text)
{
    return x + (width/2 - (strlen(text)*ISO_CHAR_WIDTH*size)/2);
}

int center_y(int y, int height, int size)
{
    return y + (height/2 - (ISO_CHAR_HEIGHT*size)/2);
}
