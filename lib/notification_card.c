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

#include "util.h"
#include "animation.h"
#include "framebuffer.h"
#include "notification_card.h"
#include "containers.h"
#include "log.h"
#include "input.h"
#include "colors.h"

enum
{
    LEVEL_NCARD_SHADOW = 49,
    LEVEL_NCARD_BG = 50,
    LEVEL_NCARD_BTN_HOVER = 55,
    LEVEL_NCARD_TEXT = 60,

    LEVEL_NCARD_CENTER_OFFSET = 1000,
};

#define CARD_PADDING_H (40*DPI_MUL)
#define CARD_PADDING_V (30*DPI_MUL)
#define CARD_MARGIN  (40*DPI_MUL)
#define CARD_WIDTH (fb_width - CARD_MARGIN*2)
#define CARD_SHADOW_OFF (7*DPI_MUL)

ncard_builder *ncard_create_builder(void)
{
    return mzalloc(sizeof(ncard_builder));
}

void ncard_set_title(ncard_builder *b, const char *title)
{
    b->title = realloc(b->title, strlen(title)+1);
    strcpy(b->title, title);
}

void ncard_set_text(ncard_builder *b, const char *text)
{
    b->text = realloc(b->text, strlen(text)+1);
    strcpy(b->text, text);
}

void ncard_set_pos(ncard_builder *b, int pos)
{
    b->pos = pos;
}

void ncard_set_cancelable(ncard_builder *b, int cancelable)
{
    b->cancelable = cancelable;
}

void ncard_avoid_item(ncard_builder *b, void *item)
{
    fb_item_pos *it = item;
    b->avoid_item = mzalloc(sizeof(fb_item_pos));
    b->avoid_item->x = it->x;
    b->avoid_item->y = it->y;
    b->avoid_item->w = it->w;
    b->avoid_item->h = it->h;
}

void ncard_add_btn(ncard_builder *b, int btn_type, const char *text, ncard_callback callback, void *callback_data)
{
    if(b->buttons[btn_type])
    {
        free(b->buttons[btn_type]->text);
        free(b->buttons[btn_type]);
    }

    ncard_builder_btn *btn = mzalloc(sizeof(ncard_builder_btn));
    btn->text = strtoupper(text);
    btn->callback_data = callback_data;
    btn->callback = callback;
    b->buttons[btn_type] = btn;
}

void ncard_set_on_hidden(ncard_builder *b, ncard_callback callback, void *data)
{
    b->on_hidden_call = callback;
    b->on_hidden_data = data;
}

void ncard_set_from_black(ncard_builder *b, int from_black)
{
    b->reveal_from_black = from_black;
}

static int ncard_calc_pos(ncard_builder* b, int max_y)
{
    if(b->pos == NCARD_POS_AUTO)
    {
        if(!b->avoid_item)
            return NCARD_POS_TOP;
        if(b->avoid_item->y > max_y)
            return NCARD_POS_TOP;
        return NCARD_POS_BOTTOM;
    }
    else
        return b->pos;
}

struct ncard_btn
{
    fb_item_pos pos;
    void *callback_data;
    ncard_callback callback;
};

