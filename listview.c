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

#include "listview.h"
#include "framebuffer.h"
#include "util.h"
#include "log.h"
#include "checkbox.h"
#include "multirom_ui.h"
#include "workers.h"
#include "input.h"
#include "animation.h"

#define MARK_W (10*DPI_MUL)
#define MARK_H (50*DPI_MUL)
#define PADDING (35*DPI_MUL)
#define LINE_W (2*DPI_MUL)
#define SCROLL_DIST (20*DPI_MUL)
#define OVERSCROLL_H (130*DPI_MUL)
#define OVERSCROLL_MARK_H (4*DPI_MUL)
#define OVERSCROLL_RETURN_SPD (10*DPI_MUL)

static void listview_bounceback(uint32_t diff, void *data)
{
    listview *v = (listview*)data;
    const int max = v->fullH - v->h;

    int step;
    if(v->pos < 0)
    {
        step = imin(-v->pos, OVERSCROLL_RETURN_SPD);
        listview_update_overscroll_mark(v, 0, -(v->pos+step));
    }
    else if(v->pos > max)
    {
        step = -imin(v->pos-max, OVERSCROLL_RETURN_SPD);
        listview_update_overscroll_mark(v, 1, (v->pos - max + step));
    }
    else
    {
        if(v->overscroll_marks[0]->w != 0)
            v->overscroll_marks[0]->w = 0;
        if(v->overscroll_marks[1]->w != 0)
            v->overscroll_marks[1]->w = 0;
        return;
    }

    if(v->touch.id == -1)
        listview_scroll_by(v, step);
}

void listview_init_ui(listview *view)
{
    int x = view->x + view->w - PADDING/2 - LINE_W/2;

    fb_rect *scroll_line = fb_add_rect(x, view->y, LINE_W, view->h, GRAY);
    scroll_line->parent = (fb_item_pos*)view;
    list_add(scroll_line, &view->ui_items);

    view->keyact_item_selected = -1;

    view->touch.id = -1;
    view->touch.last_y = -1;

    add_touch_handler(&listview_touch_handler, view);
}

void listview_destroy(listview *view)
{
    workers_remove(listview_bounceback, view);

    rm_touch_handler(&listview_touch_handler, view);

    listview_clear(view);
    list_clear(&view->ui_items, &fb_remove_item);

    fb_rm_rect(view->scroll_mark);
    fb_rm_rect(view->overscroll_marks[0]);
    fb_rm_rect(view->overscroll_marks[1]);

    free(view);
}

listview_item *listview_add_item(listview *view, int id, void *data)
{
    listview_item *it = mzalloc(sizeof(listview_item));
    it->id = id;
    it->data = data;
    it->flags = 0;
    it->parent_rect = (fb_item_pos*)view;

    if(!view->items)
        keyaction_add(view->x, view->y, listview_keyaction_call, view);

    list_add(it, &view->items);
    return it;
}

void listview_clear(listview *view)
{
    list_clear(&view->items, view->item_destroy);
    view->selected = NULL;

    keyaction_remove(listview_keyaction_call, view);
}

void listview_update_ui(listview *view)
{
    int y = 0;
    int i, it_h, visible;
    listview_item *it;

    fb_batch_start();

    for(i = 0; view->items && view->items[i]; ++i)
    {
        it = view->items[i];
        it_h = (*view->item_height)(it);

        visible = (int)(view->pos <= y+it_h && y-view->pos <= view->h);

        if(visible || (it->flags & IT_VISIBLE))
            (*view->item_draw)(view->x, view->y+y-view->pos, view->w - PADDING, it);

        if(visible)
            it->flags |= IT_VISIBLE;
        else
            it->flags &= ~(IT_VISIBLE);

        y += it_h;
    }

    view->fullH = y;

    listview_enable_scroll(view, (int)(y > view->h));
    if(y > view->h)
        listview_update_scroll_mark(view);

    fb_batch_end();
    fb_request_draw();
}

