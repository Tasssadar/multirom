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
    fb->vi.vmode = FB_VMODE_NONINTERLACED;
    fb->vi.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi) < 0)
    {
        ERROR("failed to set fb0 vi info");
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        return -1;

    px_type *mapped = mmap(0, fb->fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

    if (mapped == MAP_FAILED)
        return -1;

    struct fb_generic_data *data = mzalloc(sizeof(struct fb_generic_data));
    data->mapped[0] = mapped;
    data->mapped[1] = (px_type*) (((uint8_t*)mapped) + (fb->vi.yres * fb->fi.line_length));

    fb->impl_data = data;

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

    .open = impl_open,
    .close = impl_close,
    .update = impl_update,
    .get_frame_dest = impl_get_frame_dest,
};
