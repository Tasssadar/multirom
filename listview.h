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

#ifndef LISTVIEW_H
#define LISTVIEW_H

#include "input.h"
#include "framebuffer.h"

enum
{
    IT_VISIBLE  = 0x01,
    IT_HOVER    = 0x02,
    IT_SELECTED = 0x04,
};

typedef struct
{
    int id;
    void *data;
    int flags;
} listview_item;

typedef struct 
{
    int id;
    int start_y;
    int last_y;
    int64_t us_diff;
    listview_item *hover;
    int fast_scroll;
} listview_touch_data;

typedef struct
{
    int x, y;
    int w, h;

    int pos; // scroll pos
    int fullH; // height of all items

    listview_item **items;
    listview_item *selected;

    void (*item_draw)(int, int, int, listview_item *); // x, y, w, item
    void (*item_hide)(void*); // data
    int (*item_height)(void*); // data
    void (*item_destroy)(listview_item *);
    void (*item_selected)(listview_item *, listview_item *); // prev, now
    void (*item_confirmed)(listview_item *); // item - confirmed by keyaction

    fb_item_header **ui_items;
    fb_rect *scroll_mark;
    fb_rect *overscroll_marks[2];
    fb_rect **keyact_frame;
    int keyact_item_selected;

    listview_touch_data touch;
} listview;

int listview_touch_handler(touch_event *ev, void *data);

void listview_init_ui(listview *view);
void listview_destroy(listview *view);
listview_item *listview_add_item(listview *view, int id, void *data);
void listview_clear(listview *view);
void listview_update_ui(listview *view);
void listview_enable_scroll(listview *view, int enable);
void listview_update_scroll_mark(listview *view);
void listview_update_overscroll_mark(listview *v, int side, float overscroll);
void listview_scroll_by(listview *view, int y);
void listview_scroll_to(listview *view, int pct);
void listview_ensure_visible(listview *view, listview_item *it);
listview_item *listview_item_at(listview *view, int y_pos);
inline void listview_select_item(listview *view, listview_item *it);
void listview_update_keyact_frame(listview *view);
int listview_keyaction_call(void *data, int act);

void *rom_item_create(const char *text, const char *partition, const char *icon);
void rom_item_draw(int x, int y, int w, listview_item *it);
void rom_item_hide(void *data);
int rom_item_height(void *data);
void rom_item_destroy(listview_item *it);

#endif