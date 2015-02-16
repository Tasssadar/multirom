/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
    t->start_x = ev->x;
    t->start_y = ev->y;
    t->last_x = ev->x;
    t->last_y = ev->y;
    memcpy(&v->time_start, &ev->time, sizeof(struct timeval));
}

void touch_tracker_finish(touch_tracker *t, touch_event *ev)
{
    t->distance_x = ev->x - t->start_x;
    t->distance_y = ev->y - t->start_y;
    t->period = timeval_us_diff(ev->time, t->time_start);
}

void touch_tracker_add(touch_tracker *t, touch_event *ev)
{
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
