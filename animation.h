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
};

enum
{
    INTERPOLATOR_LINEAR,
    INTERPOLATOR_DECELERATE,
    INTERPOLATOR_ACCELERATE,
    INTERPOLATOR_OVERSHOOT,
};

typedef void (*animation_callback)(void*); // data

#define ANIM_HEADER \
    uint32_t start_offset; \
    uint32_t duration; \
    uint32_t elapsed; \
    int interpolator; \
    void *on_finished_data; \
    animation_callback on_finished_call;

typedef struct 
{
    ANIM_HEADER
} anim_header;

typedef struct
{
    ANIM_HEADER
    void *item;

    int destroy_item_when_finished;

    int startX, startY;
    int startW, startH;

    int targetX, targetY;
    int targetW, targetH;
} item_anim;

void anim_init(void);
void anim_stop(void);
void anim_fb_item_removed(void *item);
void anim_push_context(void);
void anim_pop_context(void);

item_anim *item_anim_create(void *fb_item, int duration, int interpolator);
void item_anim_add(item_anim *anim);

#endif
