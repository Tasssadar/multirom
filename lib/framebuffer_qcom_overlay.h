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

#define MSMFB_DISPLAY_COMMIT_S      _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                    struct mdp_display_commit_s)
#define MSMFB_DISPLAY_COMMIT_LR   _IOW(MSMFB_IOCTL_MAGIC, 164, \
                                    struct mdp_display_commit_lr)

#define MDP_DISPLAY_COMMIT_OVERLAY 1

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
