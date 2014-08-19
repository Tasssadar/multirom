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

#ifdef MR_QCOM_OVERLAY_HEADER
#include MR_QCOM_OVERLAY_HEADER
#endif

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))

struct fb_qcom_overlay_data {
    int overlay_id;
    uint8_t *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
};

static int free_ion_mem(struct fb_qcom_overlay_data *data)
{
    int ret = 0;

    if (data->mem_buf)
        munmap(data->mem_buf, data->size);

    if (data->ion_fd >= 0)
    {
        ret = ioctl(data->ion_fd, ION_IOC_FREE, &data->handle_data);
        if (ret < 0)
            ERROR("free_mem failed ");
    }

    if (data->mem_fd >= 0)
        close(data->mem_fd);
    if (data->ion_fd >= 0)
        close(data->ion_fd);
    return 0;
}

static int alloc_ion_mem(struct fb_qcom_overlay_data *data, unsigned int size)
{
    int result;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;

    data->ion_fd = open("/dev/ion", O_RDWR|O_DSYNC|O_CLOEXEC);
    if (data->ion_fd < 0)
    {
        ERROR("ERROR: Can't open ion ");
        return -errno;
    }

    ionAllocData.flags = 0;
    ionAllocData.len = size;
    ionAllocData.align = sysconf(_SC_PAGESIZE);
    ionAllocData.heap_mask =
            ION_HEAP(ION_IOMMU_HEAP_ID) |
            ION_HEAP(21); // ION_SYSTEM_CONTIG_HEAP_ID

    result = ioctl(data->ion_fd, ION_IOC_ALLOC,  &ionAllocData);
    if(result)
    {
        ERROR("ION_IOC_ALLOC Failed ");
        close(data->ion_fd);
        return result;
    }

    fd_data.handle = ionAllocData.handle;
    data->handle_data.handle = ionAllocData.handle;
    result = ioctl(data->ion_fd, ION_IOC_MAP, &fd_data);
    if (result)
    {
        ERROR("ION_IOC_MAP Failed ");
        free_ion_mem(data);
        return result;
    }
    data->mem_buf = (uint8_t*)mmap(NULL, size, PROT_READ |
                PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    data->mem_fd = fd_data.fd;

    if (!data->mem_buf)
    {
        ERROR("ERROR: mem_buf MAP_FAILED ");
        free_ion_mem(data);
        return -ENOMEM;
    }

    return 0;
}


static int allocate_overlay(struct fb_qcom_overlay_data *data, int fd, int width, int height)
{
    struct mdp_overlay overlay;
    int ret = 0;

    memset(&overlay, 0 , sizeof (struct mdp_overlay));

    /* Fill Overlay Data */

    overlay.src.width  = ALIGN(width, 32);
    overlay.src.height = height;

#ifdef MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT
    overlay.src.format = MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT;
#else
    /* We can't set format here because their IDs are different on aosp and CM kernels.
     * There is literally just one line different between their headers, and it breaks it.
     * MDP_FB_FORMAT works because it translates to MDP_IMGTYPE2_START, which is the same
     * on both. It means it will take the format from the framebuffer. */
    overlay.src.format = MDP_FB_FORMAT;
#endif

    overlay.src_rect.w = width;
    overlay.src_rect.h = height;
    overlay.dst_rect.w = width;
    overlay.dst_rect.h = height;
    overlay.alpha = 0xFF;
    overlay.transp_mask = MDP_TRANSP_NOP;
    overlay.id = MSMFB_NEW_REQUEST;
    ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlay);
    if (ret < 0)
    {
        ERROR("Overlay Set Failed");
        return ret;
    }

    data->overlay_id = overlay.id;
    return 0;
}

static int free_overlay(struct fb_qcom_overlay_data *data, int fd)
{
    int ret = 0;
    struct mdp_display_commit ext_commit;

    if (data->overlay_id != MSMFB_NEW_REQUEST)
    {
        ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &data->overlay_id);
        if (ret)
        {
            ERROR("Overlay Unset Failed");
            data->overlay_id = MSMFB_NEW_REQUEST;
            return ret;
        }

        memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
        ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
        ext_commit.wait_for_finish = 1;
        ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
        if (ret < 0)
        {
            ERROR("ERROR: Clear MSMFB_DISPLAY_COMMIT failed!");
            data->overlay_id = MSMFB_NEW_REQUEST;
            return ret;
        }

        data->overlay_id = MSMFB_NEW_REQUEST;
    }
    return 0;
}

static int impl_open(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = mzalloc(sizeof(struct fb_qcom_overlay_data));
    data->overlay_id = MSMFB_NEW_REQUEST;

    if (alloc_ion_mem(data, fb->fi.line_length * fb->vi.yres) < 0)
        goto fail;

    if(allocate_overlay(data, fb->fd, fb->vi.xres, fb->vi.yres) < 0)
    {
        free_ion_mem(data);
        goto fail;
    }

    fb->impl_data = data;
    return 0;

fail:
    free(data);
    return -1;
}

static void impl_close(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = fb->impl_data;
    free_overlay(data, fb->fd);
    free_ion_mem(data);
    free(data);
    fb->impl_data = NULL;
}

static int impl_update(struct framebuffer *fb)
{
    int ret = 0;
    struct msmfb_overlay_data ovdata;
    struct mdp_display_commit ext_commit;
    struct fb_qcom_overlay_data *data = fb->impl_data;

    if (data->overlay_id == MSMFB_NEW_REQUEST)
    {
        ERROR("display_frame failed, no overlay\n");
        return -1;
    }

    memset(&ovdata, 0, sizeof(struct msmfb_overlay_data));

    ovdata.id = data->overlay_id;
    ovdata.data.flags = 0;
    ovdata.data.offset = 0;
    ovdata.data.memory_id = data->mem_fd;
    ret = ioctl(fb->fd, MSMFB_OVERLAY_PLAY, &ovdata);
    if (ret < 0)
    {
        ERROR("overlay_display_frame failed, overlay play Failed\n");
        return -1;
    }

    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fb->fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if (ret < 0)
    {
        ERROR("overlay_display_frame failed, overlay commit Failed\n!");
        return -1;
    }

    return 0;
}

static void *impl_get_frame_dest(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = fb->impl_data;
    return data->mem_buf;
}

const struct fb_impl fb_impl_qcom_overlay = {
    .name = "Qualcomm ION overlay",
    .impl_id = FB_IMPL_QCOM_OVERLAY,

    .open = impl_open,
    .close = impl_close,
    .update = impl_update,
    .get_frame_dest = impl_get_frame_dest,
};
