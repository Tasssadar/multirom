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
#include "containers.h"

// only double-buffering is implemented, this define is just
// for the code to know how many buffers we use
#define NUM_BUFFERS 2

#if PIXEL_SIZE == 4
#define fb_memset(dst, what, len) android_memset32(dst, what, len)
#else
#define fb_memset(dst, what, len) android_memset16(dst, what, len)
#endif


static struct FB framebuffers[NUM_BUFFERS];
static int active_fb = 0;
static int fb_frozen = 0;

static fb_items_t fb_items = { NULL, NULL, NULL };
static fb_items_t **inactive_ctx = NULL;
uint32_t fb_width = 0;
uint32_t fb_height = 0;
int fb_rotation = 0; // in degrees, clockwise
static uint8_t **fb_rot_helpers = NULL;
static pthread_mutex_t fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fb_draw_req_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t fb_draw_thread;
static volatile int fb_draw_requested = 0;
static volatile int fb_draw_run = 0;
static void *fb_draw_thread_work(void*);

struct FB *fb = &framebuffers[0];

static void fb_destroy_item(void *item); // private!
static inline void fb_cpy_fb_with_rotation(px_type *dst, px_type *src);
static inline void fb_rotate_90deg(px_type *dst, px_type *src);
static inline void fb_rotate_270deg(px_type *dst, px_type *src);
static inline void fb_rotate_180deg(px_type *dst, px_type *src);

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

int fb_open(int rotation)
{
    fb_rotation = rotation;

    int i;
    int fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd < 0)
        return -1;

    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0)
        goto fail;

    vi.bits_per_pixel = PIXEL_SIZE * 8;
    INFO("Pixel format: %dx%d @ %dbpp\n", vi.xres, vi.yres, vi.bits_per_pixel);

#ifdef RECOVERY_BGRA
    INFO("Pixel format: BGRA_8888\n");
    vi.red.offset     = 8;
    vi.red.length     = 8;
    vi.green.offset   = 16;
    vi.green.length   = 8;
    vi.blue.offset    = 24;
    vi.blue.length    = 8;
    vi.transp.offset  = 0;
    vi.transp.length  = 8;
#elif  defined(RECOVERY_RGBX)
    INFO("Pixel format: RGBX_8888\n");
    vi.red.offset     = 24;
    vi.red.length     = 8;
    vi.green.offset   = 16;
    vi.green.length   = 8;
    vi.blue.offset    = 8;
    vi.blue.length    = 8;
    vi.transp.offset  = 0;
    vi.transp.length  = 8;
#elif defined(RECOVERY_RGB_565)
    INFO("Pixel format: RGB_565\n");
    vi.blue.offset    = 0;
    vi.green.offset   = 5;
    vi.red.offset     = 11;
    vi.blue.length    = 5;
    vi.green.length   = 6;
    vi.red.length     = 5;
    vi.blue.msb_right = 0;
    vi.green.msb_right = 0;
    vi.red.msb_right = 0;
    vi.transp.offset  = 0;
    vi.transp.length  = 0;
#else
#error "Unknown pixel format"
#endif

    vi.vmode = FB_VMODE_NONINTERLACED;
    vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if (ioctl(fd, FBIOPUT_VSCREENINFO, &vi) < 0)
    {
        ERROR("failed to set fb0 vi info");
        goto fail;
    }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) < 0)
        goto fail;

    px_type *bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (bits == MAP_FAILED)
        goto fail;

#ifdef RECOVERY_GRAPHICS_USE_LINELENGTH
    vi.xres_virtual = fi.line_length / PIXEL_SIZE;
#endif

    if(fb_rotation%180 == 0)
    {
        fb_width = vi.xres;
        fb_height = vi.yres;
    }
    else
    {
        fb_width = vi.yres;
        fb_height = vi.xres;
    }

    fb_frozen = 0;
    active_fb = 0;

    px_type *b_store = malloc(vi.xres_virtual*vi.yres*PIXEL_SIZE);
    fb_memset(b_store, fb_convert_color(BLACK), vi.xres_virtual*vi.yres*PIXEL_SIZE);

    for(i = 0; i < NUM_BUFFERS; ++i)
    {
        fb = &framebuffers[i];
        fb->fd = fd;
        fb->size = vi.xres_virtual*vi.yres*PIXEL_SIZE;
        fb->vi = vi;
        fb->fi = fi;
        fb->stride = (fb_rotation%180 == 0) ? vi.xres_virtual : vi.yres;
        fb->bits = b_store;
        fb->mapped = (px_type*)(((uint8_t*)(bits)) + (vi.yres * fi.line_length * i));
    }

    // fb always points to the first framebuffer
    fb = &framebuffers[0];

