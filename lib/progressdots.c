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

#include <unistd.h>
#include <stdlib.h>

#include "progressdots.h"
#include "colors.h"
#include "workers.h"
#include "util.h"
#include "animation.h"

static void progdots_anim_finished(void *data)
{
    progdots *p = data;

    item_anim *a = item_anim_create(p->rect, 1000, INTERPOLATOR_ACCEL_DECEL);
    if(p->rect->x == p->x)
        a->targetX = p->x + PROGDOTS_W - p->rect->w;
    else
        a->targetX = p->x;
    a->start_offset = 300;
    a->on_finished_call = progdots_anim_finished;
    a->on_finished_data = p;
    item_anim_add(a);
}

progdots *progdots_create(int x, int y)
{
    progdots *p = mzalloc(sizeof(progdots));
    p->x = x;
    p->y = y;

    p->rect = fb_add_rect(x, y, PROGDOTS_H*4, PROGDOTS_H, C_HIGHLIGHT_BG);
    item_anim *a = item_anim_create(p->rect, 1000, INTERPOLATOR_ACCEL_DECEL);
    a->targetX = x + PROGDOTS_W - p->rect->w;
    a->on_finished_call = progdots_anim_finished;
    a->on_finished_data = p;
    item_anim_add(a);
    return p;
}

void progdots_destroy(progdots *p)
{
    anim_cancel_for(p->rect, 0);
    fb_rm_rect(p->rect);
    free(p);
}
