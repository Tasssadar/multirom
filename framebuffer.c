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
#include <sys/atomics.h>
#include <png.h>

#include "log.h"
#include "framebuffer.h"
#include "util.h"
#include "containers.h"
#include "animation.h"

#if PIXEL_SIZE == 4
#define fb_memset(dst, what, len) android_memset32(dst, what, len)
#else
#define fb_memset(dst, what, len) android_memset16(dst, what, len)
#endif


uint32_t fb_width = 0;
uint32_t fb_height = 0;
int fb_rotation = 0; // in degrees, clockwise

static struct framebuffer fb;
static int fb_frozen = 0;
static int fb_force_generic = 0;
static fb_items_t fb_items = { NULL, NULL, NULL };
static fb_items_t **inactive_ctx = NULL;
static uint8_t **fb_rot_helpers = NULL;
static pthread_mutex_t fb_items_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fb_update_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t fb_draw_thread;
static volatile int fb_draw_requested = 0;
static volatile int fb_draw_run = 0;
static volatile int fb_draw_futex = 0;
static void *fb_draw_thread_work(void*);

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

int fb_open_impl(void)
{
    struct fb_impl **itr;
    struct fb_impl *impls[FB_IMPL_CNT];

#define ADD_IMPL(ID, N) \
    extern struct fb_impl fb_impl_ ## N; \
    impls[ID] = &fb_impl_ ## N;

    ADD_IMPL(FB_IMPL_GENERIC, generic);
#ifdef MR_USE_QCOM_OVERLAY
    ADD_IMPL(FB_IMPL_QCOM_OVERLAY, qcom_overlay);
#endif

    if(fb_force_generic)
        itr = &impls[FB_IMPL_GENERIC];
    else
        itr = impls;

    for(; *itr; ++itr)
    {
        if((*itr)->open(&fb) >= 0)
        {
            INFO("Framebuffer implementation: %s\n", (*itr)->name);
            fb.impl = *itr;
            return 0;
        }
    }

    ERROR("All framebuffer implementations have failed to open!\n");
    return -1;
}

int fb_open(int rotation)
{
    memset(&fb, 0, sizeof(struct framebuffer));

    fb.fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb.fd < 0)
        return -1;

    if(ioctl(fb.fd, FBIOGET_VSCREENINFO, &fb.vi) < 0)
        goto fail;

    if(ioctl(fb.fd, FBIOGET_FSCREENINFO, &fb.fi) < 0)
        goto fail;

    /*
     * No FBIOPUT_VSCREENINFO ioctl must be called here. Flo's display drivers
     * contain a hack to set backlight while in recovery, which is triggered by
     * this ioctl (and probably other things). The hack turns *something* on
     * and it causes insane battery drain while in android (it eats at least
     * five times more energy). The device enters deep sleep just fine, the dmesg
     * says it was suspended, but it drains more energy. Qualcomm ION overlay
     * framebuffer implementation works around this hack, because it doesn't
     * require that ioctl (but we can't set pixel format without it, must use
     * framebuffer's default format in "TARGET_RECOVERY_PIXEL_FORMAT" config
     * value). This bug does not manifest when MultiROM isn't installed,
     * because nothing sets FBIOPUT_VSCREENINFO during Android's boot,
     * and well, you're not really supposed to stay long in recovery nor does
     * it have "suspend" state.
     *
     * This ioctl call was moved to framebuffer_generic implementation.
     */

    if(fb_open_impl() < 0)
        goto fail;

    fb_frozen = 0;
    fb_rotation = rotation;

    if(fb_rotation%180 == 0)
    {
        fb_width = fb.vi.xres;
        fb_height = fb.vi.yres;
    }
    else
    {
        fb_width = fb.vi.yres;
        fb_height = fb.vi.xres;
    }

#ifdef RECOVERY_GRAPHICS_USE_LINELENGTH
    fb.vi.xres_virtual = fb.fi.line_length / PIXEL_SIZE;
