#ifndef LISTVIEW_H
#define LISTVIEW_H

#include "input.h"
#include "framebuffer.h"

enum
{
    IT_VISIBLE  = 0x01,
    IT_HOVER    = 0x02,
    IT_SELECTED = 0x04,
};

typedef struct
{
    int id;
    void *data;
    int flags;
} listview_item;

typedef struct 
{
    int id;
    int start_y;
    int last_y;
    int64_t us_diff;
    listview_item *hover;
    int fast_scroll;
} listview_touch_data;

typedef struct
{
    int x, y;
    int w, h;

    int pos; // scroll pos
    int fullH; // height of all items

    listview_item **items;
    listview_item *selected;

    void (*item_draw)(int, int, int, listview_item *); // x, y, w, item
    void (*item_hide)(void*); // data
    int (*item_height)(void*); // data
    void (*item_destroy)(listview_item *);
    void (*item_selected)(listview_item *, listview_item *); // prev, now

    fb_item_header **ui_items;
    fb_rect *scroll_mark;

    listview_touch_data touch;
} listview;

int listview_touch_handler(touch_event *ev, void *data);

void listview_init_ui(listview *view);
void listview_destroy(listview *view);
listview_item *listview_add_item(listview *view, int id, void *data);
void listview_clear(listview *view);
void listview_update_ui(listview *view);
void listview_enable_scroll(listview *view, int enable);
void listview_update_scroll_mark(listview *view);
void listview_scroll_by(listview *view, int y);
void listview_scroll_to(listview *view, int pct);
listview_item *listview_item_at(listview *view, int y_pos);
inline void listview_select_item(listview *view, listview_item *it);

void *rom_item_create(const char *text, const char *partition);
void rom_item_draw(int x, int y, int w, listview_item *it);
void rom_item_hide(void *data);
int rom_item_height(void *data);
void rom_item_destroy(listview_item *it);

#endif