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

#ifndef NOTIFICATION_CARD_H
#define NOTIFICATION_CARD_H

#include "framebuffer.h"

enum // order from right to left
{
    BTN_POSITIVE,
    BTN_NEGATIVE,

    BTN_COUNT
};

enum
{
    NCARD_POS_AUTO,
    NCARD_POS_TOP,
    NCARD_POS_BOTTOM
};

typedef void (*ncard_btn_callback)(void*);

typedef struct
{
    char *text;
    void *callback_data;
    ncard_btn_callback callback;
} ncard_builder_btn;

typedef struct 
{
    char *title;
    char *text;
    ncard_builder_btn *buttons[BTN_COUNT];
    int pos;
    fb_item_pos *avoid_item;
} ncard_builder;

ncard_builder *ncard_create_builder(void);
void ncard_set_title(ncard_builder *b, const char *title);
void ncard_set_text(ncard_builder *b, const char *text);
void ncard_set_pos(ncard_builder *b, int pos);
void ncard_avoid_item(ncard_builder *b, void *item);
void ncard_add_btn(ncard_builder *b, int btn_type, const char *text, ncard_btn_callback callback, void *callback_data);

void ncard_set_top_offset(int offset);
void ncard_show(ncard_builder *b, int destroy_builder);
void ncard_hide(void);
void ncard_destroy_builder(ncard_builder *b);

#endif