#if 0
    fb_dump_info();
#endif

    fb_update();

    fb_draw_run = 1;
    pthread_create(&fb_draw_thread, NULL, fb_draw_thread_work, NULL);
    return 0;

fail:
    close(fd);
    return -1;
}

void fb_close(void)
{
    fb_draw_run = 0;
    pthread_join(fb_draw_thread, NULL);

    free(fb_rot_helpers);
    fb_rot_helpers = NULL;

    munmap(fb->mapped, fb->fi.smem_len);
    close(fb->fd);
    free(fb->bits);
}

void fb_dump_info(void)
{
    ERROR("Framebuffer:\n");
    ERROR("fi.smem_len: %u\n", fb->fi.smem_len);
    ERROR("fi.type: %u\n", fb->fi.type);
    ERROR("fi.type_aux: %u\n", fb->fi.type_aux);
    ERROR("fi.visual: %u\n", fb->fi.visual);
    ERROR("fi.xpanstep: %u\n", fb->fi.xpanstep);
    ERROR("fi.ypanstep: %u\n", fb->fi.ypanstep);
    ERROR("fi.ywrapstep: %u\n", fb->fi.ywrapstep);
    ERROR("fi.line_length: %u\n", fb->fi.line_length);
    ERROR("fi.mmio_start: %p\n", (void*)fb->fi.mmio_start);
    ERROR("fi.mmio_len: %u\n", fb->fi.mmio_len);
    ERROR("fi.accel: %u\n", fb->fi.accel);
    ERROR("vi.xres: %u\n", fb->vi.xres);
    ERROR("vi.yres: %u\n", fb->vi.yres);
    ERROR("vi.xres_virtual: %u\n", fb->vi.xres_virtual);
    ERROR("vi.yres_virtual: %u\n", fb->vi.yres_virtual);
    ERROR("vi.xoffset: %u\n", fb->vi.xoffset);
    ERROR("vi.yoffset: %u\n", fb->vi.yoffset);
    ERROR("vi.bits_per_pixel: %u\n", fb->vi.bits_per_pixel);
    ERROR("vi.grayscale: %u\n", fb->vi.grayscale);
    ERROR("vi.red: offset: %u len: %u msb_right: %u\n", fb->vi.red.offset, fb->vi.red.length, fb->vi.red.msb_right);
    ERROR("vi.green: offset: %u len: %u msb_right: %u\n", fb->vi.green.offset, fb->vi.green.length, fb->vi.green.msb_right);
    ERROR("vi.blue: offset: %u len: %u msb_right: %u\n", fb->vi.blue.offset, fb->vi.blue.length, fb->vi.blue.msb_right);
    ERROR("vi.transp: offset: %u len: %u msb_right: %u\n", fb->vi.transp.offset, fb->vi.transp.length, fb->vi.transp.msb_right);
    ERROR("vi.nonstd: %u\n", fb->vi.nonstd);
    ERROR("vi.activate: %u\n", fb->vi.activate);
    ERROR("vi.height: %u\n", fb->vi.height);
    ERROR("vi.width: %u\n", fb->vi.width);
    ERROR("vi.accel_flags: %u\n", fb->vi.accel_flags);
}

void fb_set_active_framebuffer(unsigned n)
{
    if (n > 1)
        return;

    fb->vi.yres_virtual = fb->vi.yres * NUM_BUFFERS;
    fb->vi.yoffset = n * fb->vi.yres;

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0)
        ERROR("active fb swap failed");
}

void fb_update(void)
{
    active_fb = !active_fb;

    fb_cpy_fb_with_rotation(get_active_fb()->mapped, fb->bits);

    fb_set_active_framebuffer(active_fb);
}

