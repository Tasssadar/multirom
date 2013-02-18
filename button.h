#ifndef BUTTON_H
#define BUTTON_H

#include "framebuffer.h"
#include "input.h"

enum 
{
    BTN_HOVER         = 0x01,
    BTN_DISABLED      = 0x02,
    BTN_CHECKED       = 0x04,
};

enum
{
    CLR_NORMAL        = 0,
    CLR_HOVER,
    CLR_DIS,
    CLR_CHECK,

    CLR_MAX
};

typedef struct
{
    int x, y;
    int w, h;
    fb_text *text;
    fb_rect *rect;

    uint32_t c[CLR_MAX][2];
 
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
void button_set_checked(button *b, int checked);
void button_set_color(button *b, int idx, int text, uint32_t color);
void button_update_colors(button *b);
int button_touch_handler(touch_event *ev, void *data);

#endif