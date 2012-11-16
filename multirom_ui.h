#ifndef MULTIROM_UI_H
#define MULTIROM_UI_H

#include "multirom.h"
#include "input.h"
#include "listview.h"

enum
{
    TAB_INTERNAL = 0,
    TAB_USB,
    TAB_MISC,

    TAB_COUNT
};

struct multirom_rom *multirom_ui(struct multirom_status *s);
void multirom_ui_init_header(void);
void multirom_ui_header_select(int tab);
void multirom_ui_destroy_tab(int tab);
int multirom_ui_touch_handler(touch_event *ev, void*);
void multirom_ui_switch(int tab);
void multirom_ui_fill_rom_list(listview *view, int mask);

void *multirom_ui_tab_rom_init(int tab_type);
void multirom_ui_tab_rom_destroy(void *data);
void multirom_ui_tab_rom_selected(listview_item *prev, listview_item *now);
void multirom_ui_tab_rom_boot_btn(void);
void multirom_ui_tab_rom_refresh_usb(void);

void *multirom_ui_tab_misc_init(void);
void multirom_ui_tab_misc_destroy(void *data);

#endif