#endif

    fb.stride = (fb_rotation%180 == 0) ? fb.vi.xres_virtual : fb.vi.yres;
    fb.size = fb.vi.xres_virtual*fb.vi.yres*PIXEL_SIZE;
    fb.buffer = malloc(fb.size);
    fb_memset(fb.buffer, fb_convert_color(BLACK), fb.size);

#if 0
    fb_dump_info();
#endif

    fb_update();

    fb_draw_run = 1;
    pthread_create(&fb_draw_thread, NULL, fb_draw_thread_work, NULL);
    return 0;

fail:
    close(fb.fd);
    return -1;
}

void fb_close(void)
{
    fb_draw_run = 0;
    pthread_join(fb_draw_thread, NULL);

    free(fb_rot_helpers);
    fb_rot_helpers = NULL;

    fb.impl->close(&fb);
    fb.impl = NULL;

    close(fb.fd);
    free(fb.buffer);
    fb.buffer = NULL;
}

void fb_dump_info(void)
{
    ERROR("Framebuffer:\n");
    ERROR("fi.smem_len: %u\n", fb.fi.smem_len);
    ERROR("fi.type: %u\n", fb.fi.type);
    ERROR("fi.type_aux: %u\n", fb.fi.type_aux);
    ERROR("fi.visual: %u\n", fb.fi.visual);
    ERROR("fi.xpanstep: %u\n", fb.fi.xpanstep);
    ERROR("fi.ypanstep: %u\n", fb.fi.ypanstep);
    ERROR("fi.ywrapstep: %u\n", fb.fi.ywrapstep);
    ERROR("fi.line_length: %u\n", fb.fi.line_length);
    ERROR("fi.mmio_start: %p\n", (void*)fb.fi.mmio_start);
    ERROR("fi.mmio_len: %u\n", fb.fi.mmio_len);
    ERROR("fi.accel: %u\n", fb.fi.accel);
    ERROR("vi.xres: %u\n", fb.vi.xres);
    ERROR("vi.yres: %u\n", fb.vi.yres);
    ERROR("vi.xres_virtual: %u\n", fb.vi.xres_virtual);
    ERROR("vi.yres_virtual: %u\n", fb.vi.yres_virtual);
    ERROR("vi.xoffset: %u\n", fb.vi.xoffset);
    ERROR("vi.yoffset: %u\n", fb.vi.yoffset);
    ERROR("vi.bits_per_pixel: %u\n", fb.vi.bits_per_pixel);
    ERROR("vi.grayscale: %u\n", fb.vi.grayscale);
    ERROR("vi.red: offset: %u len: %u msb_right: %u\n", fb.vi.red.offset, fb.vi.red.length, fb.vi.red.msb_right);
    ERROR("vi.green: offset: %u len: %u msb_right: %u\n", fb.vi.green.offset, fb.vi.green.length, fb.vi.green.msb_right);
    ERROR("vi.blue: offset: %u len: %u msb_right: %u\n", fb.vi.blue.offset, fb.vi.blue.length, fb.vi.blue.msb_right);
    ERROR("vi.transp: offset: %u len: %u msb_right: %u\n", fb.vi.transp.offset, fb.vi.transp.length, fb.vi.transp.msb_right);
    ERROR("vi.nonstd: %u\n", fb.vi.nonstd);
    ERROR("vi.activate: %u\n", fb.vi.activate);
    ERROR("vi.height: %u\n", fb.vi.height);
    ERROR("vi.width: %u\n", fb.vi.width);
    ERROR("vi.accel_flags: %u\n", fb.vi.accel_flags);
}

int fb_get_vi_xres(void)
{
    return fb.vi.xres;
}

int fb_get_vi_yres(void)
{
    return fb.vi.yres;
}

void fb_force_generic_impl(int force)
{
    fb_force_generic = force;
}

void fb_update(void)
{
    fb_cpy_fb_with_rotation(fb.impl->get_frame_dest(&fb), fb.buffer);
    fb.impl->update(&fb);
}

