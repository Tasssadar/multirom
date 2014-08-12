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
#include <stdio.h>
#include <math.h>

#include "tabview.h"
#include "containers.h"
#include "util.h"
#include "animation.h"
#include "log.h"
#include "input.h"

struct tabview_page
{
    fb_item_pos **items;
    int last_offset;
};

typedef struct tabview_page tabview_page;

static void tabview_page_destroy(tabview_page *p)
{
    list_clear(&p->items, NULL);
    free(p);
}

static void tabview_page_update_offset(tabview_page *p, int offset)
{
    if(!p->items || offset == p->last_offset)
        return;

    fb_item_pos **itr;
    const int diff = offset - p->last_offset;

    for(itr = p->items; *itr; ++itr)
        (*itr)->x += diff;

    p->last_offset = offset;
}

int tabview_touch_handler(touch_event *ev, void *data)
{
    tabview *t = data;
    if(t->touch_id == -1 && (ev->changed & TCHNG_ADDED))
    {
        if (ev->x < t->x || ev->y < t->y ||
            ev->x > t->x+t->w || ev->y > t->y+t->h)
        {
            return -1;
        }

        t->touch_id = ev->id;
        t->touch_moving = 0;
        touch_tracker_start(t->tracker, ev);

        if(t->anim_id != ANIM_INVALID_ID)
        {
            anim_cancel(t->anim_id, 0);
            t->anim_id = ANIM_INVALID_ID;
        }
        return -1;
    }

    if(t->touch_id != ev->id)
        return -1;

    if(ev->changed & TCHNG_REMOVED)
    {
        t->touch_id = -1;
        touch_tracker_finish(t->tracker, ev);

        if(!t->touch_moving)
            return -1;

        if(t->pos % t->w != 0)
        {
            int page_idx, duration = 100;   
            float page = ((float)t->pos)/t->w;
            if(page < 0)
                page_idx = 0;
            else if(page >= t->count - 1)
                page_idx = t->count - 1;
            else
            {
                float velocity = touch_tracker_get_velocity(t->tracker, TRACKER_X);
                if(fabs(velocity) >= 1000.f)
                {
                    page_idx = (int)page;
                    if(velocity < 0.f)
                        ++page_idx;
                    duration = iabs(t->pos - page_idx*t->w)/(fabs(velocity*DPI_MUL)/1000);
                }
                else
                    page_idx = (int)(page + 0.5f);
            }

            if(page_idx != t->curr_page)
            {
                if(t->on_page_changed_by_swipe)
                    t->on_page_changed_by_swipe(page_idx);
                t->curr_page = page_idx;
            }

            tabview_set_active_page(t, page_idx, duration);
        }
        return -1;
    }

    if(ev->changed & TCHNG_POS)
    {
        touch_tracker_add(t->tracker, ev);

        if(!t->touch_moving)
        {
            if (t->tracker->distance_abs_x >= 25*DPI_MUL && t->tracker->distance_abs_x > t->tracker->distance_abs_y*3)
            {
                t->touch_moving = 1;
                ev->changed |= TCHNG_REMOVED;
                ev->x = -1;
                ev->y = -1;

                t->pos += -t->tracker->distance_x;
                tabview_update_positions(t);
            }
            return -1;
        }

        t->pos += t->tracker->prev_x - ev->x;
        tabview_update_positions(t);
        return 1;
    }

    return -1;
}

tabview *tabview_create(int x, int y, int w, int h)
{
    tabview *t = mzalloc(sizeof(tabview));
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    t->anim_id = ANIM_INVALID_ID;
    t->touch_id = -1;
    t->tracker = touch_tracker_create();
    pthread_mutex_init(&t->mutex, NULL);
    return t;
}

void tabview_destroy(tabview *t)
{
    rm_touch_handler(&tabview_touch_handler, t);
    pthread_mutex_destroy(&t->mutex);
    list_clear(&t->pages, tabview_page_destroy);
    touch_tracker_destroy(t->tracker);
    free(t);
}

void tabview_add_page(tabview *t, int idx)
{
    if(idx == -1)
        idx = t->count;

    tabview_page *p = mzalloc(sizeof(tabview_page));

    pthread_mutex_lock(&t->mutex);
    list_add_at(&t->pages, idx, p);
    ++t->count;
    t->fullW = t->count*t->w;
    pthread_mutex_unlock(&t->mutex);
}

void tabview_rm_page(tabview *t, int idx)
{
    if(idx < 0 || idx >= t->count)
        return;

    pthread_mutex_lock(&t->mutex);
    list_rm_at(&t->pages, idx, tabview_page_destroy);
    --t->count;
    t->fullW = t->count*t->w;
    pthread_mutex_unlock(&t->mutex);
}

void tabview_add_item(tabview *t, int page_idx, void *fb_item)
{
    if(page_idx < 0 || page_idx >= t->count)
        return;

    pthread_mutex_lock(&t->mutex);
    list_add(&t->pages[page_idx]->items, fb_item);
    pthread_mutex_unlock(&t->mutex);
}

void tabview_add_items(tabview *t, int page_idx, void *fb_items)
{
    if(page_idx < 0 || page_idx >= t->count || !fb_items)
        return;

    pthread_mutex_lock(&t->mutex);
    list_add_from_list(&t->pages[page_idx]->items, fb_items);
    pthread_mutex_unlock(&t->mutex);
}

void tabview_rm_item(tabview *t, int page_idx, void *fb_item)
{
    if(page_idx < 0 || page_idx >= t->count)
        return;

    pthread_mutex_lock(&t->mutex);
    list_rm(&t->pages[page_idx]->items, fb_item, NULL);
    pthread_mutex_unlock(&t->mutex);
}

void tabview_update_positions(tabview *t)
{
    int i;
    int x = 0;

    if(t->last_reported_pos != t->pos)
    {
        if(t->on_pos_changed)
            t->on_pos_changed(((float)t->pos)/t->w);
        t->last_reported_pos = t->pos;
    }

    fb_batch_start();
    pthread_mutex_lock(&t->mutex);
    for(i = 0; i < t->count; ++i)
    {   
        tabview_page_update_offset(t->pages[i], x - t->pos);
        x += t->w;
    }
    pthread_mutex_unlock(&t->mutex);
    fb_batch_end();
    fb_request_draw();
}

static void tabview_move_anim_step(void *data, float interpolated)
{
    tabview *t = data;
    t->pos = t->anim_pos_start + (t->anim_pos_diff * interpolated);
    tabview_update_positions(t);
}

void tabview_set_active_page(tabview *t, int page_idx, int anim_duration)
{
    if(page_idx < 0 || page_idx >= t->count)
        return;

    if(t->curr_page == page_idx && t->pos == page_idx*t->w)
        return;

    if(t->anim_id != ANIM_INVALID_ID)
        anim_cancel(t->anim_id, 0);

    t->curr_page = page_idx;

    if(anim_duration == 0)
    {
        t->pos = page_idx*t->w;
        tabview_update_positions(t);
        return;
    }

    t->anim_pos_start = t->pos;
    t->anim_pos_diff = page_idx*t->w - t->pos;

    call_anim *a = call_anim_create(t, tabview_move_anim_step, anim_duration, INTERPOLATOR_DECELERATE);
    t->anim_id = a->id;
    call_anim_add(a);
}
