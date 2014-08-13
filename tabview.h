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

#ifndef TABVIEW_H
#define TABVIEW_H

#include <pthread.h>
#include "framebuffer.h"
#include "input.h"
#include "touch_tracker.h"

struct tabview_page;

typedef struct {
	FB_ITEM_POS

	int pos;
    int anim_pos_start;
    int anim_pos_diff;
	int fullW;

	struct tabview_page **pages;
    int count;
    int curr_page;

    uint32_t anim_id;
    pthread_mutex_t mutex;

    void (*on_page_changed_by_swipe)(int); // new_page
    void (*on_pos_changed)(float);

    int last_reported_pos;

    int touch_id;
    int touch_moving;
    touch_tracker *tracker;
} tabview;

tabview *tabview_create(int x, int y, int w, int h);
int tabview_touch_handler(touch_event *ev, void *data);
void tabview_destroy(tabview *t);
void tabview_add_page(tabview *t, int idx);
void tabview_rm_page(tabview *t, int idx);
void tabview_add_item(tabview *t, int page_idx, void *fb_item);
void tabview_add_items(tabview *t, int page_idx, void *fb_items);
void tabview_rm_item(tabview *t, int page_idx, void *fb_item);
void tabview_update_positions(tabview *t);
void tabview_set_active_page(tabview *t, int page_idx, int anim_duration);

#endif
