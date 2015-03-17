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
#include "colors.h"
#include "log.h"
#include "containers.h"

void button_init_ui(button *b, const char *text, int size)
{
    b->touch_id = -1;

    if(text != NULL)
    {
        b->c[CLR_NORMAL][0] = C_HIGHLIGHT_BG;
        b->c[CLR_NORMAL][1] = C_HIGHLIGHT_TEXT;
        b->c[CLR_HOVER][0] = C_HIGHLIGHT_HOVER;
        b->c[CLR_HOVER][1] = C_HIGHLIGHT_TEXT;
        b->c[CLR_DIS][0] = GRAY;
        b->c[CLR_DIS][1] = WHITE;
        b->c[CLR_CHECK][0] = C_HIGHLIGHT_BG;
        b->c[CLR_CHECK][1] = C_HIGHLIGHT_TEXT;

        b->rect = fb_add_rect_lvl(b->level_off + LEVEL_RECT, b->x, b->y, b->w, b->h, b->c[CLR_NORMAL][0]);

        char *uppertext = strtoupper(text);
        fb_text_proto *p = fb_text_create(0, 0, b->c[CLR_NORMAL][1], size, uppertext);
        p->level = b->level_off + LEVEL_TEXT;
        p->style = STYLE_MEDIUM;
        b->text = fb_text_finalize(p);
        center_text(b->text, b->x, b->y, b->w, b->h);
        free(uppertext);
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
    keyaction_remove(&button_keyaction_call, b);

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
        b->rect->x = x;
        b->rect->y = y;

        center_text(b->text, b->x, b->y, b->w, b->h);
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
        fb_request_draw();
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

    if(b->touch_id == -1 && (ev->changed & TCHNG_ADDED) && !ev->consumed)
    {
        if(!in_rect(ev->x, ev->y, b->x, b->y, b->w, b->h))
            return -1;

        b->touch_id = ev->id;
    }

    if(b->touch_id != ev->id)
        return -1;

    if(ev->changed & TCHNG_POS)
        button_set_hover(b, in_rect(ev->x, ev->y, b->x, b->y, b->w, b->h));

    if(ev->changed & TCHNG_REMOVED)
    {
        if((b->flags & BTN_HOVER) && b->clicked)
            (*b->clicked)(b->clicked_data);
        button_set_hover(b, 0);
        b->touch_id = -1;
    }

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
        fb_text_set_color(b->text, b->c[state][1]);
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
    fb_request_draw();
}

int button_keyaction_call(void *data, int act)
{
    button *b = data;
    switch(act)
    {
        case KEYACT_UP:
        case KEYACT_DOWN:
        case KEYACT_CLEAR:
        {
            if(act != KEYACT_CLEAR && b->keyact_frame == NULL)
            {
                fb_add_rect_notfilled(b->level_off + LEVEL_RECT, b->x, b->y, b->w, b->h, C_KEYACT_FRAME, KEYACT_FRAME_W, &b->keyact_frame);
                fb_request_draw();
                return 0;
            }
            else
            {
                list_clear(&b->keyact_frame, &fb_remove_item);
                fb_request_draw();
                return (act == KEYACT_CLEAR) ? 0 : 1;
            }
        }
        case KEYACT_CONFIRM:
        {
            if(b->clicked && !(b->flags & BTN_DISABLED))
                (*b->clicked)(b->clicked_data);
            return 0;
        }
        default:
            return 0;
    }
}
