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

#include "util.h"
#include "animation.h"
#include "framebuffer.h"
#include "notification_card.h"
#include "containers.h"
#include "log.h"

enum
{
    LEVEL_NCARD_SHADOW = 49,
    LEVEL_NCARD_BG = 50,
    LEVEL_NCARD_TEXT = 60
};

#define CARD_PADDING_H (40*DPI_MUL)
#define CARD_PADDING_V (30*DPI_MUL)
#define CARD_MARGIN  (40*DPI_MUL)
#define CARD_WIDTH (fb_width - CARD_MARGIN*2)
#define CARD_SHADOW_OFF (5*DPI_MUL)

ncard_builder *ncard_create_builder(void)
{
    return mzalloc(sizeof(ncard_builder));
}

void ncard_set_title(ncard_builder *b, const char *title)
{
    b->title = strdup(title);
}

void ncard_set_text(ncard_builder *b, const char *text)
{
    b->text = strdup(text);
}

void ncard_set_pos(ncard_builder *b, int pos)
{
    b->pos = pos;
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

void ncard_add_btn(ncard_builder *b, int btn_type, const char *text, ncard_btn_callback callback, void *callback_data)
{
    ncard_builder_btn *btn = mzalloc(sizeof(ncard_builder_btn));
    btn->text = strtoupper(text);
    btn->callback_data = callback_data;
    btn->callback = callback;
    b->buttons[btn_type] = btn;
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
    ncard_btn_callback callback;
};

struct ncard
{
    fb_rect *bg;
    fb_rect *shadow;
    fb_rect *alpha_bg;
    fb_text **texts;
    struct ncard_btn btns[BTN_COUNT];
    int active_btns;
    int pos;
    int targetH;
    int top_offset;
    int last_y;
} ncard = {
    .bg = NULL,
    .shadow = NULL,
    .texts = NULL,
    .active_btns = 0,
    .top_offset = 0
};

struct ncard_hide_data
{
    fb_text **texts;
    fb_rect *bg;
    fb_rect *shadow;
    fb_rect *alpha_bg;
    int last_y;
};

static void ncard_move_step(void *data, float interpolated)
{
    int i;
    const int diff = ncard.bg->y - ncard.last_y;
    for(i = 0; ncard.texts && ncard.texts[i]; ++i)
        ncard.texts[i]->y += diff;
    ncard.shadow->y += diff;
    ncard.last_y = ncard.bg->y;

    if(ncard.alpha_bg && (ncard.alpha_bg->color & (0xFF << 24)) != 0xCC000000)
    {
        if(interpolated > 1.f)
            interpolated = 1.f;
        ncard.alpha_bg->color = (ncard.alpha_bg->color & ~(0xFF << 24)) | (((int)(0xCC*interpolated)) << 24);
    }
}

static void ncard_reveal_finished(void *data)
{
    ncard.bg->h = ncard.targetH;
}

static void ncard_hide_step(void *data, float interpolated)
{
    struct ncard_hide_data *d = data;   
    int i;
    const int diff = d->bg->y - d->last_y;
    for(i = 0; d->texts && d->texts[i]; ++i)
        d->texts[i]->y += diff;
    d->shadow->y += diff;
    d->last_y = d->bg->y;
    if(d->alpha_bg)
    {
        if(interpolated > 1)
            interpolated = 1;
        interpolated = 1 - interpolated;
        d->alpha_bg->color = (d->alpha_bg->color & ~(0xFF << 24)) | (((int)(0xCC*interpolated)) << 24);
    }
}

static void ncard_hide_finished(void *data)
{
    struct ncard_hide_data *d = data;
    list_clear(&d->texts, fb_remove_item);
    fb_rm_rect(d->shadow);
    fb_rm_rect(d->alpha_bg);
    free(d);
}

void ncard_set_top_offset(int top_offset)
{
    ncard.top_offset = top_offset;
}

void ncard_show(ncard_builder *b, int destroy_builder)
{
    int i, items_h, btn_x, btn_h, has_btn = 0, it_y = 0;
    fb_text *title = 0, *text = 0, *btns[BTN_COUNT];
    int interpolator;

    if(ncard.bg)
        anim_cancel_for(ncard.bg, 0);

    items_h = CARD_PADDING_V*2;
    if(b->title)
    {
        fb_text_proto *p = fb_text_create(CARD_MARGIN + CARD_PADDING_H, 0, WHITE, SIZE_EXTRA, b->title);
        p->level = LEVEL_NCARD_TEXT;
        p->style = STYLE_MEDIUM;
        title = fb_text_finalize(p);
        items_h += title->h;
    }

    if(b->text)
    {
        fb_text_proto *p = fb_text_create(CARD_MARGIN + CARD_PADDING_H, 0, 0xFFE6E6E6, SIZE_NORMAL, b->text);\
        p->level = LEVEL_NCARD_TEXT;
        if(!title)
            p->style = STYLE_ITALIC;
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

        fb_text_proto *p = fb_text_create(btn_x, 0, WHITE, SIZE_NORMAL, b->buttons[i]->text);
        p->level = LEVEL_NCARD_TEXT;
        p->style = STYLE_MEDIUM;
        fb_text *t = fb_text_finalize(p);
        t->x -= t->w + t->h;
        btn_x -= t->w + t->h*2;
        btn_h = imax(t->h*2, btn_h);
        btns[i] = t;
    }

    items_h += btn_h*1.5;

    int new_pos = ncard_calc_pos(b, ncard.top_offset + items_h + CARD_MARGIN);

    if(new_pos != ncard.pos && ncard.bg)
        ncard_hide();
    ncard.pos = new_pos;

    list_clear(&ncard.texts, fb_remove_item);
    if(!ncard.bg)
    {
        ncard.bg = fb_add_rect_lvl(LEVEL_NCARD_BG, CARD_MARGIN, 0, CARD_WIDTH, items_h, 0xFF4f4737);
        ncard.bg->y = ncard.pos == NCARD_POS_BOTTOM ? fb_height : -items_h;
        ncard.shadow = fb_add_rect_lvl(LEVEL_NCARD_SHADOW, CARD_MARGIN + CARD_SHADOW_OFF, 0, CARD_WIDTH, items_h, GRAYISH);
        interpolator = INTERPOLATOR_OVERSHOOT;
    }
    else
        interpolator = INTERPOLATOR_ACCEL_DECEL;

    ncard.targetH = items_h;
    if(ncard.pos != NCARD_POS_CENTER)
        ncard.targetH *= 1.3;
    else if(!ncard.alpha_bg)
        ncard.alpha_bg = fb_add_rect_lvl(LEVEL_NCARD_SHADOW-1, 0, 0, fb_width, fb_height, 0x00000000);

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
        it_y += title->h*2;
        list_add(title, &ncard.texts);
    }

    if(text)
    {
        text->y = it_y;
        it_y += text->h + btn_h*0.5;
        if(!title)
            center_text(text, 0, -1, fb_width, -1);
        list_add(text, &ncard.texts);
    }

    for(i = 0; i < BTN_COUNT; ++i)
    {
        if(!(ncard.active_btns & (1 << i)))
            continue;
        btns[i]->y = it_y;
        list_add(btns[i], &ncard.texts);
    }

    ncard.shadow->y = ncard.pos == NCARD_POS_BOTTOM ? ncard.bg->y - CARD_SHADOW_OFF : ncard.bg->y + CARD_SHADOW_OFF;

    ncard.last_y = ncard.bg->y;
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
    a->on_step_call = ncard_move_step;
    a->on_finished_call = ncard_reveal_finished;
    item_anim_add(a);

    if(destroy_builder)
        ncard_destroy_builder(b);
}

void ncard_hide(void)
{
    if(!ncard.bg)
        return;

    anim_cancel_for(ncard.bg, 0);

    struct ncard_hide_data *d = mzalloc(sizeof(struct ncard_hide_data));
    d->bg = ncard.bg;
    d->shadow = ncard.shadow;
    d->texts = ncard.texts;
    d->last_y = d->bg->y;
    d->alpha_bg = ncard.alpha_bg;
    ncard.shadow = NULL;
    ncard.bg = NULL;
    ncard.texts = NULL;
    ncard.alpha_bg = NULL;

    item_anim *a = item_anim_create(d->bg, 400, INTERPOLATOR_ACCELERATE);
    a->targetY = ncard.pos == NCARD_POS_TOP ? -d->bg->h : fb_height + d->bg->h;
    a->destroy_item_when_finished = 1;
    a->on_step_call = ncard_hide_step;
    a->on_step_data = d;
    a->on_finished_call = ncard_hide_finished;
    a->on_finished_data = d;
    item_anim_add(a);
}

void ncard_destroy_builder(ncard_builder *b)
{
    free(b->title);
    free(b->text);

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