void listview_enable_scroll(listview *view, int enable)
{
    if((view->scroll_mark != NULL) == (enable))
        return;

    if(enable)
    {
        int x = view->x + view->w - PADDING/2 - MARK_W/2;
        view->scroll_mark = fb_add_rect(x, view->y, MARK_W, MARK_H, GRAY);
        view->scroll_mark->parent = (fb_item_pos*)view;

        view->overscroll_marks[0] = fb_add_rect(view->x, view->y, 0, OVERSCROLL_MARK_H, CLR_SECONDARY);
        view->overscroll_marks[0]->parent = (fb_item_pos*)view;
        view->overscroll_marks[1] = fb_add_rect(view->x, view->y+view->h-OVERSCROLL_MARK_H,
                                                0, OVERSCROLL_MARK_H, CLR_SECONDARY);
        view->overscroll_marks[1]->parent = (fb_item_pos*)view;
        workers_add(listview_bounceback, view);
    }
    else
    {
        workers_remove(listview_bounceback, view);

        fb_rm_rect(view->scroll_mark);
        fb_rm_rect(view->overscroll_marks[0]);
        fb_rm_rect(view->overscroll_marks[1]);

        view->scroll_mark = NULL;
        view->overscroll_marks[0] = NULL;
        view->overscroll_marks[1] = NULL;
    }
}

void listview_update_scroll_mark(listview *view)
{
    if(!view->scroll_mark)
        return;

    int pos = view->pos;
    if(pos < 0)
        pos = 0;
    else if(pos > view->fullH - view->h)
        pos = view->fullH - view->h;

    int pct = (pos*100)/(view->fullH-view->h);
    int y = view->y + ((view->h - MARK_H)*pct)/100;
    view->scroll_mark->y = y;
}

void listview_update_overscroll_mark(listview *v, int side, float overscroll)
{
    int w = v->w * (overscroll / OVERSCROLL_H);
    v->overscroll_marks[side]->w = w;
    v->overscroll_marks[side]->x = v->x + (v->w >> 1) - (w >> 1);
}

int listview_touch_handler(touch_event *ev, void *data)
{
    listview *view = (listview*)data;
    if(view->touch.id == -1 && (ev->changed & TCHNG_ADDED))
    {
        if (ev->x < view->x || ev->y < view->y ||
            ev->x > view->x+view->w || ev->y > view->y+view->h)
        {
            if(listview_select_item(view, NULL))
                listview_update_ui(view);
            return -1;
        }

        if(ev->consumed)
            return -1;

        view->touch.id = ev->id;
        view->touch.last_y = ev->y;
        view->touch.start_y = ev->y;
        view->touch.us_diff = 0;
        view->touch.hover = listview_item_at(view, ev->y);
        view->touch.fast_scroll = (ev->x > view->x + view->w - PADDING*2 && ev->x <= view->x + view->w);

        if(view->touch.hover)
        {
            view->touch.hover->flags |= IT_HOVER;
            view->touch.hover->touchX = ev->x;
            view->touch.hover->touchY = ev->y;
            listview_update_ui(view);
        }
        listview_keyaction_call(view, KEYACT_CLEAR);
        listview_update_ui(view);
        return 0;
    }

    if(view->touch.id != ev->id)
        return -1;

    if(ev->changed & TCHNG_POS)
    {
        view->touch.us_diff += ev->us_diff;
        if(view->touch.us_diff >= 10000)
        {
            if(view->touch.hover && abs(ev->y - view->touch.start_y) > SCROLL_DIST)
            {
                view->touch.hover->flags &= ~(IT_HOVER);
                view->touch.hover = NULL;
            }

            if(!view->touch.hover)
            {
                if(view->touch.fast_scroll)
                    listview_scroll_to(view, ((ev->y-view->y)*100)/(view->h));
                else
                    listview_scroll_by(view, view->touch.last_y - ev->y);
            }

            view->touch.last_y = ev->y;
            view->touch.us_diff = 0;
        }
    }

    if(ev->changed & TCHNG_REMOVED)
    {
        if(view->touch.hover)
        {
            if(view->selected == view->touch.hover)
            {
                if(view->item_confirmed)
                    view->item_confirmed(view->selected);
            }
            else
                listview_select_item(view, view->touch.hover);
            view->touch.hover->flags &= ~(IT_HOVER);
        }
        view->touch.id = -1;
        listview_update_ui(view);
    }

    return 0;
}

