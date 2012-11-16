#include <stdlib.h>

#include "checkbox.h"
#include "input.h"
#include "util.h"

#define BORDER_SIZE 2
#define BORDER_PADDING 2

#define SELECTED_SIZE (CHECKBOX_SIZE-(BORDER_SIZE+BORDER_PADDING)*2)
#define SELECTED_PADDING (BORDER_SIZE + BORDER_PADDING)
#define TOUCH 15

checkbox *checkbox_create(int x, int y, void (*clicked)(int))
{
    checkbox *c = malloc(sizeof(checkbox));
    memset(c, 0, sizeof(checkbox));

    c->touch_id = -1;
    c->clicked = clicked;

    c->borders[BORDER_L] = fb_add_rect(0, 0, BORDER_SIZE, CHECKBOX_SIZE, WHITE);
    c->borders[BORDER_R] = fb_add_rect(0, 0, BORDER_SIZE, CHECKBOX_SIZE, WHITE);
    c->borders[BORDER_T] = fb_add_rect(0, 0, CHECKBOX_SIZE, BORDER_SIZE, WHITE);
    c->borders[BORDER_B] = fb_add_rect(0, 0, CHECKBOX_SIZE, BORDER_SIZE, WHITE);

    checkbox_set_pos(c, x, y);

    if(c->clicked)
        add_touch_handler(&checkbox_touch_handler, c);

    return c;
}

void checkbox_destroy(checkbox *c)
{
    int i;
    for(i = 0; i < BORDER_MAX; ++i)
        fb_rm_rect(c->borders[i]);

    fb_rm_rect(c->selected);

    if(c->clicked)
        rm_touch_handler(&checkbox_touch_handler, c);

    free(c);
}

void checkbox_set_pos(checkbox *c, int x, int y)
{
    c->x = x;
    c->y = y;

    int pos[][2] =
    {
        // BORDER_L
        { x, y, },
        // BORDER_R
        { x + CHECKBOX_SIZE - BORDER_SIZE, y },
        // BORDER_T
        { x, y },
        // BORDER_B
        { x, y + CHECKBOX_SIZE - BORDER_SIZE }
    };

    int i;
    for(i = 0; i < BORDER_MAX; ++i)
    {
        c->borders[i]->head.x = pos[i][0];
        c->borders[i]->head.y = pos[i][1];
    }

    if(c->selected)
    {
        c->selected->head.x = x + SELECTED_PADDING;
        c->selected->head.y = y + SELECTED_PADDING;
    }
}

void checkbox_select(checkbox *c, int select)
{
    if(!((c->selected != NULL) ^ (select != 0)))
        return;

    if(select)
    {
        c->selected = fb_add_rect(c->x + SELECTED_PADDING, c->y + SELECTED_PADDING,
                                  SELECTED_SIZE, SELECTED_SIZE, LBLUE);
    }
    else
    {
        fb_rm_rect(c->selected);
        c->selected = NULL;
    }
}

int checkbox_touch_handler(touch_event *ev, void *data)
{
    checkbox *box = (checkbox*)data;

    if(box->touch_id == -1)
    {
        if(!in_rect(ev->x, ev->y, box->x-TOUCH, box->y-TOUCH, CHECKBOX_SIZE+TOUCH*2, CHECKBOX_SIZE+TOUCH*2))
            return -1;

        box->touch_id = ev->id;
        box->hover = fb_add_rect(box->x-TOUCH, box->y-TOUCH, CHECKBOX_SIZE+TOUCH*2, CHECKBOX_SIZE+TOUCH*2, LBLUE2);
        fb_draw();
    }

    if(box->touch_id != ev->id)
        return -1;

    if(ev->changed & TCHNG_REMOVED)
    {
        if(in_rect(ev->x, ev->y, box->x-TOUCH, box->y-TOUCH, CHECKBOX_SIZE+TOUCH*2, CHECKBOX_SIZE+TOUCH*2))
        {
            (*box->clicked)(box->selected == NULL);
            checkbox_select(box, (box->selected == NULL));
        }

        fb_rm_rect(box->hover);
        box->hover = NULL;
        box->touch_id = -1;

        fb_draw();
    }
    return 0;
}
