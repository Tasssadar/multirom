#ifndef BUTTON_H
#define BUTTON_H

#include "framebuffer.h"
#include "input.h"

enum 
{
    BTN_HOVER         = 0x01,
    BTN_DISABLED      = 0x02
};

typedef struct
{
    int x, y;
    int w, h;
    fb_text *text;
    fb_rect *rect;
 
    int flags;
    int touch_id;

    int action;
    void (*clicked)(int); // action
} button;

void button_init_ui(button *b, const char *text, int size);
void button_destroy(button *b);
void button_move(button *b, int x, int y);
void button_set_hover(button *b, int hover);
void button_enable(button *b, int enable);
int button_touch_handler(touch_event *ev, void *data);

#endif