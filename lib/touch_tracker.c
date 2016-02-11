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

#include <stdlib.h>

#include "touch_tracker.h"
#include "util.h"

touch_tracker *touch_tracker_create(void)
{
    touch_tracker *t = mzalloc(sizeof(touch_tracker));
    return t;
}

void touch_tracker_destroy(touch_tracker *t)
{
    free(t);
}

void touch_tracker_start(touch_tracker *t, touch_event *ev)
{
    t->distance_abs_x = t->distance_abs_y = 0;
    t->distance_x = t->distance_y = 0;
    t->start_x = ev->x;
    t->start_y = ev->y;
    t->last_x = ev->x;
    t->last_y = ev->y;
    t->prev_x = ev->x;
    t->prev_y = ev->y;
    memcpy(&t->time_start, &ev->time, sizeof(struct timeval));
}

void touch_tracker_finish(touch_tracker *t, touch_event *ev)
{
    t->period = timeval_us_diff(ev->time, t->time_start);
}

void touch_tracker_add(touch_tracker *t, touch_event *ev)
{
    t->prev_x = t->last_x;
    t->prev_y = t->last_y;
    t->distance_x += ev->x - t->last_x;
    t->distance_y += ev->y - t->last_y;
    t->distance_abs_x += iabs(ev->x - t->last_x);
    t->distance_abs_y += iabs(ev->y - t->last_y);
    t->last_x = ev->x;
    t->last_y = ev->y;
}

float touch_tracker_get_velocity(touch_tracker *t, int axis)
{
    if(axis == TRACKER_X)
        return ((((float)t->distance_x) / t->period) * 1000000) / DPI_MUL;
    else
        return ((((float)t->distance_y) / t->period) * 1000000) / DPI_MUL;
}

float touch_tracker_get_velocity_abs(touch_tracker *t, int axis)
{
    if(axis == TRACKER_X)
        return ((((float)t->distance_abs_x) / t->period) * 1000000) / DPI_MUL;
    else
        return ((((float)t->distance_abs_y) / t->period) * 1000000) / DPI_MUL;
}
