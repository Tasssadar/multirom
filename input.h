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

#ifndef INPUT_H
#define INPUT_H

#include <sys/time.h>

#define KEY_VOLUMEUP 115
#define KEY_VOLUMEDOWN 114
#define KEY_POWER 116

enum
{
    TCHNG_POS       = 0x01,
    //TCHNG_PRESSURE  = 0x02, // unused
    TCHNG_ADDED     = 0x04,
    TCHNG_REMOVED   = 0x08
};

enum
{
    HANDLERS_FIRST  = 0,
    HANDLERS_ALL    = 1
};

typedef struct
{
    int id;
    int x, orig_x;
    int y, orig_y;
    int changed;

    struct timeval time;
    int64_t us_diff;
} touch_event;

typedef int (*touch_callback)(touch_event*, void*); // event, data

void start_input_thread(void);
void stop_input_thread(void);

int get_last_key(void);
int wait_for_key(void);

void add_touch_handler(touch_callback callback, void *data);
void rm_touch_handler(touch_callback callback, void *data);
void set_touch_handlers_mode(int mode);

void input_push_context(void);
void input_pop_context(void);

#endif