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
#include <png.h>
#include <math.h>

#include "log.h"
#include "framebuffer.h"
#include "util.h"
#include "containers.h"
#include "animation.h"
#include "listview.h"
#include "atomics.h"
#include "mrom_data.h"

#if PIXEL_SIZE == 4
#define fb_memset(dst, what, len) android_memset32(dst, what, len)
#else
#define fb_memset(dst, what, len) android_memset16(dst, what, len)
#endif


uint32_t fb_width = 0;
uint32_t fb_height = 0;
int fb_rotation = 0; // in degrees, clockwise

fb_item_pos DEFAULT_FB_PARENT = {
    .x = 0,
    .y = 0,
};

static struct framebuffer fb;
static int fb_frozen = 0;
static int fb_force_generic = 0;

static fb_context_t fb_ctx = {
    .first_item = NULL,
    .batch_started = 0,
    .background_color = BLACK,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static fb_context_t **inactive_ctx = NULL;
static uint8_t **fb_rot_helpers = NULL;
static pthread_t fb_draw_thread;
static pthread_mutex_t fb_update_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fb_draw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fb_draw_cond = PTHREAD_COND_INITIALIZER;
static atomic_int fb_draw_requested = ATOMIC_VAR_INIT(0);
static volatile int fb_draw_run = 0;
static void *fb_draw_thread_work(void*);

static void fb_destroy_item(void *item); // private!
static inline void fb_cpy_fb_with_rotation(px_type *dst, px_type *src);
static inline void fb_rotate_90deg(px_type *dst, px_type *src);
static inline void fb_rotate_270deg(px_type *dst, px_type *src);
static inline void fb_rotate_180deg(px_type *dst, px_type *src);

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

    fb.fd = open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC);
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

    DEFAULT_FB_PARENT.w = fb_width;
    DEFAULT_FB_PARENT.h = fb_height;

    fb_set_brightness(MULTIROM_DEFAULT_BRIGHTNESS);

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

void fb_set_brightness(int val)
{
#ifdef TW_BRIGHTNESS_PATH
    FILE *f = fopen(TW_BRIGHTNESS_PATH, "we");
    if(!f)
    {
        ERROR("Failed to set brightness: %s!\n", strerror(errno));
        return;
    }
    fprintf(f, "%d", val);
    fclose(f);
#endif
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
            *dst++ = *(--src);
        dst += padding;
    }
}

int fb_clone(char **buff)
{
    int len = fb.size;
    *buff = malloc(len);

    pthread_mutex_lock(&fb_update_mutex);
    memcpy(*buff, fb.buffer, len);
    pthread_mutex_unlock(&fb_update_mutex);

    return len;
}

void fb_fill(uint32_t color)
{
    fb_memset(fb.buffer, fb_convert_color(color), fb.size);
}

px_type fb_convert_color(uint32_t c)
{
#ifdef RECOVERY_BGRA
    return c;
#elif defined(RECOVERY_RGBX)
    return (c & 0xFF000000) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c & 0xFF0000) >> 16);
#elif defined(RECOVERY_ABGR)
    //             A              B                   G                  R
    return (c & 0xFF000000) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c & 0xFF0000) >> 16);
#elif defined(RECOVERY_RGB_565)
    const uint8_t alpha_pct = (((c >> 24) & 0xFF)*100) / 0xFF;
    //            R                                G                              B
    return (((c & 0xFF0000) >> 19) << 11) | (((c & 0xFF00) >> 10) << 5) | ((c & 0xFF) >> 3);
#else
#error "Unknown pixel format"
#endif
}

uint32_t fb_convert_color_img(uint32_t clr)
{
    uint32_t c = fb_convert_color(clr);
#if PIXEL_SIZE == 2
    const uint8_t alpha_pct = (((clr >> 24) & 0xFF)*100) / 0xFF;
    //      Alpha - RB                    Alpha - G
    c |= (((alpha_pct*31)/100) << 16) | (((alpha_pct*63)/100) << 24);
#endif
    return c;
}

void fb_set_background(uint32_t color)
{
    fb_ctx.background_color = color;
}

