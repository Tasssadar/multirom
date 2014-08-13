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

#ifndef BUTTON_H
#define BUTTON_H

#include "framebuffer.h"
#include "input.h"

enum 
{
    BTN_HOVER         = 0x01,
    BTN_DISABLED      = 0x02,
    BTN_CHECKED       = 0x04,
};

enum
{
    CLR_NORMAL        = 0,
    CLR_HOVER,
    CLR_DIS,
    CLR_CHECK,

    CLR_MAX
};

typedef struct
{
    FB_ITEM_HEAD

    fb_img *text;
    fb_rect *rect;
    fb_rect **keyact_frame;
    int level_off;

    uint32_t c[CLR_MAX][2];
 
    int flags;
    int touch_id;

    int action;
    void (*clicked)(int); // action
} button;

void button_init_ui(button *b, const char *text, int size);
void button_destroy(button *b);
void button_move(button *b, int x, int y);
void button_set_hover(button *b, int hover);
void button_enable(button *b, int enable);
void button_set_checked(button *b, int checked);
void button_set_color(button *b, int idx, int text, uint32_t color);
void button_update_colors(button *b);
int button_touch_handler(touch_event *ev, void *data);
int button_keyaction_call(void *data, int act);

#endif
