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

#include "button.h"
#include "input.h"
#include "util.h"
#include "multirom_ui.h"

void button_init_ui(button *b, const char *text, int size)
{
    b->touch_id = -1;

    if(text != NULL)
    {
        b->c[CLR_NORMAL][0] = CLR_PRIMARY;
        b->c[CLR_NORMAL][1] = WHITE;
        b->c[CLR_HOVER][0] = CLR_SECONDARY;
        b->c[CLR_HOVER][1] = WHITE;
        b->c[CLR_DIS][0] = GRAY;
        b->c[CLR_DIS][1] = WHITE;
        b->c[CLR_CHECK][0] = CLR_SECONDARY;
        b->c[CLR_CHECK][1] = WHITE;

        b->rect = fb_add_rect(b->x, b->y, b->w, b->h, b->c[CLR_NORMAL][0]);

        int text_x = center_x(b->x, b->w, size, text);
        int text_y = center_y(b->y, b->h, size);
        b->text = fb_add_text(text_x, text_y, b->c[CLR_NORMAL][1], size, text);
    }
    else
    {
        b->text = NULL;
        b->rect = NULL;
    }

    add_touch_handler(&button_touch_handler, b);
}

void button_destroy(button *b)
{
    rm_touch_handler(&button_touch_handler, b);

    if(b->text)
    {
        fb_rm_rect(b->rect);
        fb_rm_text(b->text);
    }

    free(b);
}

void button_move(button *b, int x, int y)
{
    b->x = x;
    b->y = y;

    if(b->text)
    {
        b->rect->head.x = x;
        b->rect->head.y = y;

        b->text->head.x = center_x(x, b->w, b->text->size, b->text->text);
        b->text->head.y = center_y(y, b->h, b->text->size);
    }
}

void button_set_hover(button *b, int hover)
{
    if((hover == 1) == ((b->flags & BTN_HOVER) != 0))
        return;

    if(hover)
        b->flags |= BTN_HOVER;
    else
        b->flags &= ~(BTN_HOVER);

    if(b->text)
    {
        button_update_colors(b);
        fb_draw();
    }
}

void button_enable(button *b, int enable)
{
    if(enable)
        b->flags &= ~(BTN_DISABLED);
    else
    {
        b->flags |= BTN_DISABLED;
        b->flags &= ~(BTN_HOVER);
    }

    if(b->text)
    {
        button_update_colors(b);
        fb_request_draw();
    }
}

int button_touch_handler(touch_event *ev, void *data)
{
    button *b = (button*)data;

    if(b->flags & BTN_DISABLED)
        return -1;

    if(b->touch_id == -1 && (ev->changed & TCHNG_ADDED))
    {
        if(!in_rect(ev->x, ev->y, b->x, b->y, b->w, b->h))
            return -1;

        b->touch_id = ev->id;
    }

    if(b->touch_id != ev->id)
        return -1;

    if(ev->changed & TCHNG_REMOVED)
    {
        if((b->flags & BTN_HOVER) && b->clicked)
            (*b->clicked)(b->action);
        button_set_hover(b, 0);
        b->touch_id = -1;
    }
    else if(ev->changed & TCHNG_POS)
        button_set_hover(b, in_rect(ev->x, ev->y, b->x, b->y, b->w, b->h));

    return 0;
}

void button_set_color(button *b, int idx, int text, uint32_t color)
{
    b->c[idx][text] = color;
    button_update_colors(b);
}

void button_update_colors(button *b)
{
    int state = CLR_NORMAL;
    if(b->flags & BTN_DISABLED)
        state = CLR_DIS;
    else if(b->flags & BTN_HOVER)
        state = CLR_HOVER;
    else if(b->flags & BTN_CHECKED)
        state = CLR_CHECK;

    if(b->text)
    {
        b->rect->color = b->c[state][0];
        b->text->color = b->c[state][1];
    }
}

void button_set_checked(button *b, int checked)
{
    if((checked == 1) == ((b->flags & BTN_CHECKED) != 0))
        return;

    if(checked)
        b->flags |= BTN_CHECKED;
    else
        b->flags &= ~(BTN_CHECKED);

    button_update_colors(b);
    fb_draw();
}