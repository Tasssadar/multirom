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
#define MAX_DISPLAY_DIM  2048

struct fb_qcom_overlay_data {
    uint8_t *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    struct ion_handle_data handle_data;
    int overlayL_id;
    int overlayR_id;
    int leftSplit;
    int rightSplit;
    int width;
};

static int map_mdp_pixel_format()
{
    int format;
#ifdef MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT
    format = MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT;
#else
    /* We can't set format here because their IDs are different on aosp and CM kernels.
     * There is literally just one line different between their headers, and it breaks it.
     * MDP_FB_FORMAT works because it translates to MDP_IMGTYPE2_START, which is the same
     * on both. It means it will take the format from the framebuffer. */
    format = MDP_FB_FORMAT;
#endif
    return format;
}

static void setDisplaySplit(struct fb_qcom_overlay_data *data)
{
    char split[64] = { 0 };
    FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
    if(fp)
    {
        //Format "left right" space as delimiter
        if(fread(split, sizeof(char), 64, fp))
        {
            data->leftSplit = atoi(split);
            INFO("Left Split=%d\n",data->leftSplit);
            char *rght = strpbrk(split, " ");
            if(rght)
                data->rightSplit = atoi(rght + 1);
            INFO("Right Split=%d\n", data->rightSplit);
        }
        fclose(fp);
    }
}

static int isDisplaySplit(struct fb_qcom_overlay_data *data)
{
    if(data->width > MAX_DISPLAY_DIM)
        return 1;

    //check if right split is set by driver
    if(data->rightSplit)
        return 1;

    return 0;
}

static int free_ion_mem(struct fb_qcom_overlay_data *data)
{
    int ret = 0;

    if(data->mem_buf)
        munmap(data->mem_buf, data->size);

    if(data->ion_fd >= 0)
    {
        ret = ioctl(data->ion_fd, ION_IOC_FREE, &data->handle_data);
        if(ret < 0)
            ERROR("free_mem failed ");
    }

    if(data->mem_fd >= 0)
        close(data->mem_fd);
    if(data->ion_fd >= 0)
        close(data->ion_fd);
    return 0;
}

