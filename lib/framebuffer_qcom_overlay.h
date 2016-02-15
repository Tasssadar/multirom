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

#ifndef H_FRAMEBUFFER_QCOM_OVERLAY
#define H_FRAMEBUFFER_QCOM_OVERLAY

#include <linux/types.h>

#include "framebuffer.h"

#ifdef MR_QCOM_OVERLAY_HEADER
#include MR_QCOM_OVERLAY_HEADER
#endif

#define NUM_BUFFERS 3

#define MSMFB_DISPLAY_COMMIT_N      _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                    struct mdp_display_commit_n)
#define MSMFB_DISPLAY_COMMIT_S      _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                    struct mdp_display_commit_s)
#define MSMFB_DISPLAY_COMMIT_LR   _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                    struct mdp_display_commit_lr)

enum {
    FILE_FOUND = 0x01,
    ROI_MERGE = 0x02,
    PARTIAL_UPDATE = 0x04
};

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
    int roi_merge;
};

struct mdp_display_commit_n {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
};

struct mdp_display_commit_s {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
    struct mdp_rect roi;
};

struct mdp_display_commit_lr {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
    struct mdp_rect l_roi;
    struct mdp_rect r_roi;
};

#endif
