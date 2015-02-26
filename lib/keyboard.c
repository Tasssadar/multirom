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

#include "containers.h"
#include "keyboard.h"
#include "util.h"
#include "log.h"

#define KS(x) ((x-1) << 16)
#define GET_KS(x) ((x & 0xFF0000)>> 16)

#define KEY_EMPTY 0xFF
#define KEY_EMPTY_HALF 0xFE
#define KEY_ENTER 0xFD
#define KEY_CLEAR 0xFC
#define KEY_SHIFT 0xFB

static const char *specialKeys[] = {
    NULL,  // KEY_EMPTY
    NULL,  // KEY_EMPTY_HALF
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
    '0' | KS(3),   KEY_ENTER,
    0
};

// rows, cols
static const uint32_t pinKeycodeMapDimensions[] = { 4, 4 };

static const uint32_t normalKeycodeMap[] = {
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    KEY_EMPTY_HALF, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    KEY_SHIFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', KEY_CLEAR | KS(2),
    KEY_CLEAR, ' ' | KS(6), '.', KEY_ENTER | KS(2),
    0
};

// rows, cols
static const uint32_t normalKeycodeMapDimensions[] = { 4, 10 };

#define PADDING (8*DPI_MUL)

struct keyboard_btn_data {
    struct keyboard *k;
    int btn_idx;
};

static void keyboard_btn_clicked(void *data)
{
    struct keyboard_btn_data *d = data;
    INFO("keyboard %c pressed.", (char)(d->k->keycode_map[d->btn_idx] & 0xFF));
}

static int keyboard_init_map(struct keyboard *k, const uint32_t *map, const uint32_t *dimen)
{
    button *btn;
    int i, idx = 0;
    uint32_t col = 0;
    char buf[2] = { 0 };
    uint8_t code;

    int x = k->x + PADDING;
    int y = k->y + PADDING;
    int w;
    const int btn_w = (k->w - PADDING*(dimen[1]+1)) /dimen[1];
    const int btn_h = (k->h - PADDING*(dimen[0]+1)) /dimen[0];

    for(i = 0; map[i]; ++i)
    {
        code = (map[i] & 0xFF);
        w = (GET_KS(map[i])+1)*btn_w + PADDING*GET_KS(map[i]);

        if(code == KEY_EMPTY_HALF)
            w /= 2;
        else if(code != KEY_EMPTY)
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
            list_add(&k->btns, btn);
        }

        col += GET_KS(map[i])+1;
        if(col < dimen[1])
            x += w + PADDING;
        else
        {
            x = k->x + PADDING;
            y += btn_h + PADDING;
            col = 0;
        }
    }

    k->keycode_map = map;
    return 0;
}

struct keyboard *keyboard_create(int x, int y, int w, int h)
{
    struct keyboard *k = mzalloc(sizeof(struct keyboard));
    k->x = x;
    k->y = y;
    k->w = w;
    k->h = h;

    //keyboard_init_map(k, pinKeycodeMap, pinKeycodeMapDimensions);
    keyboard_init_map(k, normalKeycodeMap, normalKeycodeMapDimensions);

    return k;
}

void keyboard_destroy(struct keyboard *k)
{

}
