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

#ifndef PONG_H
#define PONG_H

#include "input.h"

void pong(void);
int pong_touch_handler(touch_event *ev, void *data);
void pong_spawn_ball(int side);
void pong_calc_movement(void);
void pong_add_score(int side);
void pong_handle_ai(void);
int pong_do_movement(int step);
int pong_get_collision(int x, int y);

#endif
