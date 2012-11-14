#include <stdlib.h>

#include "button.h"
#include "input.h"
#include "util.h"

void button_init_ui(button *b, const char *text, int size)
{
    b->touch_id = -1;

    b->rect = fb_add_rect(b->x, b->y, b->w, b->h, LBLUE);

    int text_x = center_x(b->x, b->w, size, text);
    int text_y = center_y(b->y, b->h, size);
    b->text = fb_add_text(text_x, text_y, WHITE, size, text);

    add_touch_handler(&button_touch_handler, b);
}

void button_destroy(button *b)
{
    rm_touch_handler(&button_touch_handler, b);

    fb_rm_rect(b->rect);
    fb_rm_text(b->text);

    free(b);
}

void button_move(button *b, int x, int y)
{
    b->x = x;
    b->y = y;

    b->rect->head.x = x;
    b->rect->head.y = y;

    b->text->head.x = center_x(x, b->w, b->text->size, b->text->text);
    b->text->head.y = center_y(y, b->h, b->text->size);
}

void button_set_hover(button *b, int hover)
{
    if(!( ((b->flags & BTN_HOVER) != 0) ^ (hover == 1) ))
        return;

    if(hover)
    {
        b->flags |= BTN_HOVER;
        b->rect->color = LBLUE2;
    }
    else
    {
        b->flags &= ~(BTN_HOVER);
        b->rect->color = LBLUE;
    }

    fb_draw();
}

void button_enable(button *b, int enable)
{
    if(!( ((b->flags & BTN_DISABLED) == 0) ^ (enable == 1) ))
        return;

    if(enable)
    {
        b->flags &= ~(BTN_DISABLED);
        b->rect->color = LBLUE;
    }
    else
    {
        b->flags |= BTN_DISABLED;
        b->flags &= ~(BTN_HOVER);
        b->rect->color = GRAY;
    }
    fb_draw();
}

int button_touch_handler(touch_event *ev, void *data)
{
    button *b = (button*)data;

    if(b->flags & BTN_DISABLED)
        return -1;

    if(b->touch_id == -1)
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
            (*b->clicked)();
        button_set_hover(b, 0);
        b->touch_id = -1;
    }
    else if(ev->changed & TCHNG_POS)
        button_set_hover(b, in_rect(ev->x, ev->y, b->x, b->y, b->w, b->h));

    return 0;
}
