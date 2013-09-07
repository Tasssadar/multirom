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

#ifndef CHECKBOX_H
#define CHECKBOX_H

#include "framebuffer.h"
#include "input.h"

#define CHECKBOX_SIZE (30*DPI_MUL)

enum
{
    BORDER_L = 0,
    BORDER_R = 1,
    BORDER_T = 2, 
    BORDER_B = 3, 

    BORDER_MAX
};

typedef struct
{
    int x, y;
    fb_rect *selected;
    fb_rect *borders[BORDER_MAX];
    int touch_id;
    void (*clicked)(int); // checked
    fb_rect *hover;
} checkbox;

checkbox *checkbox_create(int x, int y, void (*clicked)(int));
void checkbox_destroy(checkbox *c);

void checkbox_set_pos(checkbox *c, int x, int y);
void checkbox_select(checkbox *c, int select);

int checkbox_touch_handler(touch_event *ev, void *data);

#endif