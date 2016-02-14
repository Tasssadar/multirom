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
#include <string.h>
#include <linux/fb.h>

#include "framebuffer.h"
#include "log.h"
#include "util.h"

// only double-buffering is implemented, this define is just
// for the code to know how many buffers we use
#define NUM_BUFFERS 2

struct fb_generic_data {
    px_type *mapped[NUM_BUFFERS];
    int active_buff;
};

static int impl_open(struct framebuffer *fb)
{
    fb->vi.bits_per_pixel = PIXEL_SIZE * 8;
    INFO("Pixel format: %dx%d @ %dbpp\n", fb->vi.xres, fb->vi.yres, fb->vi.bits_per_pixel);

#ifdef RECOVERY_BGRA
    INFO("Pixel format: BGRA_8888\n");
    fb->vi.red.offset     = 8;
    fb->vi.red.length     = 8;
    fb->vi.green.offset   = 16;
    fb->vi.green.length   = 8;
    fb->vi.blue.offset    = 24;
    fb->vi.blue.length    = 8;
    fb->vi.transp.offset  = 0;
    fb->vi.transp.length  = 8;
#elif  defined(RECOVERY_RGBX)
    INFO("Pixel format: RGBX_8888\n");
    fb->vi.red.offset     = 24;
    fb->vi.red.length     = 8;
    fb->vi.green.offset   = 16;
    fb->vi.green.length   = 8;
    fb->vi.blue.offset    = 8;
    fb->vi.blue.length    = 8;
    fb->vi.transp.offset  = 0;
    fb->vi.transp.length  = 8;
#elif defined(RECOVERY_RGB_565)
    INFO("Pixel format: RGB_565\n");
    fb->vi.blue.offset    = 0;
    fb->vi.green.offset   = 5;
    fb->vi.red.offset     = 11;
    fb->vi.blue.length    = 5;
    fb->vi.green.length   = 6;
    fb->vi.red.length     = 5;
    fb->vi.blue.msb_right = 0;
    fb->vi.green.msb_right = 0;
    fb->vi.red.msb_right = 0;
    fb->vi.transp.offset  = 0;
    fb->vi.transp.length  = 0;
#elif defined(RECOVERY_ABGR)
    INFO("Pixel format: ABGR_8888\n");
    fb->vi.red.offset     = 0;
    fb->vi.red.length     = 8;
    fb->vi.green.offset   = 8;
    fb->vi.green.length   = 8;
    fb->vi.blue.offset    = 16;
    fb->vi.blue.length    = 8;
    fb->vi.transp.offset  = 24;
    fb->vi.transp.length  = 8;
#else
#error "Unknown pixel format"
#endif

    fb->vi.vmode = FB_VMODE_NONINTERLACED;
    fb->vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    // mmap and memset to 0 before setting the vi to prevent screen flickering during init
    px_type *mapped = mmap(0, fb->fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

    if (mapped == MAP_FAILED)
        return -1;

    memset(mapped, 0, fb->fi.smem_len);
    munmap(mapped, fb->fi.smem_len);

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0)
    {
        ERROR("failed to set fb0 vi info");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        return -1;

    mapped = mmap(0, fb->fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

    if (mapped == MAP_FAILED)
        return -1;

    struct fb_generic_data *data = mzalloc(sizeof(struct fb_generic_data));
    data->mapped[0] = mapped;
    data->mapped[1] = (px_type*) (((uint8_t*)mapped) + (fb->vi.yres * fb->fi.line_length));

    fb->impl_data = data;

#ifdef TW_SCREEN_BLANK_ON_BOOT
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_POWERDOWN);
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK);
#endif

    return 0;
}

static void impl_close(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;
    if(data)
    {
        munmap(data->mapped[0], fb->fi.smem_len);
        free(data);
        fb->impl_data = NULL;
    }
}

static int impl_update(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;

    fb->vi.yres_virtual = fb->vi.yres * NUM_BUFFERS;
    fb->vi.yoffset = data->active_buff * fb->vi.yres;

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0)
    {
        ERROR("active fb swap failed");
        return -1;
    }

    return 0;
}

static void *impl_get_frame_dest(struct framebuffer *fb)
{
    struct fb_generic_data *data = fb->impl_data;
    data->active_buff = !data->active_buff;
    return data->mapped[data->active_buff];
}

const struct fb_impl fb_impl_generic = {
    .name = "Generic",
    .impl_id = FB_IMPL_GENERIC,

    .open = impl_open,
    .close = impl_close,
    .update = impl_update,
    .get_frame_dest = impl_get_frame_dest,
};
