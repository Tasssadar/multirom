#include <stdlib.h>

#include "listview.h"
#include "framebuffer.h"
#include "util.h"
#include "log.h"
#include "checkbox.h"

#define MARK_W 10
#define MARK_H 50
#define PADDING 20
#define LINE_W 2
#define SCROLL_DIST 20

void listview_init_ui(listview *view)
{
    int x = view->x + view->w - PADDING/2 - LINE_W/2;

    fb_rect *scroll_line = fb_add_rect(x, view->y, LINE_W, view->h, GRAYISH);
    list_add(scroll_line, &view->ui_items);

    view->touch.id = -1;
    view->touch.last_y = -1;

    add_touch_handler(&listview_touch_handler, view);
}

void listview_destroy(listview *view)
{
    rm_touch_handler(&listview_touch_handler, view);

    listview_clear(view);
    list_clear(&view->ui_items, &fb_remove_item);

    fb_rm_rect(view->scroll_mark);

    free(view);
}

listview_item *listview_add_item(listview *view, int id, void *data)
{
    listview_item *it = malloc(sizeof(listview_item));
    it->id = id;
    it->data = data;
    it->flags = 0;

    list_add(it, &view->items);
    return it;
}

void listview_clear(listview *view)
{
    list_clear(&view->items, view->item_destroy);
}

void listview_update_ui(listview *view)
{
    int y = 0;
    int i, it_h, visible;
    listview_item *it;

    for(i = 0; view->items && view->items[i]; ++i)
    {
        it = view->items[i];
        it_h = (*view->item_height)(it->data);

        visible = (int)(view->pos <= y && y+it_h-view->pos <= view->h);

        if(!visible && (it->flags & IT_VISIBLE))
            (*view->item_hide)(it->data);
        else if(visible)
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

    fb_draw();
}

void listview_enable_scroll(listview *view, int enable)
{
    if(!((view->scroll_mark != NULL) ^ (enable)))
        return;

    if(enable)
    {
        int x = view->x + view->w - PADDING/2 - MARK_W/2;
        view->scroll_mark = fb_add_rect(x, view->y, MARK_W, MARK_H, GRAYISH);
    }
    else
    {
        fb_rm_rect(view->scroll_mark);
        view->scroll_mark = NULL;
    }
}

void listview_update_scroll_mark(listview *view)
{
    if(!view->scroll_mark)
        return;

    int pct = (view->pos*100)/(view->fullH-view->h);
    int y = view->y + ((view->h - MARK_H)*pct)/100;
    view->scroll_mark->head.y = y;
}

int listview_touch_handler(touch_event *ev, void *data)
{
    listview *view = (listview*)data;
    if(view->touch.id == -1)
    {
        if (ev->x < view->x || ev->y < view->y ||
            ev->x > view->x+view->w || ev->y > view->y+view->h)
            return -1;

        view->touch.id = ev->id;
        view->touch.last_y = ev->y;
        view->touch.start_y = ev->y;
        view->touch.us_diff = 0;
        view->touch.hover = listview_item_at(view, ev->y);

        if(view->touch.hover)
        {
            view->touch.hover->flags |= IT_HOVER;
            listview_update_ui(view);
        }
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
                if(ev->x > view->x + view->w - PADDING*1.5f && ev->x < view->x + view->w)
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
            listview_select_item(view, view->touch.hover);
            view->touch.hover->flags &= ~(IT_HOVER);
        }
        view->touch.id = -1;
        listview_update_ui(view);
    }

    return 0;
}

void listview_select_item(listview *view, listview_item *it)
{
    if(view->item_selected)
        (*view->item_selected)(view->selected, it);

    if(view->selected)
        view->selected->flags &= ~(IT_SELECTED);
    it->flags |= IT_SELECTED;

    view->selected = it;
}

void listview_scroll_by(listview *view, int y)
{
    if(!view->scroll_mark)
        return;

    view->pos += y;

    if(view->pos < 0)
        view->pos = 0;
    else if(view->pos > (view->fullH - view->h))
        view->pos = (view->fullH - view->h);

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

    listview_update_ui(view);
}

listview_item *listview_item_at(listview *view, int y_pos)
{
    int y = -view->pos + view->y;
    int i, it_h;
    listview_item *it;

    for(i = 0; view->items && view->items[i]; ++i)
    {
        it = view->items[i];
        it_h = (*view->item_height)(it->data);

        if(y < y_pos && y+it_h > y_pos)
            return it;

        y += it_h;
    }
    return NULL;
}

#define ROM_ITEM_H 100

typedef struct
{
    char *text;
    fb_text *text_it;
    fb_rect *bottom_line;
    fb_rect *hover_rect;
    checkbox *box;
} rom_item_data;

void *rom_item_create(const char *text)
{
    rom_item_data *data = malloc(sizeof(rom_item_data));
    memset(data, 0, sizeof(rom_item_data));

    data->text = malloc(strlen(text)+1);
    strcpy(data->text, text);

    return data;
}

void rom_item_draw(int x, int y, int w, listview_item *it)
{
    rom_item_data *d = (rom_item_data*)it->data;
    if(!d->text_it)
    {
        d->text_it = fb_add_text(x+100, 0, WHITE, SIZE_BIG, d->text);
        d->bottom_line = fb_add_rect(x, 0, w, 1, 0xFF1B1B1B);
        d->box = checkbox_create(0, 0);
    }

    d->text_it->head.y = center_y(y, ROM_ITEM_H, SIZE_BIG);
    d->bottom_line->head.y = y+ROM_ITEM_H-2;

    if(it->flags & IT_HOVER)
    {
        if(!d->hover_rect)
            d->hover_rect = fb_add_rect(x, 0, w, rom_item_height(it->data), LBLUE2);
        d->hover_rect->head.y = y;
    }
    else if(d->hover_rect)
    {
        fb_rm_rect(d->hover_rect);
        d->hover_rect = NULL;
    }

    checkbox_set_pos(d->box, x+30, y + (ROM_ITEM_H/2 - 30/2));
    checkbox_select(d->box, (it->flags & IT_SELECTED));
}

void rom_item_hide(void *data)
{
    rom_item_data *d = (rom_item_data*)data;
    if(!d->text_it)
        return;

    fb_rm_text(d->text_it);
    fb_rm_rect(d->bottom_line);
    fb_rm_rect(d->hover_rect);

    checkbox_destroy(d->box);

    d->text_it = NULL;
    d->bottom_line = NULL;
    d->hover_rect = NULL;
    d->box = NULL;
}

int rom_item_height(void *data)
{
    return ROM_ITEM_H;
}

void rom_item_destroy(listview_item *it)
{
    rom_item_hide(it->data);
    rom_item_data *d = (rom_item_data*)it->data;
    free(d->text);
    free(it->data);
    free(it);
}