void fb_batch_start(void)
{
    pthread_mutex_lock(&fb_ctx.mutex);
    fb_ctx.batch_thread = pthread_self();
    fb_ctx.batch_started = 1;
}

void fb_batch_end(void)
{
    fb_ctx.batch_started = 0;
    pthread_mutex_unlock(&fb_ctx.mutex);
}

void fb_items_lock(void)
{
    if(!fb_ctx.batch_started || !pthread_equal(fb_ctx.batch_thread, pthread_self()))
        pthread_mutex_lock(&fb_ctx.mutex);
}

void fb_items_unlock(void)
{
    if(!fb_ctx.batch_started || !pthread_equal(fb_ctx.batch_thread, pthread_self()))
        pthread_mutex_unlock(&fb_ctx.mutex);
}

static void fb_ctx_put_it_before(fb_item_header *new_it, fb_item_header *next_it)
{
    if(next_it->prev)
    {
        next_it->prev->next = new_it;
        new_it->prev = next_it->prev;
    }
    new_it->next = next_it;
    next_it->prev = new_it;
}

static void fb_ctx_put_it_after(fb_item_header *new_it, fb_item_header *prev_it)
{
    if(prev_it->next)
    {
        prev_it->next->prev = new_it;
        new_it->next = prev_it->next;
    }
    new_it->prev = prev_it;
    prev_it->next = new_it;
}

void fb_ctx_add_item(void *item)
{
    fb_item_header *h = item;

    fb_items_lock();

    if(!fb_ctx.first_item)
        fb_ctx.first_item = item;
    else
    {
        fb_item_header *itr = fb_ctx.first_item;
        while(1)
        {
            if(itr->level > h->level)
            {
                if(itr == fb_ctx.first_item)
                    fb_ctx.first_item = h;
                fb_ctx_put_it_before(h, itr);
                itr = NULL;
                break;
            }

            if(itr->next)
                itr = itr->next;
            else
                break;
        }

        if(itr)
            fb_ctx_put_it_after(h, itr);
    }

    fb_items_unlock();
}

void fb_ctx_rm_item(void *item)
{
    fb_item_header *h = item;

    fb_items_lock();

    if(!h->prev)
        fb_ctx.first_item = h->next;
    else
        h->prev->next = h->next;

    if(h->next)
        h->next->prev = h->prev;

    fb_items_unlock();
}

void fb_remove_item(void *item)
{
    switch(((fb_item_header*)item)->type)
    {
        case FB_IT_RECT:
            fb_rm_rect((fb_rect*)item);
            break;
        case FB_IT_IMG:
            fb_rm_img((fb_img*)item);
            break;
        case FB_IT_LISTVIEW:
            listview_destroy((listview*)item);
            break;
        case FB_IT_LINE:
            fb_rm_line((fb_line*)item);
            break;
    }
}