void fb_cpy_fb_with_rotation(px_type *dst, px_type *src)
{
    switch(fb_rotation)
    {
        case 0:
            memcpy(dst, src, fb->vi.xres_virtual * fb->vi.yres * PIXEL_SIZE);
            break;
        case 90:
            fb_rotate_90deg(dst, src);
            break;
        case 180:
            fb_rotate_180deg(dst, src);
            break;
        case 270:
            fb_rotate_270deg(dst, src);
            break;
    }
}

void fb_rotate_90deg(px_type *dst, px_type *src)
{
    uint32_t i;
    int32_t x;

    if(!fb_rot_helpers)
        fb_rot_helpers = malloc(fb_height*sizeof(px_type*));

    px_type **helpers = (px_type**)fb_rot_helpers;

    helpers[0] = src;
    for(i = 1; i < fb_height; ++i)
        helpers[i] = helpers[i-1] + fb->stride;

    const int padding = fb->vi.xres_virtual - fb->vi.xres;
    for(i = 0; i < fb_width; ++i)
    {
        for(x = fb_height-1; x >= 0; --x)
            *dst++ = *(helpers[x]++);
        dst += padding;
    }
}

void fb_rotate_270deg(px_type *dst, px_type *src)
{
    if(!fb_rot_helpers)
        fb_rot_helpers = malloc(fb_height*sizeof(px_type*));

    uint32_t i, x;
    px_type **helpers = (px_type**)fb_rot_helpers;

    helpers[0] = src + fb_width-1;
    for(i = 1; i < fb_height; ++i)
        helpers[i] = helpers[i-1] + fb->stride;

    const int padding = fb->vi.xres_virtual - fb->vi.xres;
    for(i = 0; i < fb_width; ++i)
    {
        for(x = 0; x < fb_height; ++x)
            *dst++ = *(helpers[x]--);
        dst += padding;
    }
}

void fb_rotate_180deg(px_type *dst, px_type *src)
{
    uint32_t i, x;
    int len = fb->vi.xres_virtual * fb->vi.yres;
    src += len;

    const int padding = fb->vi.xres_virtual - fb->vi.xres;
    for(i = 0; i < fb_height; ++i)
    {
        src -= padding;
        for(x = 0; x < fb_width; ++x)
            *dst++ = *src--;
        dst += padding;
    }
}

int fb_clone(char **buff)
{
    int len = fb->size;
    *buff = malloc(len);

    pthread_mutex_lock(&fb_mutex);
    memcpy(*buff, fb->bits, len);
    pthread_mutex_unlock(&fb_mutex);

    return len;
}

void fb_fill(uint32_t color)
{
    fb_memset(fb->bits, fb_convert_color(color), fb->size);
}

px_type fb_convert_color(uint32_t c)
{
#ifdef RECOVERY_BGRA
    //             A              R                    G                  B
    return (c & 0xFF000000) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c & 0xFF0000) >> 16);
#elif defined(RECOVERY_RGBX)
    return c;
#elif defined(RECOVERY_RGB_565)
    //            R                                G                              B
    return (((c & 0xFF) >> 3) << 11) | (((c & 0xFF00) >> 10) << 5) | ((c & 0xFF0000) >> 19);
#else
#error "Unknown pixel format"
#endif
}

void fb_draw_text(fb_text *t)
{
    int c_width = ISO_CHAR_WIDTH * t->size;
    int c_height = ISO_CHAR_HEIGHT * t->size; 

    int linelen = fb_width/(c_width);
    int x = t->head.x;
    int y = t->head.y;

    px_type color = fb_convert_color(t->color);

    int i;
    for(i = 0; t->text[i] != 0; ++i)
    {
        switch(t->text[i])
        {
            case '\n':
                y += c_height;
                x = t->head.x;
                continue;
            case '\r':
                x = t->head.x;
                continue;
            case '\f':
                x = t->head.x; 
                y = t->head.y;
                continue;
        }
        if(x < (int)fb_width)
            fb_draw_char(x, y, t->text[i], color, t->size);
        x += c_width;
    }
}

