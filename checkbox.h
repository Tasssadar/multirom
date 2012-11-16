#ifndef CHECKBOX_H
#define CHECKBOX_H

#include "framebuffer.h"
#include "input.h"

#define CHECKBOX_SIZE 30

enum
{
    BORDER_L = 0,
    BORDER_R = 1,
    BORDER_T = 2, 
    BORDER_B = 3, 

    BORDER_MAX
};

typedef struct
{
    int x, y;
    fb_rect *selected;
    fb_rect *borders[BORDER_MAX];
    int touch_id;
    void (*clicked)(int); // checked
    fb_rect *hover;
} checkbox;

checkbox *checkbox_create(int x, int y, void (*clicked)(int));
void checkbox_destroy(checkbox *c);

void checkbox_set_pos(checkbox *c, int x, int y);
void checkbox_select(checkbox *c, int select);

int checkbox_touch_handler(touch_event *ev, void *data);

#endif