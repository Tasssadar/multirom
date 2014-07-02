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

#ifndef ANIMATION_H
#define ANIMATION_H

enum
{
    ANIM_TYPE_ITEM,
    ANIM_TYPE_CALLBACK,
};

enum
{
    INTERPOLATOR_LINEAR,
    INTERPOLATOR_DECELERATE,
    INTERPOLATOR_ACCELERATE,
    INTERPOLATOR_OVERSHOOT,
    INTERPOLATOR_ACCEL_DECEL,
};

typedef void (*animation_callback)(void*); // data

#define ANIM_HEADER \
    uint32_t start_offset; \
    uint32_t duration; \
    uint32_t elapsed; \
    int interpolator; \
    void *on_finished_data; \
    animation_callback on_finished_call; \
    void *on_step_data; \
    animation_callback on_step_call;

typedef struct 
{
    ANIM_HEADER
} anim_header;

typedef struct
{
    ANIM_HEADER
    void *item;

    int destroy_item_when_finished;

    int start[4];
    int last[4];

    int targetX, targetY;
    int targetW, targetH;
} item_anim;

typedef void (*call_anim_callback)(void*, float); // data, interpolated
typedef struct
{
    ANIM_HEADER

    call_anim_callback callback;
    void *data;
} call_anim;

void anim_init(void);
void anim_stop(void);
void anim_cancel_for(void *fb_item, int only_not_started);
void anim_push_context(void);
void anim_pop_context(void);

item_anim *item_anim_create(void *fb_item, int duration, int interpolator);
void item_anim_add(item_anim *anim);
void item_anim_add_after(item_anim *anim);

call_anim *call_anim_create(void *data, call_anim_callback callback, int duration, int interpolator);
void call_anim_add(call_anim *anim);


#endif
