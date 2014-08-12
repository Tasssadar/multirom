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
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "log.h"
#include "workers.h"
#include "animation.h"
#include "util.h"
#include "framebuffer.h"
#include "containers.h"

struct anim_list_it
{
    int anim_type;
    anim_header *anim;

    struct anim_list_it *prev;
    struct anim_list_it *next;
};

struct anim_list
{
    struct anim_list_it *first;
    struct anim_list_it *last;

    struct anim_list_it **inactive_ctx;

    int running;
    float duration_coef;
    volatile int in_update_loop;
    pthread_mutex_t mutex;
};

static struct anim_list_it EMPTY_CONTEXT;

static struct anim_list anim_list = {
    .first = NULL,
    .last = NULL,
    .inactive_ctx = NULL,
    .running = 0,
    .duration_coef = 1.f,
    .in_update_loop = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

static void anim_list_append(struct anim_list_it *it)
{
    pthread_mutex_lock(&anim_list.mutex);
    if(!anim_list.first)
    {
        anim_list.first = anim_list.last = it;
        pthread_mutex_unlock(&anim_list.mutex);
        return;
    }

    it->prev = anim_list.last;
    anim_list.last->next = it;
    anim_list.last = it;
    pthread_mutex_unlock(&anim_list.mutex);
}

// anim_list.mutex must be locked
static void anim_list_rm(struct anim_list_it *it)
{
    if(it->prev)
        it->prev->next = it->next;
    else
        anim_list.first = it->next;

    if(it->next)
        it->next->prev = it->prev;
    else
        anim_list.last = it->prev;
}

// anim_list.mutex must be locked
static void anim_list_clear(void)
{
    struct anim_list_it *it, *next;
    for(next = anim_list.first; next; )
    {
        it = next;
        next = next->next;

        free(it->anim);
        free(it);
    }
    anim_list.first = anim_list.last = NULL;
}

#define OVERSHOOT_TENSION 2.f
static float anim_interpolate(int type, float input)
{
    switch(type)
    {
        default:
        case INTERPOLATOR_LINEAR:
            return input;
        case INTERPOLATOR_DECELERATE:
            return (1.f - (1.f - input) * (1.f - input));
        case INTERPOLATOR_ACCELERATE:
            return input * input;
        case INTERPOLATOR_OVERSHOOT:
            input -= 1.f;
            return (input * input * ((OVERSHOOT_TENSION+1.f) * input + OVERSHOOT_TENSION) + 1.f);
        case INTERPOLATOR_ACCEL_DECEL:
            return (float)(cos((input + 1) * M_PI) / 2.0f) + 0.5f;
    }
}

static inline void anim_int_step(int *prop, int *start, int *last, int *target, float interpolated)
{
    if(*target != -1)
    {
        const int diff = *prop - *last;
        *start += diff;
        *prop = *start + (int)((*target) * interpolated);
        *last = *prop;
    }
}

static inline int item_anim_is_on_screen(item_anim *anim)
{
    fb_item_header *it = anim->item;
    return it->x + it->w > 0 && it->x < fb_width &&
            it->y + it->h > 0 && it->y < fb_height;
}

static void item_anim_step(item_anim *anim, float interpolated, int *need_draw)
{
    int outside = !item_anim_is_on_screen(anim);

    fb_item_header *fb_it = anim->item;
    anim_int_step(&fb_it->x, &anim->start[0], &anim->last[0], &anim->targetX, interpolated);
    anim_int_step(&fb_it->y, &anim->start[1], &anim->last[1], &anim->targetY, interpolated);
    anim_int_step(&fb_it->w, &anim->start[2], &anim->last[2], &anim->targetW, interpolated);
    anim_int_step(&fb_it->h, &anim->start[3], &anim->last[3], &anim->targetH, interpolated);

    if(!(*need_draw) && (!outside || item_anim_is_on_screen(anim)))
        *need_draw = 1;
}

static void item_anim_on_start(item_anim *anim)
{
    fb_item_header *fb_it = anim->item;
    anim->start[0] = anim->last[0] = fb_it->x;
    anim->start[1] = anim->last[1] = fb_it->y;
    anim->start[2] = anim->last[2] = fb_it->w;
    anim->start[3] = anim->last[3] = fb_it->h;

    if(anim->targetX != -1)
        anim->targetX -= fb_it->x;
    if(anim->targetY != -1)
        anim->targetY -= fb_it->y;
    if(anim->targetW != -1)
        anim->targetW -= fb_it->w;
    if(anim->targetH != -1)
        anim->targetH -= fb_it->h;
}

static void item_anim_on_finished(item_anim *anim)
{
    if(anim->destroy_item_when_finished)
        fb_remove_item(anim->item);
}

static void call_anim_step(call_anim *anim, float interpolated)
{
    if(anim->callback)
        anim->callback(anim->data, interpolated);
}

static void anim_update(uint32_t diff, void *data)
{
    struct anim_list *list = data;
    struct anim_list_it *it;
    anim_header *anim;
    float normalized, interpolated;
    int need_draw = 0;

    pthread_mutex_lock(&list->mutex);
    list->in_update_loop = 1;
    
    for(it = list->first; it; )
    {
        anim = it->anim;

        // Handle offset
        if(anim->start_offset)
        {
            if(anim->start_offset > diff)
                anim->start_offset -= diff;
            else
                anim->start_offset = 0;
            it = it->next;
            continue;
        }

        // calculate interpolation
        anim->elapsed += diff;
        if(anim->elapsed >= anim->duration)
            normalized = 1.f;
        else
            normalized = ((float)anim->elapsed)/anim->duration;

        interpolated = anim_interpolate(anim->interpolator, normalized);

        // Handle animation step
        switch(it->anim_type)
        {
            case ANIM_TYPE_ITEM:
                item_anim_step((item_anim*)anim, interpolated, &need_draw);
                break;
            case ANIM_TYPE_CALLBACK:
                call_anim_step((call_anim*)anim, interpolated);
                break;
        }

        if(anim->on_step_call)
        {
            pthread_mutex_unlock(&list->mutex);
            anim->on_step_call(anim->on_step_data, interpolated);
            pthread_mutex_lock(&list->mutex);
        }

        // remove complete animations
        if(anim->elapsed >= anim->duration)
        {
            if(anim->on_finished_call)
            {
                pthread_mutex_unlock(&list->mutex);
                anim->on_finished_call(anim->on_finished_data);
                pthread_mutex_lock(&list->mutex);
            }

            switch(it->anim_type)
            {
                case ANIM_TYPE_ITEM:
                    pthread_mutex_unlock(&list->mutex);
                    item_anim_on_finished((item_anim*)anim);
                    pthread_mutex_lock(&list->mutex);
                    break;
            }

            struct anim_list_it *to_remove = it;
            it = it->next;
            anim_list_rm(to_remove);
            free(to_remove->anim);
            free(to_remove);
        }
        else
            it = it->next;
    }

    if(need_draw)
        fb_request_draw();

    list->in_update_loop = 0;
    pthread_mutex_unlock(&list->mutex);
}

static uint32_t anim_generate_id(void)
{
    static uint32_t id = 0;
    uint32_t res = id++;
    if(res == ANIM_INVALID_ID)
        res = id++;
    return res;
}

void anim_init(float duration_coef)
{
    if(anim_list.running)
        return;

    anim_list.running = 1;
    anim_list.duration_coef = duration_coef;
    workers_add(&anim_update, &anim_list);
}

void anim_stop(int wait_for_finished)
{
    if(!anim_list.running)
        return;

    anim_list.running = 0;
    while(wait_for_finished)
    {
        pthread_mutex_lock(&anim_list.mutex);
        if(!anim_list.first)
        {
            pthread_mutex_unlock(&anim_list.mutex);
            break;
        }
        pthread_mutex_unlock(&anim_list.mutex);
        usleep(10000);
    }

    workers_remove(&anim_update, &anim_list);

    pthread_mutex_lock(&anim_list.mutex);
    anim_list_clear();
    pthread_mutex_unlock(&anim_list.mutex);
}

void anim_cancel(uint32_t id, int only_not_started)
{
    if(!anim_list.running)
        return;

    struct anim_list_it *it;

    pthread_mutex_lock(&anim_list.mutex);
    for(it = anim_list.first; it; )
    {
        if(it->anim->id == id && (!only_not_started || it->anim->start_offset == 0))
        {
            anim_list_rm(it);
            free(it->anim);
            free(it);
            break;
        }
        else
            it = it->next;
    }
    pthread_mutex_unlock(&anim_list.mutex);
}

void anim_cancel_for(void *fb_item, int only_not_started)
{
    if(!anim_list.running)
        return;

    if(anim_list.in_update_loop && pthread_equal(pthread_self(), workers_get_thread_id()))
        return;

    struct anim_list_it *it, *to_remove;
    anim_header *anim;

    pthread_mutex_lock(&anim_list.mutex);
    for(it = anim_list.first; it; )
    {
        anim = it->anim;

        if(!anim->cancel_check || (only_not_started && anim->start_offset == 0))
        {
            it = it->next;
            continue;
        }

        if(anim->cancel_check(anim->cancel_check_data, fb_item))
        {
            to_remove = it;
            it = it->next;
            anim_list_rm(to_remove);
            free(to_remove->anim);
            free(to_remove);
        }
        else
            it = it->next;
    }
    pthread_mutex_unlock(&anim_list.mutex);
}

void anim_push_context(void)
{
    pthread_mutex_lock(&anim_list.mutex);
    if(anim_list.first)
    {
        list_add(&anim_list.inactive_ctx, anim_list.first);
        anim_list.first = anim_list.last = NULL;
    }
    else
    {
        list_add(&anim_list.inactive_ctx, &EMPTY_CONTEXT);
    }
    pthread_mutex_unlock(&anim_list.mutex);
}

void anim_pop_context(void)
{
    pthread_mutex_lock(&anim_list.mutex);
    if(!anim_list.inactive_ctx)
    {
        pthread_mutex_unlock(&anim_list.mutex);
        return;
    }

    if(anim_list.first)
        anim_list_clear();

    const int idx = list_item_count(anim_list.inactive_ctx)-1;
    struct anim_list_it *last_active_ctx = anim_list.inactive_ctx[idx];
    if(last_active_ctx != &EMPTY_CONTEXT)
    {
        anim_list.first = last_active_ctx;
        while(last_active_ctx->next)
            last_active_ctx = last_active_ctx->next;
        anim_list.last = last_active_ctx;
    }
    list_rm_at(&anim_list.inactive_ctx, idx, NULL);
    pthread_mutex_unlock(&anim_list.mutex);
}

int anim_item_cancel_check(void *item_my, void *item_destroyed)
{
    return item_my == item_destroyed;
}

item_anim *item_anim_create(void *fb_item, int duration, int interpolator)
{
    item_anim *anim = mzalloc(sizeof(item_anim));
    anim->id = anim_generate_id();
    anim->item = fb_item;
    anim->duration = duration * anim_list.duration_coef;
    anim->interpolator = interpolator;
    anim->cancel_check_data = fb_item;
    anim->cancel_check = anim_item_cancel_check;
    anim->targetX = -1;
    anim->targetY = -1;
    anim->targetW = -1;
    anim->targetH = -1;
    return anim;
}

void item_anim_add(item_anim *anim)
{
    if(!anim_list.running)
    {
        free(anim);
        return;
    }

    item_anim_on_start(anim);

    struct anim_list_it *it = mzalloc(sizeof(struct anim_list_it));
    it->anim_type = ANIM_TYPE_ITEM;
    it->anim = (anim_header*)anim;
    anim_list_append(it);
}

void item_anim_add_after(item_anim *anim)
{
    struct anim_list_it *it;
    pthread_mutex_lock(&anim_list.mutex);
    for(it = anim_list.first; it; it = it->next)
    {
        if(it->anim_type == ANIM_TYPE_ITEM && ((item_anim*)it->anim)->item == anim->item)
        {
            const int u = it->anim->start_offset + it->anim->duration - it->anim->elapsed;
            anim->start_offset = imax(anim->start_offset, u);
        }
    }
    pthread_mutex_unlock(&anim_list.mutex);

    item_anim_add(anim);
}

call_anim *call_anim_create(void *data, call_anim_callback callback, int duration, int interpolator)
{
    call_anim *anim = mzalloc(sizeof(call_anim));
    anim->id = anim_generate_id();
    anim->data = data;
    anim->callback = callback;
    anim->duration = duration * anim_list.duration_coef;
    anim->interpolator = interpolator;
    return anim;
}

void call_anim_add(call_anim *anim)
{
    if(!anim_list.running)
    {
        free(anim);
        return;
    }

    struct anim_list_it *it = mzalloc(sizeof(struct anim_list_it));
    it->anim_type = ANIM_TYPE_CALLBACK;
    it->anim = (anim_header*)anim;
    anim_list_append(it);
}
