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
#include "progressdots.h"
#include "multirom_ui.h"
#include "workers.h"
#include "util.h"

// ms
#define SWITCH_SPEED 800

static void progdots_animate(uint32_t diff, void *data)
{
    progdots *p = (progdots*)data;

    if(p->switch_timer <= diff)
    {
        if(++p->active_dot >= PROGDOTS_CNT)
            p->active_dot = 0;

        progdots_set_active(p, p->active_dot);
        fb_request_draw();

        p->switch_timer = SWITCH_SPEED;
    }
    else
        p->switch_timer -= diff;
}

progdots *progdots_create(int x, int y)
{
    progdots *p = mzalloc(sizeof(progdots));
    p->x = x;
    p->y = y;
    p->switch_timer = SWITCH_SPEED;

    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
    {
        p->dots[i] = fb_add_rect(x, y, PROGDOTS_H, PROGDOTS_H, (i == 0 ? CLR_PRIMARY : WHITE));
        x += PROGDOTS_H + (PROGDOTS_W - (PROGDOTS_CNT*PROGDOTS_H))/(PROGDOTS_CNT-1);
    }

    workers_add(progdots_animate, p);

    fb_draw();
    return p;
}

void progdots_destroy(progdots *p)
{
    workers_remove(progdots_animate, p);

    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
        fb_rm_rect(p->dots[i]);
    free(p);
}

void progdots_set_active(progdots *p, int dot)
{
    p->active_dot = dot;
    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
        p->dots[i]->color = (i == dot ? CLR_PRIMARY : WHITE);
}