void fb_draw_char(int x, int y, char c, px_type color, int size)
{
    int line = 0;
    uint8_t bit = 0;
    unsigned char *f = (unsigned char*)iso_font + (ISO_CHAR_HEIGHT*c);

    for(; line < ISO_CHAR_HEIGHT; ++line)
    {
        for(bit = 0; bit < ISO_CHAR_WIDTH; ++bit)
        {
            if(*f & (1 << bit))
                fb_draw_square(x+(bit*size), y, color, size);
        }
        y += size;
        ++f;
    }
}

void fb_draw_square(int x, int y, px_type color, int size)
{
    px_type *bits = fb->bits + (fb->stride*y) + x;
    int i;
    for(i = 0; i < size; ++i)
    {
        fb_memset(bits, color, size*PIXEL_SIZE);
        bits += fb->stride;
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
    px_type *bits = fb->bits + (fb->stride*r->head.y) + r->head.x;
    px_type color = fb_convert_color(r->color);
    int w = r->w*PIXEL_SIZE;

    int i;
    for(i = 0; i < r->h; ++i)
    {
        fb_memset(bits, color, w);
        bits += fb->stride;
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

static fb_text *fb_create_text_item(int x, int y, uint32_t color, int size, const char *txt)
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

fb_text *fb_add_text(int x, int y, uint32_t color, int size, const char *fmt, ...)
{
    char txt[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof(txt), fmt, ap);
    va_end(ap);

    return fb_add_text_long(x, y, color, size, txt);
}

fb_text *fb_add_text_long(int x, int y, uint32_t color, int size, char *text)
{
    fb_text *t = fb_create_text_item(x, y, color, size, text);

    pthread_mutex_lock(&fb_mutex);
    list_add(t, &fb_items.texts);
    pthread_mutex_unlock(&fb_mutex);

    return t;
}

fb_rect *fb_add_rect(int x, int y, int w, int h, uint32_t color)
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

void fb_add_rect_notfilled(int x, int y, int w, int h, uint32_t color, int thickness, fb_rect ***list)
{
    fb_rect *r;
    // top
    r = fb_add_rect(x, y, w, thickness, color);
    list_add(r, list);

    // right
    r = fb_add_rect(x + w - thickness, y, thickness, h, color);
    list_add(r, list);

    // bottom
    r = fb_add_rect(x, y + h - thickness, w, thickness, color);
    list_add(r, list);

    // left
    r = fb_add_rect(x, y, thickness, h, color);
    list_add(r, list);
}

void fb_rm_text(fb_text *t)
{
    if(!t)
        return;

    pthread_mutex_lock(&fb_mutex);
    list_rm_noreorder(t, &fb_items.texts, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);
}

void fb_rm_rect(fb_rect *r)
{
    if(!r)
        return;

    pthread_mutex_lock(&fb_mutex);
    list_rm_noreorder(r, &fb_items.rects, &fb_destroy_item);
    pthread_mutex_unlock(&fb_mutex);
}

#define BOX_BORDER (2*DPI_MUL)
#define SHADOW (10*DPI_MUL)
fb_msgbox *fb_create_msgbox(int w, int h, int bgcolor)
{
    if(fb_items.msgbox)
        return fb_items.msgbox;

    fb_msgbox *box = mzalloc(sizeof(fb_msgbox));

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
                                     w-BOX_BORDER*2, h-BOX_BORDER*2, bgcolor);

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
        list_rm_noreorder(text, &fb_items.msgbox->texts, &fb_destroy_item);
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

#if PIXEL_SIZE == 4
#define ALPHA 220
#define BLEND_CLR 0x1B
static inline int blend(int value1, int value2) {
    int r = (0xFF-ALPHA)*value1 + ALPHA*value2;
    return (r+1 + (r >> 8)) >> 8; // divide by 255
}
#else
#ifdef HAS_NEON_BLEND
extern void scanline_col32cb16blend_neon(uint16_t *dst, uint32_t *col, size_t ct);
#else
#define ALPHA5 0x04
#define ALPHA6 0x08
#define BLEND_CLR5 0x03
#define BLEND_CLR6 0x06
#endif
#endif

void fb_draw_overlay(void)
{
#ifdef MR_DISABLE_ALPHA
    fb_fill(0xFF1B1B1B);
#else
 #if PIXEL_SIZE == 4
    int i;
    uint8_t *bits = (uint8_t*)fb->bits;
    const int size = fb->vi.xres_virtual*fb->vi.yres;
    for(i = 0; i < size; ++i)
    {
        *bits = blend(*bits, BLEND_CLR);
        ++bits;
        *bits = blend(*bits, BLEND_CLR);
        ++bits;
        *bits = blend(*bits, BLEND_CLR);
        bits += 2;
    }
 #else
  #ifdef HAS_NEON_BLEND
    uint32_t blend_clr = 0xDC1B1B1B;
    scanline_col32cb16blend_neon((uint16_t*)fb->bits, &blend_clr, fb->size >> 1);
  #else
    const int size = fb->size >> 1;
    uint16_t *bits = fb->bits;
    int i;
    for(i = 0; i < size; ++i)
    {
        *bits = ((ALPHA5*(*bits & 0x1F) + (ALPHA5*BLEND_CLR5)) / 31) |
            (((ALPHA6*((*bits & 0x7E0) >> 5) + (ALPHA6*BLEND_CLR6)) / 63) << 5) |
            (((ALPHA5*((*bits & 0xF800) >> 11) + (ALPHA5*BLEND_CLR5)) / 31) << 11);
        ++bits;
    }
  #endif
 #endif // PIXEL_SIZE
#endif // MR_DISABLE_ALPHA
}

void fb_draw(void)
{
    if(fb_frozen)
        return;

    uint32_t i;
    pthread_mutex_lock(&fb_mutex);

    fb_fill(BLACK);

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

    fb_update();

    pthread_mutex_unlock(&fb_mutex);
}

void fb_freeze(int freeze)
{
    if(freeze)
        ++fb_frozen;
    else
        --fb_frozen;
}

int center_x(int x, int width, int size, const char *text)
{
    return x + (width/2 - (strlen(text)*ISO_CHAR_WIDTH*size)/2);
}

int center_y(int y, int height, int size)
{
    return y + (height/2 - (ISO_CHAR_HEIGHT*size)/2);
}

void fb_push_context(void)
{
    fb_items_t *ctx = mzalloc(sizeof(fb_items_t));

    pthread_mutex_lock(&fb_mutex);

    list_move(&fb_items.texts, &ctx->texts);
    list_move(&fb_items.rects, &ctx->rects);
    ctx->msgbox = fb_items.msgbox;
    fb_items.msgbox = NULL;

    pthread_mutex_unlock(&fb_mutex);

    list_add(ctx, &inactive_ctx);
}

void fb_pop_context(void)
{
    if(!inactive_ctx)
        return;

    fb_clear();

    int idx = list_item_count(inactive_ctx)-1;
    fb_items_t *ctx = inactive_ctx[idx];

    pthread_mutex_lock(&fb_mutex);

    list_move(&ctx->texts, &fb_items.texts);
    list_move(&ctx->rects, &fb_items.rects);
    fb_items.msgbox = ctx->msgbox;

    pthread_mutex_unlock(&fb_mutex);

    list_rm_noreorder(ctx, &inactive_ctx, &free);

    fb_draw();
}

#define SLEEP_CONST 16
void *fb_draw_thread_work(void *cookie)
{
    volatile int req = 0;

    struct timespec last, curr;
    uint32_t diff = 0, prevSleepTime = 0;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while(fb_draw_run)
    {
        clock_gettime(CLOCK_MONOTONIC, &curr);
        diff = timespec_diff(&last, &curr);

        pthread_mutex_lock(&fb_draw_req_mutex);
        req = fb_draw_requested;
        fb_draw_requested = 0;
        pthread_mutex_unlock(&fb_draw_req_mutex);

        if(req)
            fb_draw();

        last = curr;
        if(diff <= SLEEP_CONST+prevSleepTime)
        {
            prevSleepTime = SLEEP_CONST+prevSleepTime-diff;
            usleep(prevSleepTime*1000);
        }
        else
            prevSleepTime = 0;
    }
    return NULL;
}

void fb_request_draw(void)
{
    pthread_mutex_lock(&fb_draw_req_mutex);
    fb_draw_requested = 1;
    pthread_mutex_unlock(&fb_draw_req_mutex);
}
