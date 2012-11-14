#include <stdlib.h>

#include "checkbox.h"

#define BORDER_SIZE 2
#define BORDER_PADDING 2

#define SELECTED_SIZE (CHECKBOX_SIZE-(BORDER_SIZE+BORDER_PADDING)*2)
#define SELECTED_PADDING (BORDER_SIZE + BORDER_PADDING)

checkbox *checkbox_create(int x, int y)
{
    checkbox *c = malloc(sizeof(checkbox));
    memset(c, 0, sizeof(checkbox));

    c->borders[BORDER_L] = fb_add_rect(0, 0, BORDER_SIZE, CHECKBOX_SIZE, WHITE);
    c->borders[BORDER_R] = fb_add_rect(0, 0, BORDER_SIZE, CHECKBOX_SIZE, WHITE);
    c->borders[BORDER_T] = fb_add_rect(0, 0, CHECKBOX_SIZE, BORDER_SIZE, WHITE);
    c->borders[BORDER_B] = fb_add_rect(0, 0, CHECKBOX_SIZE, BORDER_SIZE, WHITE);

    checkbox_set_pos(c, x, y);

    return c;
}

void checkbox_destroy(checkbox *c)
{
    int i;
    for(i = 0; i < BORDER_MAX; ++i)
        fb_rm_rect(c->borders[i]);

    fb_rm_rect(c->selected);

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
