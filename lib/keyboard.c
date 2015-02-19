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

#include <stdlib.h>

#include "keyboard.h"
#include "util.h"

#define KS(x) ((x-1) << 16)
#define GET_KS(x) ((x & 0xFF0000)>> 16)

#define KEY_EMPTY 0xFF
#define KEY_ENTER 0xFE
#define KEY_CLEAR 0xFD

static const char *specialKeys[] = {
    NULL,  // KEY_EMPTY
    "OK",  // KEY_ENTER
    "X",   // KEY_CLEAR
};

// One keycode
// bits | 0         | 8       | 16      |
// data | character | flags   | colspan |
static const uint32_t pinKeycodeMap[] = {
    '1', '2', '3', KEY_EMPTY,
    '4', '5', '6', KEY_EMPTY,
    '7', '8', '9', KEY_CLEAR,
    '0' | KS(3),   KEY_EMPTY,
    0
};

// rows, cols
static const uint32_t pinKeycodeMapDimensions[] = { 4, 4 };

#define PADDING (5*DPI_MUL)

struct keyboard_btn_data {
    struct keyboard *k;
    int btn_idx;
};

static void keyboard_btn_clicked(void *data)
{
    struct keyboard_btn_data *d = data;
}

static int keyboard_init_map(struct keyboard *k, const uint32_t *map, const uint32_t *dimen)
{
    button *btn;
    int i, idx = 0;
    uint32_t col = 0;
    char buf[2] = { 0 };

    int x = k->x + PADDING;
    int y = k->y + PADDING;
    int w;
    const int btn_w = (k->w - PADDING*(dimen[1]+1)) /dimen[1];
    const int btn_h = (k->h - PADDING*(dimen[0]+1)) /dimen[0];

    for(i = 0; map[0]; ++i)
    {
        w = (GET_KS(map[i])+1)*btn_w + PADDING*GET_KS(map[i]);

        if((map[i] & 0xFF) != KEY_EMPTY)
        {
            btn = mzalloc(sizeof(button));
            btn->x = x;
            btn->y = y;
            btn->w = w;
            btn->h = btn_h;

            struct keyboard_btn_data *d = mzalloc(sizeof(struct keyboard_btn_data));
            d->k = k;
            d->btn_idx = i;
            btn->clicked_data = d;
            btn->clicked = keyboard_btn_clicked;

            buf[0] = (map[i] & 0xFF);
            button_init_ui(btn, buf[0] >= 0 ? buf : specialKeys[0xFF - (map[i] & 0xFF)], SIZE_NORMAL);
        }

        if(++col < dimen[1])
            x += w + PADDING;
        else
        {
            x = k->x + PADDING;
            y += btn_h + PADDING;
            col = 0;
        }
    }
}

struct keyboard *keyboard_create(int x, int y, int w, int h)
{
    struct keyboard *k = mzalloc(sizeof(struct keyboard));
    k->x = x;
    k->y = y;
    k->w = w;
    k->h = h;


}

void keyboard_destroy(struct keyboard *k)
{

}