int listview_select_item(listview *view, listview_item *it)
{
    if(view->selected == it)
        return 0;

    if(view->item_selected)
        (*view->item_selected)(view->selected, it);

    if(view->selected)
        view->selected->flags &= ~(IT_SELECTED);

    if(it)
        it->flags |= IT_SELECTED;

    view->selected = it;
    return 1;
}

void listview_scroll_by(listview *view, int y)
{
    if(!y || !view->scroll_mark)
        return;

    view->pos += y;

    if(view->pos < -OVERSCROLL_H)
        view->pos = -OVERSCROLL_H;
    else if(view->pos > (view->fullH - view->h) + OVERSCROLL_H)
        view->pos = (view->fullH - view->h) + OVERSCROLL_H;

    listview_select_item(view, NULL);
    listview_update_ui(view);
}

void listview_scroll_to(listview *view, int pct)
{
    if(!view->scroll_mark)
        return;

    view->pos = ((view->fullH - view->h)*pct)/100;

    if(view->pos < 0)
        view->pos = 0;
    else if(view->pos > (view->fullH - view->h))
        view->pos = (view->fullH - view->h);

    listview_select_item(view, NULL);
    listview_update_ui(view);
}

int listview_ensure_visible(listview *view, listview_item *it)
{
    if(!view->scroll_mark)
        return 0;

    int i;
    int y = 0;
    for(i = 0; view->items[i]; ++i)
    {
        if(it == view->items[i])
            break;
        y += view->item_height(view->items[i]);
    }

    int last_h = view->items[i] ? view->item_height(view->items[i]) : 0;

    if((y + last_h) - view->pos > view->h)
        view->pos = (y + last_h) - view->h;
    else if(y - view->pos < 0)
        view->pos = y;
    else
        return 0;
    return 1;
}

int listview_ensure_selected_visible(listview *view)
{
    if(view->selected)
        return listview_ensure_visible(view, view->selected);
    else
        return 0;
}

listview_item *listview_item_at(listview *view, int y_pos)
{
    int y = -view->pos + view->y;
    int i, it_h;
    listview_item *it;

    for(i = 0; view->items && view->items[i]; ++i)
    {
        it = view->items[i];
        it_h = (*view->item_height)(it);

        if(y < y_pos && y+it_h > y_pos)
            return it;

        y += it_h;
    }
    return NULL;
}

int listview_keyaction_call(void *data, int act)
{
    listview *v = data;
    switch(act)
    {
        case KEYACT_DOWN:
        {
            ++v->keyact_item_selected;
            if(v->keyact_item_selected >= list_item_count(v->items))
                v->keyact_item_selected = -1;
            listview_update_keyact_frame(v);
            return (v->keyact_item_selected == -1) ? 1 :0;
        }
        case KEYACT_UP:
        {
            if(v->keyact_item_selected == -1)
                v->keyact_item_selected = list_item_count(v->items)-1;
            else
                --v->keyact_item_selected;
            listview_update_keyact_frame(v);
            return (v->keyact_item_selected == -1) ? 1 :0;
        }
        case KEYACT_CLEAR:
        {
            if(v->keyact_item_selected != -1)
            {
                v->keyact_item_selected = -1;
                listview_select_item(v, NULL);
                listview_update_ui(v);
                fb_request_draw();
            }
            return 0;
        }
        case KEYACT_CONFIRM:
        {
            if(v->item_confirmed)
                v->item_confirmed(v->items[v->keyact_item_selected]);
            return 0;
        }
        default:
            return 0;
    }
}

