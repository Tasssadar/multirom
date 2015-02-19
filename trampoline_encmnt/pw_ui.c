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

#include "pw_ui.h"
#include "../lib/framebuffer.h"
#include "../lib/colors.h"
#include "../lib/log.h"
#include "../lib/input.h"

int pw_ui_run(int pwtype)
{
    if(fb_open(0) < 0)
    {
        ERROR("Failed to open framebuffer");
        return -1;
    }

    fb_freeze(1);
    fb_set_background(C_BACKGROUND);

    start_input_thread();

    fb_freeze(0);
    fb_request_draw();

    while(1) {
        sleep(1);
    }

    stop_input_thread();

    fb_clear();
    fb_close();
    return -1;
}