void fb_cpy_fb_with_rotation(px_type *dst, px_type *src)
{
    switch(fb_rotation)
    {
        case 0:
            memcpy(dst, src, fb.vi.xres_virtual * fb.vi.yres * PIXEL_SIZE);
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
        helpers[i] = helpers[i-1] + fb.stride;

    const int padding = fb.vi.xres_virtual - fb.vi.xres;
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
        helpers[i] = helpers[i-1] + fb.stride;

    const int padding = fb.vi.xres_virtual - fb.vi.xres;
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
    int len = fb.vi.xres_virtual * fb.vi.yres;
    src += len;

    const int padding = fb.vi.xres_virtual - fb.vi.xres;
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
    int len = fb.size;
    *buff = malloc(len);

    pthread_mutex_lock(&fb_items_mutex);
    memcpy(*buff, fb.buffer, len);
    pthread_mutex_unlock(&fb_items_mutex);

    return len;
}

void fb_fill(uint32_t color)
{
    fb_memset(fb.buffer, fb_convert_color(color), fb.size);
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

void fb_remove_item(void *item)
{
    switch(((fb_item_header*)item)->type)
    {
        case FB_IT_RECT:
            fb_rm_rect((fb_rect*)item);
            break;
        case FB_IT_BOX:
            // fb_destroy_msgbox must be used
            assert(0);
            break;
        case FB_IT_IMG:
            fb_rm_img((fb_img*)item);
            break;
    }
}

void fb_destroy_item(void *item)
{
    anim_fb_item_removed(item);

    switch(((fb_item_header*)item)->type)
    {
        case FB_IT_RECT:
            break;
        case FB_IT_BOX:
            // fb_destroy_msgbox must be used
            assert(0);
            break;
        case FB_IT_IMG:
        {
            fb_img *i = (fb_img*)item;
            switch(i->img_type)
            {
                case FB_IMG_TYPE_PNG:
                    fb_png_release(i->data);
                    break;
                case FB_IMG_TYPE_GENERIC:
                    free(i->data);
                    break;
                case FB_IMG_TYPE_TEXT:
                    fb_text_destroy(i);
                    break;
                default:
                    ERROR("fb_destroy_item(): unknown fb_img type %d\n", i->img_type);
                    assert(0);
                    break;
            }
            break;
        }
    }
    free(item);
}

void fb_draw_rect(fb_rect *r)
{
    px_type *bits = fb.buffer + (fb.stride*r->y) + r->x;
    px_type color = fb_convert_color(r->color);
    const int w = r->w*PIXEL_SIZE;

    int i;
    for(i = 0; i < r->h; ++i)
    {
        fb_memset(bits, color, w);
        bits += fb.stride;
    }
}

static inline int blend_png(int value1, int value2, int alpha) {
    int r = (0xFF-alpha)*value1 + alpha*value2;
    return (r+1 + (r >> 8)) >> 8; // divide by 255
}

void fb_draw_img(fb_img *i)
{
    int y, x;
    px_type *bits = fb.buffer + (fb.stride*i->y) + i->x;
    px_type *img = i->data;
    const int w = i->w*PIXEL_SIZE;
    uint8_t alpha;
    uint8_t *comps_img, *comps_bits;

#if PIXEL_SIZE == 4
    const uint8_t max_alpha = 0xFF;
#elif PIXEL_SIZE == 2
    const uint8_t max_alpha = 31;
#endif

    const int min_x = i->x >= 0 ? 0 : i->x + i->w;
    const int min_y = i->y >= 0 ? 0 : i->y + i->h;
    const int max_x = imin(i->w, fb_width - i->x);
    const int max_y = imin(i->h, fb_height - i->y);
    const int rendered_w = max_x - min_x;

    img = (px_type*)(((uint32_t*)img) + min_y * i->w);

    for(y = min_y; y < max_y; ++y)
    {
        for(x = min_x; x < max_x; ++x)
        {
            // Colors, 0xAABBGGRR
#if PIXEL_SIZE == 4
            alpha = PX_GET_A(*img);
#elif PIXEL_SIZE == 2
            alpha = ((uint8_t*)img)[2];
#endif
            // fully opaque
            if(alpha == max_alpha)
            {
                *bits = *img;
            }
            // do the blending
            else if(alpha != 0x00)
            {
#ifdef MR_DISABLE_ALPHA
                *bits = *img;
#else
  #if PIXEL_SIZE == 4
                comps_bits = (uint8_t*)bits;
                comps_img = (uint8_t*)img;
                comps_bits[PX_IDX_R] = blend_png(comps_bits[PX_IDX_R], comps_img[PX_IDX_R], comps_img[PX_IDX_A]);
                comps_bits[PX_IDX_G] = blend_png(comps_bits[PX_IDX_G], comps_img[PX_IDX_G], comps_img[PX_IDX_A]);
                comps_bits[PX_IDX_B] = blend_png(comps_bits[PX_IDX_B], comps_img[PX_IDX_B], comps_img[PX_IDX_A]);
                comps_bits[PX_IDX_A] = 0xFF;
  #else
                const uint8_t alpha5b = alpha;
                const uint8_t alpha6b = ((uint8_t*)img)[3];
                *bits = (((31-alpha5b)*(*bits & 0x1F)            + (alpha5b*(*img & 0x1F))) / 31) |
                        ((((63-alpha6b)*((*bits & 0x7E0) >> 5)   + (alpha6b*((*img & 0x7E0) >> 5))) / 63) << 5) |
                        ((((31-alpha5b)*((*bits & 0xF800) >> 11) + (alpha5b*((*img & 0xF800) >> 11))) / 31) << 11);
  #endif // PIXEL_SIZE
#endif // MR_DISABLE_ALPHA
            }

            ++bits;
#if PIXEL_SIZE == 4
            ++img;
#elif PIXEL_SIZE == 2
            img += 2;
#endif
        }
        bits += fb.stride - rendered_w;
        img = (px_type*)(((uint32_t*)img) + (i->w - rendered_w));
    }
}

int fb_generate_item_id()
{
    pthread_mutex_lock(&fb_items_mutex);
    static int id = 0;
    int res = id++;
    pthread_mutex_unlock(&fb_items_mutex);

    return res;
}

fb_img *fb_add_text(int x, int y, uint32_t color, int size, const char *fmt, ...)
{
    int ret;
    fb_img *res;
    char txt[512];
    va_list ap;
    char *buff = txt;

    txt[0] = 0;

    va_start(ap, fmt);
    ret = vsnprintf(txt, sizeof(txt), fmt, ap);
    if(ret >= (int)sizeof(txt))
    {
        buff = malloc(ret+1);
        vsnprintf(buff, ret+1, fmt, ap);
    }
    va_end(ap);

    res = fb_add_text_long(x, y, color, size, buff);
    if(ret >= (int)sizeof(txt))
        free(buff);
    return res;
}

fb_img *fb_add_text_justified(int x, int y, uint32_t color, int size, int justify, const char *fmt, ...)
{
    int ret;
    fb_img *res;
    char txt[512] = { 0 };
    va_list ap;
    char *buff = txt;

    txt[0] = 0;

    va_start(ap, fmt);
    ret = vsnprintf(txt, sizeof(txt), fmt, ap);
    if(ret >= (int)sizeof(txt))
    {
        buff = malloc(ret+1);
        vsnprintf(buff, ret+1, fmt, ap);
    }
    va_end(ap);

    res = fb_add_text_long_justified(x, y, color, size, justify, buff);
    if(ret >= (int)sizeof(txt))
        free(buff);
    return res;
}

fb_img *fb_add_text_long_justified(int x, int y, uint32_t color, int size, int justify, const char *text)
{
    fb_img *img = fb_text_create_item(x, y, color, size, justify, text);
    if(img)
    {
        pthread_mutex_lock(&fb_items_mutex);
        list_add(img, &fb_items.imgs);
        pthread_mutex_unlock(&fb_items_mutex);
    }
    return img;
}

fb_rect *fb_add_rect(int x, int y, int w, int h, uint32_t color)
{
    fb_rect *r = malloc(sizeof(fb_rect));
    r->id = fb_generate_item_id();
    r->type = FB_IT_RECT;
    r->x = x;
    r->y = y;

    r->w = w;
    r->h = h;
    r->color = color;

    pthread_mutex_lock(&fb_items_mutex);
    list_add(r, &fb_items.rects);
    pthread_mutex_unlock(&fb_items_mutex);

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

fb_img *fb_add_img(int x, int y, int w, int h, int img_type, px_type *data)
{
    fb_img *result = mzalloc(sizeof(fb_img));
    result->type = FB_IT_IMG;
    result->x = x;
    result->y = y;
    result->img_type = img_type;
    result->data = data;
    result->w = w;
    result->h = h;

    pthread_mutex_lock(&fb_items_mutex);
    list_add(result, &fb_items.imgs);
    pthread_mutex_unlock(&fb_items_mutex);

    return result;
}

fb_img* fb_add_png_img(int x, int y, int w, int h, const char *path)
{
    px_type *data = fb_png_get(path, w, h);
    if(!data)
        return NULL;

    return fb_add_img(x, y, w, h, FB_IMG_TYPE_PNG, data);
}

void fb_rm_rect(fb_rect *r)
{
    if(!r)
        return;

    pthread_mutex_lock(&fb_items_mutex);
    list_rm_noreorder(r, &fb_items.rects, &fb_destroy_item);
    pthread_mutex_unlock(&fb_items_mutex);
}

void fb_rm_text(fb_img *i)
{
    fb_rm_img(i);
}

void fb_rm_img(fb_img *i)
{
    if(!i)
        return;

    pthread_mutex_lock(&fb_items_mutex);
    list_rm_noreorder(i, &fb_items.imgs, &fb_destroy_item);
    pthread_mutex_unlock(&fb_items_mutex);
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

    box->id = fb_generate_item_id();
    box->type = FB_IT_BOX;
    box->x = x;
    box->y = y;
    box->w = w;
    box->h = h;

    box->background[0] = fb_add_rect(x+SHADOW, y+SHADOW, w, h, GRAY); // shadow
    box->background[1] = fb_add_rect(x, y, w, h, WHITE); // border
    box->background[2] = fb_add_rect(x+BOX_BORDER, y+BOX_BORDER,
                                     w-BOX_BORDER*2, h-BOX_BORDER*2, bgcolor);

    pthread_mutex_lock(&fb_items_mutex);
    fb_items.msgbox = box;
    pthread_mutex_unlock(&fb_items_mutex);
    return box;
}

fb_img *fb_msgbox_add_text(int x, int y, int size, char *fmt, ...)
{
    char txt[512];
    txt[0] = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof(txt), fmt, ap);
    va_end(ap);

    fb_msgbox *box = fb_items.msgbox;

    fb_img *t = fb_text_create_item(x, y, WHITE, size, JUSTIFY_LEFT, txt);

    if(x == -1)
        t->x = box->w/2 - t->w/2;

    if(y == -1)
        t->y = box->h/2 - t->h/2;

    t->x += box->x;
    t->y += box->y;

    pthread_mutex_lock(&fb_items_mutex);
    list_add(t, &box->imgs);
    pthread_mutex_unlock(&fb_items_mutex);
    return t;
}

void fb_msgbox_rm_text(fb_img *text)
{
    if(!text)
        return;

    pthread_mutex_lock(&fb_items_mutex);
    if(fb_items.msgbox)
        list_rm_noreorder(text, &fb_items.msgbox->imgs, &fb_destroy_item);
    pthread_mutex_unlock(&fb_items_mutex);
}

void fb_destroy_msgbox(void)
{
    pthread_mutex_lock(&fb_items_mutex);
    if(!fb_items.msgbox)
    {
        pthread_mutex_unlock(&fb_items_mutex);
        return;
    }

    fb_msgbox *box = fb_items.msgbox;
    fb_items.msgbox = NULL;
    pthread_mutex_unlock(&fb_items_mutex);

    list_clear(&box->imgs, &fb_destroy_item);

    uint32_t i;
    for(i = 0; i < ARRAY_SIZE(box->background); ++i)
        fb_rm_rect(box->background[i]);

    free(box);
}

void fb_clear(void)
{
    pthread_mutex_lock(&fb_items_mutex);
    list_clear(&fb_items.rects, &fb_destroy_item);
    list_clear(&fb_items.imgs, &fb_destroy_item);
    pthread_mutex_unlock(&fb_items_mutex);

    fb_destroy_msgbox();

    fb_png_drop_unused();
    fb_text_drop_cache_unused();
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
#ifndef MR_DISABLE_ALPHA
 #if PIXEL_SIZE == 4
    int i;
    uint8_t *bits = (uint8_t*)fb.buffer;
    const int size = fb.vi.xres_virtual*fb.vi.yres;
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
    scanline_col32cb16blend_neon((uint16_t*)fb.buffer, &blend_clr, fb.size >> 1);
  #else
    const int size = fb.size >> 1;
    uint16_t *bits = fb.buffer;
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

static void fb_draw(void)
{
    uint32_t i;
    pthread_mutex_lock(&fb_items_mutex);

    fb_fill(BLACK);

    // rectangles
    for(i = 0; fb_items.rects && fb_items.rects[i]; ++i)
        fb_draw_rect(fb_items.rects[i]);

    // images
    for(i = 0; fb_items.imgs && fb_items.imgs[i]; ++i)
        fb_draw_img(fb_items.imgs[i]);

    // msg box
    if(fb_items.msgbox)
    {
        fb_draw_overlay();

        fb_msgbox *box = fb_items.msgbox;

        for(i = 0; i < ARRAY_SIZE(box->background); ++i)
            fb_draw_rect(box->background[i]);

        for(i = 0; box->imgs && box->imgs[i]; ++i)
            fb_draw_img(box->imgs[i]);
    }

    pthread_mutex_unlock(&fb_items_mutex);

    pthread_mutex_lock(&fb_update_mutex);
    fb_update();
    pthread_mutex_unlock(&fb_update_mutex);
}

void fb_freeze(int freeze)
{
    if(freeze)
        ++fb_frozen;
    else
        --fb_frozen;
}

void fb_push_context(void)
{
    fb_items_t *ctx = mzalloc(sizeof(fb_items_t));

    pthread_mutex_lock(&fb_items_mutex);

    list_move(&fb_items.rects, &ctx->rects);
    list_move(&fb_items.imgs, &ctx->imgs);
    ctx->msgbox = fb_items.msgbox;
    fb_items.msgbox = NULL;

    pthread_mutex_unlock(&fb_items_mutex);

    list_add(ctx, &inactive_ctx);
}

void fb_pop_context(void)
{
    if(!inactive_ctx)
        return;

    fb_clear();

    int idx = list_item_count(inactive_ctx)-1;
    fb_items_t *ctx = inactive_ctx[idx];

    pthread_mutex_lock(&fb_items_mutex);

    list_move(&ctx->rects, &fb_items.rects);
    list_move(&ctx->imgs, &fb_items.imgs);
    fb_items.msgbox = ctx->msgbox;

    pthread_mutex_unlock(&fb_items_mutex);

    list_rm_noreorder(ctx, &inactive_ctx, &free);

    fb_request_draw();
}

#define SLEEP_CONST 16
void *fb_draw_thread_work(void *cookie)
{
    struct timespec last, curr;
    uint32_t diff = 0, prevSleepTime = 0;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while(fb_draw_run)
    {
        clock_gettime(CLOCK_MONOTONIC, &curr);
        diff = timespec_diff(&last, &curr);

        if(__atomic_cmpxchg(1, 0, &fb_draw_requested) == 0)
        {
            fb_draw();
            __futex_wake(&fb_draw_futex, INT_MAX);
        }
#ifdef MR_CONTINUOUS_FB_UPDATE
        else
        {
            pthread_mutex_lock(&fb_update_mutex);
            fb_update();
            pthread_mutex_unlock(&fb_update_mutex);
        }
#endif

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
    if(!fb_frozen)
        __atomic_cmpxchg(0, 1, &fb_draw_requested);
}

void fb_force_draw(void)
{
    __atomic_swap(1, &fb_draw_requested);
    __futex_wait(&fb_draw_futex, 0, NULL);
}