void listview_update_keyact_frame(listview *view)
{
    if(view->keyact_item_selected == -1)
    {
        if(view->selected)
        {
            listview_select_item(view, NULL);
            listview_update_ui(view);
        }
        return;
    }

    listview_item *it = view->items[view->keyact_item_selected];
    listview_ensure_visible(view, it);

    int i;
    int y = view->y;
    for(i = 0; i < view->keyact_item_selected && view->items[i]; ++i)
        y += view->item_height(view->items[i]);

    int h = view->item_height(view->items[i]);
    y -= view->pos;

    listview_select_item(view, it);
    listview_update_ui(view);
}

#define ROM_ITEM_H (110*DPI_MUL)
#define ROM_ITEM_SHADOW (7*DPI_MUL)
#define ROM_ITEM_SEL_W (8*DPI_MUL)
#define ROM_ICON_H (70*DPI_MUL)
#define ROM_TEXT_PADDING (130*DPI_MUL)
#define ROM_ICON_PADDING (ROM_TEXT_PADDING/2 - ROM_ICON_H/2)

typedef struct
{
    char *text;
    char *partition;
    char *icon_path;
    fb_text *text_it;
    fb_text *part_it;
    fb_rect *sel_rect;
    fb_rect *sel_rect_sh;
    fb_img *icon;
    int deselect_anim_started;
    int rom_name_size;
    int last_y;
} rom_item_data;

void *rom_item_create(const char *text, const char *partition, const char *icon)
{
    rom_item_data *data = mzalloc(sizeof(rom_item_data));

    data->rom_name_size = SIZE_BIG;

    data->text = strdup(text);
    if(partition)
        data->partition = strdup(partition);
    if(icon)
        data->icon_path = strdup(icon);
    return data;
}

static void rom_item_deselect_finished(void *data)
{
    rom_item_data *d = data;

    fb_rm_rect(d->sel_rect);
    fb_rm_rect(d->sel_rect_sh);
    d->sel_rect = NULL;
    d->sel_rect_sh = NULL;
}

static void rom_item_alpha(void *data, float interpolated)
{
    rom_item_data *d = data;
    if(!d->sel_rect || !d->sel_rect_sh)
        return;

    uint32_t alpha = 0xFF*interpolated;
    if(d->deselect_anim_started)
        alpha = 0xFF - alpha;
    alpha <<= 24;
    d->sel_rect->color = (d->sel_rect->color & ~(0xFF << 24)) | alpha;
    d->sel_rect_sh->color = (d->sel_rect_sh->color & ~(0xFF << 24)) | alpha;
}

static void rom_item_sel_step(void *data)
{
    rom_item_data *d = data;
    if(!d->sel_rect || !d->sel_rect_sh)
        return;

    d->sel_rect_sh->x = d->sel_rect->x + ROM_ITEM_SHADOW;
    d->sel_rect_sh->y = d->sel_rect->y + ROM_ITEM_SHADOW;
    d->sel_rect_sh->w = d->sel_rect->w;
    d->sel_rect_sh->h = d->sel_rect->h;
}