struct ncard
{
    fb_rect *bg;
    fb_rect *shadow;
    fb_rect *alpha_bg;
    fb_text **texts;
    fb_rect *hover_rect;
    struct ncard_btn btns[BTN_COUNT];
    int active_btns;
    int pos;
    int targetH;
    int top_offset;
    int last_y;
    int hiding;
    int moving;
    int touch_handler_registered;
    int touch_id;
    int hover_btn;
    int cancelable;
    ncard_callback on_hidden_call;
    void *on_hidden_data;
    int reveal_from_black;
    pthread_mutex_t mutex;
} ncard = {
    .bg = NULL,
    .shadow = NULL,
    .texts = NULL,
    .active_btns = 0,
    .top_offset = 0,
    .hiding = 0,
    .hover_rect = NULL,
    .touch_handler_registered = 0,
    .touch_id = -1,
    .hover_btn = 0,
    .cancelable = 0,
    .on_hidden_call = NULL,
    .on_hidden_data = NULL,
    .reveal_from_black = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

static int ncard_touch_handler(touch_event *ev, void *data)
{
    struct ncard *c = data;

    pthread_mutex_lock(&c->mutex);

    if(c->touch_id == -1 && (ev->changed & TCHNG_ADDED))
    {
        int i;
        for(i = 0; i < BTN_COUNT; ++i)
        {
            if(!(c->active_btns & (1 << i)))
                continue;

            if(in_rect(ev->x, ev->y, c->btns[i].pos.x, c->btns[i].pos.y, c->btns[i].pos.w, c->btns[i].pos.h))
            {
                fb_rm_rect(c->hover_rect);
                int level = LEVEL_NCARD_BTN_HOVER;
                if(c->pos == NCARD_POS_CENTER)
                    level += LEVEL_NCARD_CENTER_OFFSET;
                c->hover_rect = fb_add_rect_lvl(level, c->btns[i].pos.x, c->btns[i].pos.y, c->btns[i].pos.w, c->btns[i].pos.h, C_NCARD_SHADOW);

                c->touch_id = ev->id;
                c->hover_btn = i;
                break;
            }
        }
    }

    if(c->touch_id != ev->id)
    {
        if(c->cancelable)
        {
            pthread_mutex_unlock(&c->mutex);
            ncard_hide();
            return 0;
        }
        else
        {
            pthread_mutex_unlock(&c->mutex);
            return c->pos == NCARD_POS_CENTER ? 0 : -1;
        }
    }

    if(ev->changed & TCHNG_REMOVED)
    {
        struct ncard_btn *b = &c->btns[c->hover_btn];
        if(b->callback && in_rect(ev->x, ev->y, b->pos.x, b->pos.y, b->pos.w, b->pos.h))
        {
            ncard_callback call = b->callback;
            void *call_data = b->callback_data;
            pthread_mutex_unlock(&c->mutex);
            call(call_data);
            pthread_mutex_lock(&c->mutex);
        }
        else
        {
            fb_rm_rect(c->hover_rect);
            c->hover_rect = NULL;
        }
        c->touch_id = -1;
    }

    pthread_mutex_unlock(&c->mutex);
    return 0;
}

static void ncard_move_step(void *data, float interpolated)
{
    int i;
    struct ncard *c = data;

    pthread_mutex_lock(&c->mutex);

    const int diff = c->bg->y - c->last_y;

    for(i = 0; c->texts && c->texts[i]; ++i)
        c->texts[i]->y += diff;

    for(i = 0; i < BTN_COUNT; ++i)
    {
        if(!(c->active_btns & (1 << i)))
            continue;
        c->btns[i].pos.y += diff;
    }

    c->shadow->y += diff;
    c->shadow->h = c->bg->h;
    if(c->hover_rect)
        c->hover_rect->y += diff;
    c->last_y = c->bg->y;

    if(c->alpha_bg && (c->hiding || (c->alpha_bg->color & (0xFF << 24)) != 0xCC000000))
    {
        if(interpolated > 1.f)
            interpolated = 1.f;
        if(c->hiding)
            interpolated = 1.f - interpolated;

        if(!c->hiding && c->reveal_from_black)
            c->alpha_bg->color = (c->alpha_bg->color & ~(0xFF << 24)) | ((0xFF - (int)(0x33*interpolated)) << 24);
        else
            c->alpha_bg->color = (c->alpha_bg->color & ~(0xFF << 24)) | (((int)(0xCC*interpolated)) << 24);
    }

    pthread_mutex_unlock(&c->mutex);

    fb_request_draw();
}

static void ncard_reveal_finished(UNUSED void *data)
{
    pthread_mutex_lock(&ncard.mutex);
    ncard.bg->h = ncard.targetH;
    ncard.shadow->h = ncard.targetH;
    ncard.moving = 0;
    pthread_mutex_unlock(&ncard.mutex);
}

static void ncard_hide_finished(void *data)
{
    struct ncard *c = data;
    list_clear(&c->texts, fb_remove_item);
    fb_rm_rect(c->shadow);
    fb_rm_rect(c->alpha_bg);
    fb_rm_rect(c->hover_rect);
    free(c);
    ncard.moving = 0;
}

void ncard_set_top_offset(int top_offset)
{
    pthread_mutex_lock(&ncard.mutex);
    ncard.top_offset = top_offset;
    pthread_mutex_unlock(&ncard.mutex);
}

void ncard_show(ncard_builder *b, int destroy_builder)
{
    int i, items_h, btn_x, btn_h, has_btn = 0, it_y = 0, lvl_offset = 0;
    fb_text *title = 0, *text = 0, *btns[BTN_COUNT];
    int interpolator;

    pthread_mutex_lock(&ncard.mutex);

    if(ncard.bg)
        anim_cancel_for(ncard.bg, 0);

    if(b->pos == NCARD_POS_CENTER)
        lvl_offset = LEVEL_NCARD_CENTER_OFFSET;

    items_h = CARD_PADDING_V*2;
    if(b->title)
    {
        fb_text_proto *p = fb_text_create(CARD_MARGIN + CARD_PADDING_H, fb_height, C_NCARD_TEXT, SIZE_EXTRA, b->title);
        p->level = LEVEL_NCARD_TEXT + lvl_offset;
        p->style = STYLE_MEDIUM;
        p->wrap_w = CARD_WIDTH - CARD_PADDING_H*2;
        title = fb_text_finalize(p);
        items_h += title->h;
    }

    if(b->text)
    {
        fb_text_proto *p = fb_text_create(CARD_MARGIN + CARD_PADDING_H, fb_height, C_NCARD_TEXT_SECONDARY, SIZE_NORMAL, b->text);\
        p->level = LEVEL_NCARD_TEXT + lvl_offset;
        p->wrap_w = CARD_WIDTH - CARD_PADDING_H*2;
        if(!title)
        {
            p->style = STYLE_ITALIC;
            p->justify = JUSTIFY_CENTER;
        }
        text = fb_text_finalize(p);
        items_h += text->h;
    }

    if(title && text)
        items_h += title->h;

    ncard.active_btns = 0;
    btn_x = CARD_MARGIN + CARD_WIDTH - CARD_PADDING_H;
    btn_h = 0;
    for(i = 0; i < BTN_COUNT; ++i)
    {
        if(!b->buttons[i])
            continue;

        ncard.active_btns |= (1 << i);

        fb_text_proto *p = fb_text_create(btn_x, fb_height, C_NCARD_TEXT, SIZE_NORMAL, b->buttons[i]->text);
        p->level = LEVEL_NCARD_TEXT + lvl_offset;
        p->style = STYLE_MEDIUM;
        fb_text *t = fb_text_finalize(p);
        t->x -= t->w;
        btn_x -= t->w + t->h*2;
        btn_h = imax(t->h*2, btn_h);
        btns[i] = t;

        ncard.btns[i].callback_data = b->buttons[i]->callback_data;
        ncard.btns[i].callback = b->buttons[i]->callback;
        ncard.btns[i].pos.w = t->w + t->h*2;
        ncard.btns[i].pos.h = t->h*3;
        ncard.btns[i].pos.x = btn_x + t->h;
    }

    items_h += btn_h*1.25;

    int new_pos = ncard_calc_pos(b, ncard.top_offset + items_h + CARD_MARGIN);

    if(new_pos != ncard.pos && ncard.bg)
    {
        pthread_mutex_unlock(&ncard.mutex);
        ncard_hide();
        pthread_mutex_lock(&ncard.mutex);
    }

    ncard.pos = new_pos;

    list_clear(&ncard.texts, fb_remove_item);
    fb_rm_rect(ncard.hover_rect);
    ncard.hover_rect = NULL;

    if(!ncard.bg)
    {
        ncard.bg = fb_add_rect_lvl(LEVEL_NCARD_BG + lvl_offset, CARD_MARGIN, 0, CARD_WIDTH, items_h, C_NCARD_BG);
        ncard.bg->y = ncard.pos == NCARD_POS_BOTTOM ? (int)fb_height : -items_h;
        ncard.shadow = fb_add_rect_lvl(LEVEL_NCARD_SHADOW + lvl_offset, CARD_MARGIN + CARD_SHADOW_OFF, 0, CARD_WIDTH, items_h, C_NCARD_SHADOW);
        interpolator = INTERPOLATOR_OVERSHOOT;
    }
    else
        interpolator = INTERPOLATOR_ACCEL_DECEL;

    ncard.targetH = items_h;
    if(ncard.pos != NCARD_POS_CENTER)
        ncard.targetH *= 1.3;
    else if(!ncard.alpha_bg)
    {
        ncard.alpha_bg = fb_add_rect_lvl(LEVEL_NCARD_SHADOW + lvl_offset - 1, 0, 0, fb_width, fb_height,
                b->reveal_from_black ? BLACK : 0x00000000);
    }

    if(items_h >= ncard.bg->h)
    {
        ncard.bg->h = ncard.targetH;
        ncard.shadow->h = ncard.bg->h;
    }

    it_y = ncard.bg->y + CARD_PADDING_V;
    if(ncard.pos == NCARD_POS_TOP)
        it_y += ncard.bg->h - items_h;

    if(title)
    {
        title->y = it_y;
        it_y += title->h*1.5;
        list_add(&ncard.texts, title);
    }

    if(text)
    {
        text->y = it_y;
        it_y += text->h + btn_h*0.75;
        if(!title)
            center_text(text, 0, -1, fb_width, -1);
        list_add(&ncard.texts, text);
    }

    for(i = 0; i < BTN_COUNT; ++i)
    {
        if(!(ncard.active_btns & (1 << i)))
            continue;
        btns[i]->y = it_y;
        ncard.btns[i].pos.y = it_y - ncard.btns[i].pos.h/3;
        list_add(&ncard.texts, btns[i]);
    }

    if(ncard.active_btns && !ncard.touch_handler_registered)
    {
        add_touch_handler_async(ncard_touch_handler, &ncard);
        ncard.touch_handler_registered = 1;
    }
    else if(!ncard.active_btns && ncard.touch_handler_registered)
    {
        rm_touch_handler_async(ncard_touch_handler, &ncard);
        ncard.touch_handler_registered = 0;
    }

    ncard.shadow->y = ncard.pos == NCARD_POS_BOTTOM ? ncard.bg->y - CARD_SHADOW_OFF : ncard.bg->y + CARD_SHADOW_OFF;

    ncard.last_y = ncard.bg->y;
    ncard.cancelable = b->cancelable;
    ncard.on_hidden_call = b->on_hidden_call;
    ncard.on_hidden_data = b->on_hidden_data;
    ncard.reveal_from_black = b->reveal_from_black;

    item_anim *a = item_anim_create(ncard.bg, 400, interpolator);
    switch(ncard.pos)
    {
        case NCARD_POS_TOP:
            a->targetY = ncard.top_offset - (ncard.bg->h - items_h);
            break;
        case NCARD_POS_BOTTOM:
            a->targetY = fb_height - items_h;
            break;
        case NCARD_POS_CENTER:
            a->targetY = fb_height/2 - items_h/2;
            break;
    }
    a->targetH = ncard.targetH;
    a->on_step_call = ncard_move_step;
    a->on_step_data = &ncard;
    a->on_finished_call = ncard_reveal_finished;
    item_anim_add(a);
    ncard.moving = 1;

    pthread_mutex_unlock(&ncard.mutex);

    if(destroy_builder)
        ncard_destroy_builder(b);
}

void ncard_hide(void)
{
    if(!ncard.bg)
        return;

    anim_cancel_for(ncard.bg, 0);

    struct ncard *c = mzalloc(sizeof(struct ncard));
    pthread_mutex_lock(&ncard.mutex);
    c->bg = ncard.bg;
    c->shadow = ncard.shadow;
    c->hover_rect = ncard.hover_rect;
    c->texts = ncard.texts;
    c->last_y = c->bg->y;
    c->alpha_bg = ncard.alpha_bg;
    c->hiding = 1;
    ncard.moving = 1;
    ncard.shadow = NULL;
    ncard.hover_rect = NULL;
    ncard.bg = NULL;
    ncard.texts = NULL;
    ncard.alpha_bg = NULL;

    if(ncard.touch_handler_registered)
    {
        rm_touch_handler(ncard_touch_handler, &ncard);
        ncard.touch_handler_registered = 0;
    }

    pthread_mutex_unlock(&ncard.mutex);

    item_anim *a = item_anim_create(c->bg, 400, INTERPOLATOR_ACCELERATE);
    a->targetY = ncard.pos == NCARD_POS_TOP ? -c->bg->h : (int)fb_height + c->bg->h;
    a->destroy_item_when_finished = 1;
    a->on_step_call = ncard_move_step;
    a->on_step_data = c;
    a->on_finished_call = ncard_hide_finished;
    a->on_finished_data = c;
    item_anim_add(a);

    if(ncard.on_hidden_call)
        ncard.on_hidden_call(ncard.on_hidden_data);
}

void ncard_hide_callback(UNUSED void *data)
{
    ncard_hide();
}

void ncard_destroy_builder(ncard_builder *b)
{
    free(b->title);
    free(b->text);
    free(b->avoid_item);

    int i;
    for(i = 0; i < BTN_COUNT; ++i)
    {
        if(b->buttons[i])
        {
            free(b->buttons[i]->text);
            free(b->buttons[i]);
        }
    }
    free(b);
}

int ncard_try_cancel(void)
{
    if(ncard.bg && ncard.cancelable)
    {
        ncard_hide();
        return 1;
    }
    return 0;
}

int ncard_is_visible(void)
{
    return ncard.bg != NULL;
}

int ncard_is_moving(void)
{
    return ncard.moving == 1;
}