void fb_destroy_item(void *item)
{
    anim_cancel_for(item, 0);

    switch(((fb_item_header*)item)->type)
    {
        case FB_IT_RECT:
        case FB_IT_LINE:
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

static inline void clamp_to_parent(void *it, int *min_x, int *max_x, int *min_y, int *max_y)
{
    fb_item_header *h = it;

    int parent_x = h->parent->x;
    int parent_y = h->parent->y;
    int parent_w = h->parent->w;
    int parent_h = h->parent->h;

    if(h->parent != &DEFAULT_FB_PARENT)
    {
        if(parent_x < 0)
        {
            parent_w += parent_x;
            parent_x = 0;
        }
        if(parent_y < 0)
        {
            parent_h += parent_y;
            parent_y = 0;
        }
        parent_w = imin(parent_x + parent_w, fb_width) - parent_x;
        parent_h = imin(parent_y + parent_h, fb_height) - parent_y;
    }

    *min_x = h->x >= parent_x ? 0 : parent_x - h->x;
    *min_y = h->y >= parent_y ? 0 : parent_y - h->y;
    *max_x = imin(h->w, parent_x + parent_w - h->x);
    *max_y = imin(h->h, parent_y + parent_h - h->y);
}

void fb_draw_rect(fb_rect *r)
{
    const uint8_t alpha = (r->color >> 24) & 0xFF;
    const uint8_t inv_alpha = 0xFF - ((r->color >> 24) & 0xFF);
    const px_type color = fb_convert_color(r->color);

    if(alpha == 0)
        return;

#if defined(RECOVERY_RGBX) || defined(RECOVERY_ABGR)
    const uint32_t premult_color_rb = ((color & 0xFF00FF) * (alpha)) >> 8;
    const uint32_t premult_color_g = ((color & 0x00FF00) * (alpha)) >> 8;
#elif defined(RECOVERY_BGRA)
    const uint32_t premult_color_rb = (((color >> 8) & 0xFF00FF) * (alpha)) >> 8;
    const uint32_t premult_color_g = (((color >> 8) & 0x00FF00) * (alpha)) >> 8;
#elif defined(RECOVERY_RGB_565)
    const uint8_t alpha5b = (alpha >> 3) + 1;
    const uint8_t alpha6b = (alpha >> 2) + 1;
    const uint8_t inv_alpha5b = 32 - alpha5b;
    const uint8_t inv_alpha6b = 64 - alpha6b;
    const uint16_t premult_color_rb = ((color & 0xF81F) * alpha5b) >> 5;
    const uint16_t premult_color_g = ((color & 0x7E0) * alpha6b) >> 6;
#endif

    int min_x, max_x, min_y, max_y;
    clamp_to_parent(r, &min_x, &max_x, &min_y, &max_y);
    const int rendered_w = max_x - min_x;

    if(rendered_w <= 0)
        return;

    const int w = rendered_w*PIXEL_SIZE;

    px_type *bits = fb.buffer + (fb.stride*(r->y + min_y)) + r->x + min_x;

    int i, x;
    uint8_t *comps_bits;
    const uint8_t *comps_clr = (uint8_t*)&color;
    for(i = min_y; i < max_y; ++i)
    {
        if(alpha == 0xFF)
        {
            fb_memset(bits, color, w);
            bits += fb.stride;
        }
        // Do the blending
        else
        {
#ifdef MR_DISABLE_ALPHA
            fb_memset(bits, color, w);
            bits += fb.stride;
#else
            for(x = 0; x < rendered_w; ++x)
            {
  #if defined(RECOVERY_RGBX) || defined(RECOVERY_ABGR)
                const uint32_t rb = (premult_color_rb & 0xFF00FF) + ((inv_alpha * (*bits & 0xFF00FF)) >> 8);
                const uint32_t g = (premult_color_g & 0x00FF00) + ((inv_alpha * (*bits & 0x00FF00)) >> 8);
                *bits = 0xFF000000 | (rb & 0xFF00FF) | (g & 0x00FF00);
  #elif defined(RECOVERY_BGRA)
                const uint32_t rb = (premult_color_rb & 0xFF00FF) + ((inv_alpha * ((*bits >> 8) & 0xFF00FF)) >> 8);
                const uint32_t g = (premult_color_g & 0x00FF00) + ((inv_alpha * ((*bits >> 8) & 0x00FF00)) >> 8);
                *bits = 0xFF000000 | (rb & 0xFF00FF) | (g & 0x00FF00);
  #elif defined(RECOVERY_RGB_565)
                const uint16_t rb = (premult_color_rb & 0xF81F) + ((inv_alpha5b * (*bits & 0xF81F)) >> 5);
                const uint16_t g = (premult_color_g & 0x7E0) + ((inv_alpha6b * (*bits & 0x7E0)) >> 6);
                *bits = (rb & 0xF81F) | (g & 0x7E0);
  #else
    #error "No alpha blending implementation for this format!"
  #endif
                ++bits;
            }
            bits += fb.stride - rendered_w;
#endif // MR_DISABLE_ALPHA
        }
    }
}

static inline int blend_png(int value1, int value2, int alpha) {
    int r = (0xFF-alpha)*value1 + alpha*value2;
    return (r+1 + (r >> 8)) >> 8; // divide by 255
}

void fb_draw_img(fb_img *i)
{
    int y, x;
    const int w = i->w*PIXEL_SIZE;
    uint8_t alpha;
    uint8_t *comps_img, *comps_bits;

#if PIXEL_SIZE == 4
    const uint8_t max_alpha = 0xFF;
#elif PIXEL_SIZE == 2
    const uint8_t max_alpha = 31;
#endif

    int min_x, max_x, min_y, max_y;
    clamp_to_parent(i, &min_x, &max_x, &min_y, &max_y);
    const int rendered_w = max_x - min_x;

    if(rendered_w <= 0)
        return;

    px_type *bits = fb.buffer + (fb.stride*(i->y + min_y)) + i->x + min_x;
    px_type *img = (px_type*)(((uint32_t*)i->data) + (min_y * i->w) + min_x);

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

// from http://members.chello.at/~easyfilter/bresenham.html
void fb_draw_line(fb_line *l)
{
    const px_type px = fb_convert_color(l->color);

    int x0 = imin(imax(l->x, l->parent->x), l->parent->x + l->parent->w);
    int x1 = imin(imax(l->x2, l->parent->x), l->parent->x + l->parent->w);
    int y0 = imin(imax(l->y, l->parent->y), l->parent->y + l->parent->h);
    int y1 = imin(imax(l->y2, l->parent->y), l->parent->y + l->parent->h);

    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;

    int err;
    double e2 = sqrt((double)(dx*dx) + (double)(dy*dy));

    if(e2 == 0.0)
        return;

    dx *= 255/e2;
    dy *= 255/e2;
    double th = 255*((double)(l->thickness - 1));

    if(dx < dy)
    {
        x1 = (e2+th/2) / dy;
        err = x1*dy - th/2;
        for(x0 -= x1*sx; ; y0 += sy)
        {
            x1 = x0;
            for(e2 = dy-err-th; e2+dy < 255; e2 += dy)
            {
                x1 += sx;
                *(fb.buffer + fb.stride*y0 + x1) = px;
            }
            if(y0 == y1)
                break;
            err += dx;
            if(err > 255)
            {
                err -= dy;
                x0 += sx;
            }
        }
    }
    else
    {
        y1 = (e2 + th/2) / dx;
        err = y1*dx - th/2;
        for(y0 -= y1*sy; ; x0 += sx)
        {
            y1 = y0;
            for(e2 = dx - err - th; e2+dx < 255; e2 += dx)
            {
                y1 += sy;
                *(fb.buffer + fb.stride*y1 + x0) = px;
            }

            if(x0 == x1)
                break;
            err += dy;
            if(err > 255)
            {
                err -= dx;
                y0 += sy;
            }
        }
    }
}

int fb_generate_item_id(void)
{
    fb_items_lock();
    static int id = 0;
    int res = id++;
    fb_items_unlock();

    return res;
}

fb_rect *fb_add_rect_lvl(int level, int x, int y, int w, int h, uint32_t color)
{
    fb_rect *r = mzalloc(sizeof(fb_rect));
    r->id = fb_generate_item_id();
    r->type = FB_IT_RECT;
    r->parent = &DEFAULT_FB_PARENT;
    r->level = level;

    r->x = x;
    r->y = y;

    r->w = w;
    r->h = h;
    r->color = color;

    fb_ctx_add_item(r);
    return r;
}

void fb_add_rect_notfilled(int level, int x, int y, int w, int h, uint32_t color, int thickness, fb_rect ***list)
{
    fb_rect *r;
    // top
    r = fb_add_rect_lvl(level, x, y, w, thickness, color);
    list_add(list, r);

    // right
    r = fb_add_rect_lvl(level, x + w - thickness, y, thickness, h, color);
    list_add(list, r);

    // bottom
    r = fb_add_rect_lvl(level, x, y + h - thickness, w, thickness, color);
    list_add(list, r);

    // left
    r = fb_add_rect_lvl(level, x, y, thickness, h, color);
    list_add(list, r);
}

fb_img *fb_add_img(int level, int x, int y, int w, int h, int img_type, px_type *data)
{
    fb_img *result = mzalloc(sizeof(fb_img));
    result->id = fb_generate_item_id();
    result->type = FB_IT_IMG;
    result->parent = &DEFAULT_FB_PARENT;
    result->level = level;
    result->x = x;
    result->y = y;
    result->img_type = img_type;
    result->data = data;
    result->w = w;
    result->h = h;

    fb_ctx_add_item(result);
    return result;
}

fb_img* fb_add_png_img_lvl(int level, int x, int y, int w, int h, const char *path)
{
    px_type *data = NULL;
    if(strncmp(path, ":/", 2) == 0)
    {
        const int full_path_len = strlen(path) + strlen(mrom_dir()) + 4;
        char *full_path = malloc(full_path_len);
        snprintf(full_path, full_path_len, "%s/res%s", mrom_dir(), path+1);
        data = fb_png_get(full_path, w, h);
        free(full_path);
    }
    else
        data = fb_png_get(path, w, h);
    if(!data)
        return NULL;

    return fb_add_img(level, x, y, w, h, FB_IMG_TYPE_PNG, data);
}

fb_circle *fb_add_circle_lvl(int level, int x, int y, int radius, uint32_t color)
{
    const int diameter = radius*2 + 1;
    uint32_t *data = mzalloc(diameter * diameter * 4);
    uint32_t px = fb_convert_color_img(color);

    int rx, ry;
    const int radius_check = radius*radius + radius*0.8;

    for(ry = -radius; ry <= radius; ++ry)
        for(rx = -radius; rx <= radius; ++rx)
            if(rx*rx+ry*ry <= radius_check)
                *(data + diameter*(radius + ry) + (radius+rx)) = px;

    return fb_add_img(level, x, y, diameter, diameter, FB_IMG_TYPE_GENERIC, data);
}

fb_line *fb_add_line_lvl(int level, int x1, int y1, int x2, int y2, int thickness, uint32_t color)
{
    fb_line *res = mzalloc(sizeof(fb_line));
    res->id = fb_generate_item_id();
    res->type = FB_IT_LINE;
    res->parent = &DEFAULT_FB_PARENT;
    res->level = level;
    res->x = x1;
    res->y = y1;
    res->x2 = x2;
    res->y2 = y2;
    res->thickness = thickness;
    res->color = color;
    fb_ctx_add_item(res);
    return res;
}

void fb_rm_rect(fb_rect *r)
{
    if(!r)
        return;

    fb_ctx_rm_item(r);
    fb_destroy_item(r);
}

void fb_rm_text(fb_img *i)
{
    fb_rm_img(i);
}

void fb_rm_img(fb_img *i)
{
    if(!i)
        return;

    fb_ctx_rm_item(i);
    fb_destroy_item(i);
}

void fb_rm_circle(fb_circle *c)
{
    fb_rm_img(c);
}

void fb_rm_line(fb_line *l)
{
    if(!l)
        return;

    fb_ctx_rm_item(l);
    fb_destroy_item(l);
}

void fb_clear(void)
{
    pthread_mutex_lock(&fb_ctx.mutex);
    fb_item_header *it, *next;
    for(it = fb_ctx.first_item; it; it = next)
    {
        next = it->next;
        fb_destroy_item(it);
    }
    fb_ctx.first_item = NULL;
    pthread_mutex_unlock(&fb_ctx.mutex);

    fb_png_drop_unused();
    fb_text_drop_cache_unused();
}

static void fb_draw(void)
{
    uint32_t i;
    fb_item_header *it;

    fb_fill(fb_ctx.background_color);

    fb_batch_start();
    for(it = fb_ctx.first_item; it; it = it->next)
    {
        switch(it->type)
        {
            case FB_IT_RECT:
                fb_draw_rect((fb_rect*)it);
                break;
            case FB_IT_IMG:
                fb_draw_img((fb_img*)it);
                break;
            case FB_IT_LISTVIEW:
                listview_update_ui_args((listview*)it, 1, 1);
                break;
            case FB_IT_LINE:
                fb_draw_line((fb_line*)it);
                break;
        }
    }
    fb_batch_end();

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

    // wait for last draw to finish or prevent new draw
    if(fb_frozen == 1)
    {
        atomic_int expected = ATOMIC_VAR_INIT(1);
        pthread_mutex_lock(&fb_draw_mutex);
        int res = atomic_compare_exchange_strong(&fb_draw_requested, &expected, 0);
        pthread_mutex_unlock(&fb_draw_mutex);
    }
}

void fb_push_context(void)
{
    fb_context_t *ctx = mzalloc(sizeof(fb_context_t));

    pthread_mutex_lock(&fb_ctx.mutex);
    ctx->first_item = fb_ctx.first_item;
    ctx->background_color = fb_ctx.background_color;
    fb_ctx.first_item = NULL;
    pthread_mutex_unlock(&fb_ctx.mutex);

    list_add(&inactive_ctx, ctx);
}

void fb_pop_context(void)
{
    if(!inactive_ctx)
        return;

    fb_clear();

    int idx = list_item_count(inactive_ctx)-1;
    fb_context_t *ctx = inactive_ctx[idx];

    pthread_mutex_lock(&fb_ctx.mutex);
    fb_ctx.first_item = ctx->first_item;
    fb_ctx.background_color = ctx->background_color;
    pthread_mutex_unlock(&fb_ctx.mutex);

    list_rm_noreorder(&inactive_ctx, ctx, &free);

    fb_request_draw();
}

#define SLEEP_CONST 16
void *fb_draw_thread_work(UNUSED void *cookie)
{
    struct timespec last, curr;
    uint32_t diff = 0, prevSleepTime = 0;
    clock_gettime(CLOCK_MONOTONIC, &last);

    atomic_int expected = ATOMIC_VAR_INIT(1);

    while(fb_draw_run)
    {
        clock_gettime(CLOCK_MONOTONIC, &curr);
        diff = timespec_diff(&last, &curr);

        expected.__val = 1; // might be reseted by atomic_compare_exchange_strong
        pthread_mutex_lock(&fb_draw_mutex);
        if(atomic_compare_exchange_strong(&fb_draw_requested, &expected, 0))
        {
            fb_draw();
            pthread_cond_broadcast(&fb_draw_cond);
            pthread_mutex_unlock(&fb_draw_mutex);
        }
        else
        {
            pthread_mutex_unlock(&fb_draw_mutex);
#ifdef MR_CONTINUOUS_FB_UPDATE
            pthread_mutex_lock(&fb_update_mutex);
            fb_update();
            pthread_mutex_unlock(&fb_update_mutex);
#endif
        }


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
    {
        atomic_int expected = ATOMIC_VAR_INIT(0);
        atomic_compare_exchange_strong(&fb_draw_requested, &expected, 1);
    }
}

void fb_force_draw(void)
{
    atomic_int expected = ATOMIC_VAR_INIT(0);

    pthread_mutex_lock(&fb_draw_mutex);
    atomic_compare_exchange_strong(&fb_draw_requested, &expected, 1);
    pthread_cond_wait(&fb_draw_cond, &fb_draw_mutex);
    pthread_mutex_unlock(&fb_draw_mutex);
}

int fb_save_screenshot(void)
{
    char *r;
    int c, media_rw_id;
    char dir[256];
    char path[256];

    strcpy(dir, mrom_dir());
    r = strrchr(dir, '/');
    if(!r)
    {
        ERROR("Failed to determine path to save a screenshot!\n");
        return -1;
    }
    *r = 0;
    strcat(dir, "/Pictures/Screenshots");
    mkdir_recursive_with_perms(path, 0775, "media_rw", "media_rw");

    for(c = 0; c < 999; ++c)
    {
        snprintf(path, sizeof(path), "%s/mrom_screenshot_%03d.png", dir, c);
        if(access(path, F_OK) < 0)
            break;
    }

    pthread_mutex_lock(&fb_draw_mutex);
    if(fb_png_save_img(path, fb_width, fb_height, fb.stride, fb.buffer) >= 0)
    {
        media_rw_id = decode_uid("media_rw");
        if(media_rw_id != -1)
            chown(path, (uid_t)media_rw_id, (gid_t)media_rw_id);
        chmod(path, 0664);

        INFO("Screenshot saved to %s\n", path);

        fb_fill(WHITE);
        pthread_mutex_lock(&fb_update_mutex);
        fb_update();
        usleep(100000);
        pthread_mutex_unlock(&fb_update_mutex);
        pthread_mutex_unlock(&fb_draw_mutex);

        fb_request_draw();
        return 0;
    }
    else
    {
        pthread_mutex_unlock(&fb_draw_mutex);
        ERROR("Failed to take screenshot!\n");
        return -1;
    }
}
