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

#ifndef INPUT_PRIV_H
#define INPUT_PRIV_H

#include <sys/time.h>
#include "input.h"

#define MAX_DEVICES 16
#define MAX_FINGERS 10

// for touch calculation
extern int mt_screen_res[2];
extern touch_event mt_events[MAX_FINGERS];
extern int mt_slot;
extern int mt_switch_xy;
extern int mt_range_x[2];
extern int mt_range_y[2];

typedef struct
{
    void *data;
    touch_callback callback;
} touch_handler;

struct handler_list_it
{
    touch_handler *handler;

    struct handler_list_it *prev;
    struct handler_list_it *next;
};

typedef struct handler_list_it handler_list_it;

typedef struct
{
    int handlers_mode;
    handler_list_it *handlers;
} handlers_ctx;

void touch_commit_events(struct timeval ev_time);
inline int calc_mt_pos(int val, int *range, int d_max);

// Implemented in input_touch*.c files
void handle_abs_event(struct input_event *ev);
void handle_syn_event(struct input_event *ev);
void init_touch_specifics(void);
void destroy_touch_specifics(void);


#endif