void rom_item_draw(int x, int y, int w, listview_item *it)
{
    rom_item_data *d = (rom_item_data*)it->data;
    const int item_h = rom_item_height(it);
    if(!d->text_it)
    {
        d->text_it = fb_add_text(x+ROM_TEXT_PADDING, 0, BLACK, d->rom_name_size, d->text);
        d->text_it->parent = it->parent_rect;

        while(d->text_it->w + ROM_TEXT_PADDING >= w && d->rom_name_size > 3)
            fb_text_set_size(d->text_it, --d->rom_name_size);

        if(d->icon_path)
        {
            d->icon = fb_add_png_img(x+ROM_ICON_PADDING, 0, ROM_ICON_H, ROM_ICON_H, d->icon_path);
            d->icon->parent = it->parent_rect;
        }

        if(d->partition)
        {
            d->part_it = fb_add_text(x+ROM_TEXT_PADDING, 0, GRAY, SIZE_SMALL, d->partition);
            d->part_it->parent = it->parent_rect;
        }
    }

    center_text(d->text_it, -1, y, -1, item_h);

    if(d->icon)
        d->icon->y = y + (item_h/2 - ROM_ICON_H/2);

    if(d->part_it)
        d->part_it->y = d->text_it->y + d->text_it->h + 2*DPI_MUL;

    if(it->flags & IT_SELECTED)
    {
        if(!d->sel_rect)
        {
            int baseX = it->touchX;
            int baseY = it->touchY;
            if(!baseX && !baseY)
            {
                baseX = x + w/2;
                baseY = y + item_h/2;
            }

            d->sel_rect_sh = fb_add_rect(baseX+ROM_ITEM_SHADOW, baseY+ROM_ITEM_SHADOW, 1, 1, GRAYISH & ~(0xFF << 24));
            d->sel_rect_sh->parent = it->parent_rect;
            d->sel_rect = fb_add_rect(baseX, baseY, 1, 1, WHITE & ~(0xFF << 24));
            d->sel_rect->parent = it->parent_rect;

            call_anim *canim = call_anim_create(d, rom_item_alpha, 300, INTERPOLATOR_ACCEL_DECEL);
            call_anim_add(canim);

            item_anim *anim = item_anim_create(d->sel_rect, 300, INTERPOLATOR_ACCEL_DECEL);
            anim->start_offset = 0;
            anim->targetX = x;
            anim->targetY = y;
            anim->targetW = w;
            anim->targetH = item_h;
            anim->on_step_data = d;
            anim->on_step_call = rom_item_sel_step;
            item_anim_add(anim);

            d->deselect_anim_started = 0;
        }
        else
        {
            d->sel_rect_sh->y += y - d->last_y;
            d->sel_rect->y += y - d->last_y;
        }
    }
    else if(d->sel_rect)
    {
        if(!d->deselect_anim_started)
        {
            d->deselect_anim_started = 1;

            item_anim *anim = item_anim_create(d->sel_rect, 150, INTERPOLATOR_ACCELERATE);
            if(it->touchX || it->touchY)
            {
                anim->targetX = it->touchX;
                anim->targetY = it->touchY;
            }
            else
            {
                anim->targetX = x + w/2;
                anim->targetY = y + item_h/2;
            }
            anim->targetW = 0;
            anim->targetH = 0;
            anim->on_step_data = d;
            anim->on_step_call = rom_item_sel_step;
            item_anim_add_after(anim);

            call_anim *canim = call_anim_create(d, rom_item_alpha, 150, INTERPOLATOR_ACCELERATE);
            canim->start_offset = anim->start_offset;
            canim->cancel_check_data = d->sel_rect;
            canim->cancel_check = anim_item_cancel_check;
            canim->on_finished_data = d;
            canim->on_finished_call = rom_item_deselect_finished;
            call_anim_add(canim);
        }
        else
        {
            d->sel_rect_sh->y += y - d->last_y;
            d->sel_rect->y += y - d->last_y;
        }
    }

    d->last_y = y;
}

void rom_item_hide(void *data)
{
    rom_item_data *d = (rom_item_data*)data;
    if(!d->text_it)
        return;

    fb_rm_text(d->text_it);
    fb_rm_text(d->part_it);
    fb_rm_rect(d->sel_rect);
    fb_rm_rect(d->sel_rect_sh);
    fb_rm_img(d->icon);

    d->text_it = NULL;
    d->part_it = NULL;
    d->sel_rect = NULL;
    d->sel_rect_sh = NULL;
    d->icon = NULL;
}

int rom_item_height(listview_item *it)
{
    return ROM_ITEM_H;
}

void rom_item_destroy(listview_item *it)
{
    rom_item_hide(it->data);
    rom_item_data *d = (rom_item_data*)it->data;
    free(d->text);
    free(d->partition);
    free(d->icon_path);
    free(it->data);
    free(it);
}
