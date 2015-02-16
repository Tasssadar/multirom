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

// Implementation of "Protocol Example B" from kernel's
// Documentation/input/multi-touch-protocol.txt

#include <linux/input.h>
#include "input.h"
#include "input_priv.h"
#include "util.h"

void init_touch_specifics(void)
{

}

void destroy_touch_specifics(void)
{

}

void handle_abs_event(struct input_event *ev)
{
    switch(ev->code)
    {
        case ABS_MT_SLOT:
            if(ev->value < (int)ARRAY_SIZE(mt_events))
                mt_slot = ev->value;
            break;
        case ABS_MT_TRACKING_ID:
        {
            if(ev->value != -1)
            {
                mt_events[mt_slot].id = ev->value;
                mt_events[mt_slot].changed |= TCHNG_ADDED;
            }
            else
                mt_events[mt_slot].changed |= TCHNG_REMOVED;
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
    if(ev->code == SYN_REPORT)
        touch_commit_events(ev->time);
}
