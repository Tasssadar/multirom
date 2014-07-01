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
    volatile int in_update_loop;
    pthread_mutex_t mutex;
};

static struct anim_list_it EMPTY_CONTEXT;

static struct anim_list anim_list = {
    .first = NULL,
    .last = NULL,
    .inactive_ctx = NULL,
    .running = 0,
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
    }
}

static inline void anim_int_step(int *prop, int start, int target, float interpolated)
{
    if(target != -1)
        *prop = start + (int)((target - start) * interpolated);
}

static void item_anim_step(item_anim *anim, float interpolated)
{
    fb_item_header *fb_it = anim->item;
    anim_int_step(&fb_it->x, anim->startX, anim->targetX, interpolated);
    anim_int_step(&fb_it->y, anim->startY, anim->targetY, interpolated);
    anim_int_step(&fb_it->w, anim->startW, anim->targetW, interpolated);
    anim_int_step(&fb_it->h, anim->startH, anim->targetH, interpolated);
    fb_request_draw();
}

static void item_anim_on_start(item_anim *anim)
{
    fb_item_header *fb_it = anim->item;
    anim->startX = fb_it->x;
    anim->startY = fb_it->y;
    anim->startW = fb_it->w;
    anim->startH = fb_it->h;
}

static void item_anim_on_finished(item_anim *anim)
{
    if(anim->destroy_item_when_finished)
        fb_remove_item(anim->item);
}

static void anim_update(uint32_t diff, void *data)
{
    struct anim_list *list = data;
    struct anim_list_it *it;
    anim_header *anim;
    float normalized, interpolated;

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
            {
                anim->start_offset = 0;
                switch(it->anim_type)
                {
                    case ANIM_TYPE_ITEM:
                        item_anim_on_start((item_anim*)anim);
                        break;
                }
            }
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
        INFO("Interpolate diff %u normalized %f interpolated %f\n", diff, normalized, interpolated);

        // Handle animation step
        switch(it->anim_type)
        {
            case ANIM_TYPE_ITEM:
                item_anim_step((item_anim*)anim, interpolated);
                break;
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
                    item_anim_on_finished((item_anim*)anim);
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

    list->in_update_loop = 0;
    pthread_mutex_unlock(&list->mutex);
}

void anim_init(void)
{
    if(anim_list.running)
        return;

    anim_list.running = 1;
    workers_add(&anim_update, &anim_list);
}

void anim_stop(void)
{
    if(!anim_list.running)
        return;

    anim_list.running = 0;
    workers_remove(&anim_update, &anim_list);

    pthread_mutex_lock(&anim_list.mutex);
    anim_list_clear();
    pthread_mutex_unlock(&anim_list.mutex);
}

void anim_fb_item_removed(void *item)
{
    if(!anim_list.running)
        return;

    if(anim_list.in_update_loop && pthread_equal(pthread_self(), workers_get_thread_id()))
        return;

    struct anim_list_it *it, *to_remove;
    item_anim *anim;

    pthread_mutex_lock(&anim_list.mutex);
    for(it = anim_list.first; it; )
    {
        if(it->anim_type == ANIM_TYPE_ITEM && ((item_anim*)it->anim)->item == item)
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
        list_add(anim_list.first, &anim_list.inactive_ctx);
        anim_list.first = anim_list.last = NULL;
    }
    else
    {
        list_add(&EMPTY_CONTEXT, &anim_list.inactive_ctx);
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
    list_rm_at(idx, &anim_list.inactive_ctx, NULL);
    pthread_mutex_unlock(&anim_list.mutex);
}

item_anim *item_anim_create(void *fb_item, int duration, int interpolator)
{
    item_anim *anim = mzalloc(sizeof(item_anim));
    anim->item = fb_item;
    anim->duration = duration;
    anim->interpolator = interpolator;
    anim->targetX = -1;
    anim->targetY = -1;
    anim->targetW = -1;
    anim->targetH = -1;
    return anim;
}

void item_anim_add(item_anim *anim)
{
    if(!anim->start_offset)
        item_anim_on_start(anim);

    struct anim_list_it *it = mzalloc(sizeof(struct anim_list_it));
    it->anim_type = ANIM_TYPE_ITEM;
    it->anim = (anim_header*)anim;
    anim_list_append(it);
}