static int alloc_ion_mem(struct fb_qcom_overlay_data *data, unsigned int size)
{
    int result;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;

    data->ion_fd = open("/dev/ion", O_RDWR|O_DSYNC|O_CLOEXEC);
    if(data->ion_fd < 0)
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
    if(result)
    {
        ERROR("ION_IOC_MAP Failed ");
        free_ion_mem(data);
        return result;
    }
    data->mem_buf = (uint8_t*)mmap(NULL, size, PROT_READ |
                PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    data->mem_fd = fd_data.fd;

    if(!data->mem_buf)
    {
        ERROR("ERROR: mem_buf MAP_FAILED ");
        free_ion_mem(data);
        return -ENOMEM;
    }

    return 0;
}


static int allocate_overlay(struct fb_qcom_overlay_data *data, int fd, int width, int height)
{
    int ret = 0;

    if(!isDisplaySplit(data))
    {
        // Check if overlay is already allocated
        if(data->overlayL_id == MSMFB_NEW_REQUEST)
        {
            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill Overlay Data */
            overlayL.src.width  = ALIGN(width, 32);
            overlayL.src.height = height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.w = width;
            overlayL.src_rect.h = height;
            overlayL.dst_rect.w = width;
            overlayL.dst_rect.h = height;
            overlayL.alpha = 0xFF;
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if(ret < 0)
            {
                ERROR("Overlay Set Failed");
                return ret;
            }
            data->overlayL_id = overlayL.id;
        }
    }
    else
    {
        float xres = data->width;
        int lSplit = data->leftSplit;
        float lSplitRatio = lSplit / xres;
        float lCropWidth = width * lSplitRatio;
        int lWidth = lSplit;
        int rWidth = width - lSplit;

        if(data->overlayL_id == MSMFB_NEW_REQUEST)
        {
            struct mdp_overlay overlayL;

            memset(&overlayL, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayL Data */
            overlayL.src.width  = ALIGN(width, 32);
            overlayL.src.height = height;
            overlayL.src.format = map_mdp_pixel_format();
            overlayL.src_rect.x = 0;
            overlayL.src_rect.y = 0;
            overlayL.src_rect.w = lCropWidth;
            overlayL.src_rect.h = height;
            overlayL.dst_rect.x = 0;
            overlayL.dst_rect.y = 0;
            overlayL.dst_rect.w = lWidth;
            overlayL.dst_rect.h = height;
            overlayL.alpha = 0xFF;
            overlayL.transp_mask = MDP_TRANSP_NOP;
            overlayL.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayL);
            if(ret < 0)
            {
                ERROR("OverlayL Set Failed");
                return ret;
            }
            data->overlayL_id = overlayL.id;
        }

        if(data->overlayR_id == MSMFB_NEW_REQUEST)
        {
            struct mdp_overlay overlayR;

            memset(&overlayR, 0 , sizeof (struct mdp_overlay));

            /* Fill OverlayR Data */
            overlayR.src.width  = ALIGN(width, 32);
            overlayR.src.height = height;
            overlayR.src.format = map_mdp_pixel_format();
            overlayR.src_rect.x = lCropWidth;
            overlayR.src_rect.y = 0;
            overlayR.src_rect.w = width - lCropWidth;
            overlayR.src_rect.h = height;
            overlayR.dst_rect.x = 0;
            overlayR.dst_rect.y = 0;
            overlayR.dst_rect.w = rWidth;
            overlayR.dst_rect.h = height;
            overlayR.alpha = 0xFF;
            overlayR.flags = MDSS_MDP_RIGHT_MIXER;
            overlayR.transp_mask = MDP_TRANSP_NOP;
            overlayR.id = MSMFB_NEW_REQUEST;
            ret = ioctl(fd, MSMFB_OVERLAY_SET, &overlayR);
            if(ret < 0) 
            {
                ERROR("OverlayR Set Failed");
                return ret;
            }
            data->overlayR_id = overlayR.id;
        }
    }

    return 0;
}

static int free_overlay(struct fb_qcom_overlay_data *data, int fd)
{
    int ret = 0;
    struct mdp_display_commit ext_commit;

    if(!isDisplaySplit(data))
    {
        if(data->overlayL_id != MSMFB_NEW_REQUEST)
        {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &data->overlayL_id);
            if(ret)
            {
                ERROR("OverlayL Unset Failed");
                data->overlayL_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }
    }
    else
    {
        if(data->overlayL_id != MSMFB_NEW_REQUEST)
        {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &data->overlayL_id);
            if(ret)
            {
                ERROR("OverlayL Unset Failed");
                data->overlayL_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }

        if(data->overlayR_id != MSMFB_NEW_REQUEST)
        {
            ret = ioctl(fd, MSMFB_OVERLAY_UNSET, &data->overlayR_id);
            if(ret)
            {
                ERROR("OverlayR Unset Failed");
                data->overlayR_id = MSMFB_NEW_REQUEST;
                return ret;
            }
        }
    }

    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    
    data->overlayL_id = MSMFB_NEW_REQUEST;
    data->overlayR_id = MSMFB_NEW_REQUEST;

    ret = ioctl(fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if(ret < 0)
    {
        ERROR("Clear MSMFB_DISPLAY_COMMIT failed!");   
        return ret;
    }

    return 0;
}

static int impl_open(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = mzalloc(sizeof(struct fb_qcom_overlay_data));
    data->overlayL_id = MSMFB_NEW_REQUEST;
    data->overlayR_id = MSMFB_NEW_REQUEST;
    data->width = fb->vi.xres;
    data->leftSplit = data->width / 2;

    setDisplaySplit(data);

    if(alloc_ion_mem(data, fb->fi.line_length * fb->vi.yres) < 0)
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
    struct msmfb_overlay_data ovdataL, ovdataR;
    struct mdp_display_commit ext_commit;
    struct fb_qcom_overlay_data *data = fb->impl_data;

    if(!isDisplaySplit(data))
    {
        if(data->overlayL_id == MSMFB_NEW_REQUEST)
        {
            ERROR("display_frame failed, no overlay\n");
            return -EINVAL;
        }

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = data->overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = data->mem_fd;
        ret = ioctl(fb->fd, MSMFB_OVERLAY_PLAY, &ovdataL);
        if(ret < 0)
        {
            ERROR("overlay_display_frame failed, overlay play Failed\n");
            return -1;
        }
    }
    else
    {
        if(data->overlayL_id == MSMFB_NEW_REQUEST)
        {
            ERROR("display_frame failed, no overlayL \n");
            return -EINVAL;
        }

        memset(&ovdataL, 0, sizeof(struct msmfb_overlay_data));

        ovdataL.id = data->overlayL_id;
        ovdataL.data.flags = 0;
        ovdataL.data.offset = 0;
        ovdataL.data.memory_id = data->mem_fd;
        ret = ioctl(fb->fd, MSMFB_OVERLAY_PLAY, &ovdataL);
        if(ret < 0)
        {
            ERROR("overlay_display_frame failed, overlayL play Failed\n");
            return ret;
        }

        if(data->overlayR_id == MSMFB_NEW_REQUEST)
        {
            ERROR("display_frame failed, no overlayR \n");
            return -EINVAL;
        }

        memset(&ovdataR, 0, sizeof(struct msmfb_overlay_data));

        ovdataR.id = data->overlayR_id;
        ovdataR.data.flags = 0;
        ovdataR.data.offset = 0;
        ovdataR.data.memory_id = data->mem_fd;
        ret = ioctl(fb->fd, MSMFB_OVERLAY_PLAY, &ovdataR);
        if(ret < 0)
        {
            ERROR("overlay_display_frame failed, overlayR play Failed\n");
            return ret;
        }
    }

    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    ext_commit.wait_for_finish = 1;
    ret = ioctl(fb->fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if(ret < 0)
    {
        ERROR("overlay_display_frame failed, overlay commit Failed\n!");
        return -1;
    }

    return ret;
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
