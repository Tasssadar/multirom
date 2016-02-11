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
#include <poll.h>
#include <errno.h>

#include "framebuffer.h"
#include "log.h"
#include "util.h"

#ifdef MR_QCOM_OVERLAY_HEADER
#include MR_QCOM_OVERLAY_HEADER
#endif

#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define MAX_DISPLAY_DIM  2048
#define NUM_BUFFERS 3

struct fb_qcom_overlay_mem_info {
    uint8_t *mem_buf;
    int size;
    int ion_fd;
    int mem_fd;
    int offset;
    struct ion_handle_data handle_data;
};

struct fb_qcom_vsync {
    int fb_fd;
    int enabled;
    volatile int _run_thread;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct timespec time;
};

struct fb_qcom_overlay_data {
    struct fb_qcom_overlay_mem_info mem_info[NUM_BUFFERS];
    struct fb_qcom_vsync *vsync;
    int active_mem;
    int overlayL_id;
    int overlayR_id;
    int leftSplit;
    int rightSplit;
    int width;
};

#define VSYNC_PREFIX "VSYNC="

#ifdef MR_QCOM_OVERLAY_USE_VSYNC
static int fb_qcom_vsync_enable(struct fb_qcom_vsync *vs, int enable)
{
    clock_gettime(CLOCK_MONOTONIC, &vs->time);

    if(vs->enabled != enable)
    {
        if(vs->fb_fd < 0 || ioctl(vs->fb_fd, MSMFB_OVERLAY_VSYNC_CTRL, &enable) < 0)
        {
            ERROR("Failed to set vsync status\n");
            return -1;
        }

        vs->enabled = enable;
    }

    return 0;
}

static void *fb_qcom_vsync_thread_work(void *data)
{
    struct fb_qcom_vsync *vs = data;
    int fd, err, len;
    struct pollfd pfd;
    struct timespec now;
    uint64_t vsync_timestamp;
    uint64_t now_timestamp;
    char buff[64];

    fd = open("/sys/class/graphics/fb0/vsync_event", O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        ERROR("Unable to open vsync_event!\n");
        return NULL;
    }

    read(fd, buff, sizeof(buff));
    pfd.fd = fd;
    pfd.events = POLLPRI | POLLERR;

    while(vs->_run_thread)
    {
        err = poll(&pfd, 1, 10);
        if(err <= 0)
            continue;

        if(pfd.revents & POLLPRI)
        {
            len = pread(pfd.fd, buff, sizeof(buff)-1, 0);
            if(len > 0)
            {
                buff[len] = 0;
                if(strncmp(buff, VSYNC_PREFIX, strlen(VSYNC_PREFIX)) == 0)
                {
                    vsync_timestamp = strtoull(buff + strlen(VSYNC_PREFIX), NULL, 10);
                    clock_gettime(CLOCK_MONOTONIC, &now);
                    now_timestamp = ((uint64_t)now.tv_sec) * 1000000000ULL + now.tv_nsec;
                    if(vsync_timestamp > now_timestamp)
                        usleep((vsync_timestamp - now_timestamp)/1000);

                    pthread_cond_signal(&vs->cond);
                }
            }
            else
                ERROR("Unable to read from vsync_event!");
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if(timespec_diff(&vs->time, &now) >= 60)
            fb_qcom_vsync_enable(vs, 0);
    }

    close(fd);
    return NULL;
}
#endif // #ifdef MR_QCOM_OVERLAY_USE_VSYNC

static struct fb_qcom_vsync *fb_qcom_vsync_init(int fb_fd)
{
    struct fb_qcom_vsync *res = mzalloc(sizeof(struct fb_qcom_vsync));
    res->fb_fd = fb_fd;
#ifdef MR_QCOM_OVERLAY_USE_VSYNC
    res->_run_thread = 1;
    pthread_mutex_init(&res->mutex, NULL);
    pthread_cond_init(&res->cond, NULL);
    pthread_create(&res->thread, NULL, &fb_qcom_vsync_thread_work, res);
#endif
    return res;
}

static void fb_qcom_vsync_destroy(struct fb_qcom_vsync *vs)
{
#ifdef MR_QCOM_OVERLAY_USE_VSYNC
    pthread_mutex_lock(&vs->mutex);
    vs->_run_thread = 0;
    pthread_mutex_unlock(&vs->mutex);
    pthread_join(vs->thread, NULL);
    pthread_mutex_destroy(&vs->mutex);
    pthread_cond_destroy(&vs->cond);
#endif

    free(vs);
}

static int fb_qcom_vsync_wait(UNUSED struct fb_qcom_vsync *vs)
{
#ifdef MR_QCOM_OVERLAY_USE_VSYNC
    int res;
    struct timespec ts;

    pthread_mutex_lock(&vs->mutex);

    if(!vs->_run_thread)
    {
        pthread_mutex_unlock(&vs->mutex);
        return 0;
    }

    fb_qcom_vsync_enable(vs, 1);

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 20*1000*1000;
    if(ts.tv_nsec >= 1000000000)
    {
        ts.tv_nsec -= 1000000000;
        ++ts.tv_sec;
    }

    res = pthread_cond_timedwait(&vs->cond, &vs->mutex, &ts);
    pthread_mutex_unlock(&vs->mutex);

    return res;
#else
    return 0;
#endif
}


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
    FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "re");
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
    int ret = 0, i;
    struct fb_qcom_overlay_mem_info *info;
    for(i = 0; i < NUM_BUFFERS; ++i)
    {
        info = &data->mem_info[i];

        if(info->mem_buf)
            munmap(info->mem_buf, info->size);

        if(info->ion_fd >= 0)
        {
            ret = ioctl(info->ion_fd, ION_IOC_FREE, &info->handle_data);
            if(ret < 0)
                ERROR("free_mem failed ");
        }

        if(info->mem_fd >= 0)
            close(info->mem_fd);

        if(info->ion_fd >= 0)
            close(info->ion_fd);
    }
    return 0;
}

