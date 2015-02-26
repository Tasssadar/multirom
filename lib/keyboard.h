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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#include "framebuffer.h"
#include "button.h"

struct keyboard
{
    FB_ITEM_POS
    button **btns;
    const uint32_t *keycode_map;
};

struct keyboard *keyboard_create(int x, int y, int w, int h);
void keyboard_destroy(struct keyboard *k);

#endif
