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

#define OSK_EMPTY 0xFF
#define OSK_ENTER 0xFE
#define OSK_BACKSPACE 0xFD
#define OSK_CLEAR 0xFC
#define OSK_CHARSET1 0xFB
#define OSK_CHARSET2 0xFA
#define OSK_CHARSET3 0xF9
#define OSK_CHARSET4 0xF8

typedef void (*keyboard_on_pressed_callback)(void *data, uint8_t keycode);
struct keyboard
{
    FB_ITEM_POS
    button **btns;
    void **keyboard_bnt_data;
    const uint32_t *keycode_map;
    keyboard_on_pressed_callback key_pressed;
    void *key_pressed_data;
};

#define KEYBOARD_PIN 0
#define KEYBOARD_NORMAL 1

struct keyboard *keyboard_create(int type, int x, int y, int w, int h);
void keyboard_set_callback(struct keyboard *k, keyboard_on_pressed_callback callback, void *data);
void keyboard_destroy(struct keyboard *k);

#endif