static int alloc_ion_mem(struct fb_qcom_overlay_data *data, unsigned int size)
{
    int result, i;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;
    struct fb_qcom_overlay_mem_info *info;

    ionAllocData.flags = 0;
    ionAllocData.len = size;
    ionAllocData.align = sysconf(_SC_PAGESIZE);

// are you kidding me -.-
#if (PLATFORM_SDK_VERSION >= 21)
    ionAllocData.heap_id_mask =
#else
    ionAllocData.heap_mask =
#endif
            ION_HEAP(ION_IOMMU_HEAP_ID) |
            ION_HEAP(21); // ION_SYSTEM_CONTIG_HEAP_ID

    for(i = 0; i < NUM_BUFFERS; ++i)
    {
        info = &data->mem_info[i];

        info->ion_fd = open("/dev/ion", O_RDWR|O_DSYNC|O_CLOEXEC);
        if(info->ion_fd < 0)
        {
            ERROR("ERROR: Can't open ion ");
            return -errno;
        }

        result = ioctl(info->ion_fd, ION_IOC_ALLOC,  &ionAllocData);
        if(result)
        {
            ERROR("ION_IOC_ALLOC Failed ");
            close(info->ion_fd);
            return result;
        }

        fd_data.handle = ionAllocData.handle;
        info->handle_data.handle = ionAllocData.handle;
        result = ioctl(info->ion_fd, ION_IOC_MAP, &fd_data);
        if(result)
        {
            ERROR("ION_IOC_MAP Failed ");
            free_ion_mem(data);
            return result;
        }
        info->mem_buf = (uint8_t*)mmap(NULL, size, PROT_READ |
                    PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
        info->mem_fd = fd_data.fd;

        if(info->mem_buf == MAP_FAILED)
        {
            ERROR("ERROR: mem_buf MAP_FAILED ");
            info->mem_buf = NULL;
            free_ion_mem(data);
            return -ENOMEM;
        }

        info->offset = 0;
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

#ifdef TW_SCREEN_BLANK_ON_BOOT
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_POWERDOWN);
    ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK);
#endif

    if(alloc_ion_mem(data, fb->fi.line_length * fb->vi.yres) < 0)
        goto fail;

    if(allocate_overlay(data, fb->fd, fb->vi.xres, fb->vi.yres) < 0)
    {
        free_ion_mem(data);
        goto fail;
    }

    data->vsync = fb_qcom_vsync_init(fb->fd);

    fb->impl_data = data;
    return 0;

fail:
    free(data);
    return -1;
}

static void impl_close(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = fb->impl_data;
    fb_qcom_vsync_destroy(data->vsync);
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
    struct fb_qcom_overlay_mem_info *info = &data->mem_info[data->active_mem];

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
        ovdataL.data.offset = info->offset;
        ovdataL.data.memory_id = info->mem_fd;
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
        ovdataL.data.offset = info->offset;
        ovdataL.data.memory_id = info->mem_fd;
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
        ovdataR.data.offset = info->offset;
        ovdataR.data.memory_id = info->mem_fd;
        ret = ioctl(fb->fd, MSMFB_OVERLAY_PLAY, &ovdataR);
        if(ret < 0)
        {
            ERROR("overlay_display_frame failed, overlayR play Failed\n");
            return ret;
        }
    }

    memset(&ext_commit, 0, sizeof(struct mdp_display_commit));
    ext_commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;

    fb_qcom_vsync_wait(data->vsync);

    ret = ioctl(fb->fd, MSMFB_DISPLAY_COMMIT, &ext_commit);
    if(ret < 0)
    {
        ERROR("overlay_display_frame failed, overlay commit Failed\n!");
        return -1;
    }

    if(++data->active_mem >= NUM_BUFFERS)
        data->active_mem = 0;

    return ret;
}

static void *impl_get_frame_dest(struct framebuffer *fb)
{
    struct fb_qcom_overlay_data *data = fb->impl_data;
    return data->mem_info[data->active_mem].mem_buf;
}

const struct fb_impl fb_impl_qcom_overlay = {
    .name = "Qualcomm ION overlay",
    .impl_id = FB_IMPL_QCOM_OVERLAY,

    .open = impl_open,
    .close = impl_close,
    .update = impl_update,
    .get_frame_dest = impl_get_frame_dest,
};
