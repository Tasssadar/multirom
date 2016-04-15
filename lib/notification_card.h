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
    NCARD_POS_BOTTOM,
    NCARD_POS_CENTER,
};

typedef void (*ncard_callback)(void*);

typedef struct
{
    char *text;
    void *callback_data;
    ncard_callback callback;
} ncard_builder_btn;

typedef struct
{
    char *title;
    char *text;
    ncard_builder_btn *buttons[BTN_COUNT];
    int pos;
    fb_item_pos *avoid_item;
    int cancelable;
    ncard_callback on_hidden_call;
    void *on_hidden_data;
    int reveal_from_black;
} ncard_builder;

ncard_builder *ncard_create_builder(void);
void ncard_set_title(ncard_builder *b, const char *title);
void ncard_set_text(ncard_builder *b, const char *text);
void ncard_set_pos(ncard_builder *b, int pos);
void ncard_set_cancelable(ncard_builder *b, int cancelable);
void ncard_avoid_item(ncard_builder *b, void *item);
void ncard_add_btn(ncard_builder *b, int btn_type, const char *text, ncard_callback callback, void *callback_data);
void ncard_set_on_hidden(ncard_builder *b, ncard_callback callback, void *data);
void ncard_set_from_black(ncard_builder *b, int from_black);

void ncard_set_top_offset(int offset);
void ncard_show(ncard_builder *b, int destroy_builder);
void ncard_hide(void);
int ncard_is_visible(void);
int ncard_is_moving(void);
int ncard_try_cancel(void);
void ncard_hide_callback(void *data);
void ncard_destroy_builder(ncard_builder *b);

#endif
