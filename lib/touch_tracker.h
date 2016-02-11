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

#ifndef TOUCH_TRACKER_H
#define TOUCH_TRACKER_H

#include <sys/time.h>
#include <time.h>
#include "input.h"

#define TRACKER_X 0
#define TRACKER_Y 1

typedef struct
{
    struct timeval time_start;
    int64_t period;
    int distance_x, distance_y;
    int distance_abs_x, distance_abs_y;
    int last_x, last_y;
    int prev_x, prev_y;
    int start_x, start_y;
} touch_tracker;

touch_tracker *touch_tracker_create(void);
void touch_tracker_destroy(touch_tracker *t);
void touch_tracker_start(touch_tracker *t, touch_event *ev);
void touch_tracker_finish(touch_tracker *t, touch_event *ev);
void touch_tracker_add(touch_tracker *t, touch_event *ev);
float touch_tracker_get_velocity(touch_tracker *t, int axis);
float touch_tracker_get_velocity_abs(touch_tracker *t, int axis);

#endif
