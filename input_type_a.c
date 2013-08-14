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

// Implementation of "Protocol Example A" from kernel's
// Documentation/input/multi-touch-protocol.txt

// This protocol requires client to keep track of ids,
// I don't really like this implementation, but I can't
// come up with anything better :/

#include <linux/input.h>
#include <assert.h>
#include <stdlib.h>

#include "input.h"
#include "input_priv.h"
#include "util.h"

static int *active_touches = NULL;
static int *curr_touches = NULL;

static void idlist_clear(int *list)
{
    int i;
    for(i = 0; i < MAX_FINGERS && list[i] != -1; ++i)
        list[i] = -1;
}

static int *idlist_init(void)
{
    int *res = malloc(MAX_FINGERS*sizeof(int));

    int i;
    for(i = 0; i < MAX_FINGERS; ++i)
        res[i] = -1;

    return res;
}

static void idlist_swap(int **list_a, int **list_b)
{
    int *tmp = *list_a;
    *list_a = *list_b;
    *list_b = tmp;
}

static int idlist_add(int *list, int id)
{
    int i;
    for(i = 0; i < MAX_FINGERS; ++i)
    {
        if(list[i] == id)
            return -1;

        if(list[i] == -1)
        {
            list[i] = id;
            return 0;
        }
    }
    assert(0);
    return -1;
}

static int idlist_rm(int *list, int id)
{
    int i;
    for(i = 0; i < MAX_FINGERS; ++i)
    {
        if(list[i] == -1)
            return -1;

        if(list[i] == id)
        {
            for(++i; i < MAX_FINGERS && list[i] != -1; ++i)
                list[i-1] = list[i];
            list[i-1] = -1;
            return 0;
        }
    }
    return -1;
}

void init_touch_specifics(void)
{
    active_touches = idlist_init();
    curr_touches = idlist_init();
}

void destroy_touch_specifics(void)
{
    free(active_touches);
    free(curr_touches);
    active_touches = NULL;
    curr_touches = NULL;
}

void handle_abs_event(struct input_event *ev)
{
    switch(ev->code)
    {
        case ABS_MT_TRACKING_ID:
        {
            mt_events[mt_slot++].id = ev->value;
            break;
        }
        case ABS_MT_POSITION_X:
        case ABS_MT_POSITION_Y:
        {
            if((ev->code == ABS_MT_POSITION_X) ^ (mt_switch_xy != 0))
            {
                mt_events[mt_slot].orig_x = calc_mt_pos(ev->value, mt_range_x, mt_screen_res[0]);
                if(mt_switch_xy)
                    mt_events[mt_slot].orig_x = mt_screen_res[0] - mt_events[mt_slot].orig_x;
            }
            else
                mt_events[mt_slot].orig_y = calc_mt_pos(ev->value, mt_range_y, mt_screen_res[1]);

            mt_events[mt_slot].changed |= TCHNG_POS;
            break;
        }
    }
}

void handle_syn_event(struct input_event *ev)
{
    if(ev->code != SYN_REPORT)
        return;

    idlist_swap(&curr_touches, &active_touches);

    int i;
    for(i = 0; i < mt_slot; ++i)
    {
        idlist_add(active_touches, mt_events[i].id);
        if(idlist_rm(curr_touches, mt_events[i].id) == -1)
            mt_events[i].changed |= TCHNG_ADDED;
    }

    for(i = 0; mt_slot < MAX_FINGERS && i < MAX_FINGERS && curr_touches[i] != -1; ++i)
    {
        mt_events[mt_slot].id = curr_touches[i];
        mt_events[mt_slot].changed = TCHNG_REMOVED;
        curr_touches[i] = -1;
        ++mt_slot;
    }

    mt_slot = 0;
    touch_commit_events(ev->time);
}
