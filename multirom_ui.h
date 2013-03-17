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

enum
{
    UI_EXIT_BOOT_ROM          = 1,
    UI_EXIT_REBOOT            = 2,
    UI_EXIT_REBOOT_RECOVERY   = 3,
    UI_EXIT_REBOOT_BOOTLOADER = 4,
    UI_EXIT_SHUTDOWN          = 5
};

enum
{
    CLRS_BLUE    = 0,
    CLRS_PURPLE  = 1,
    CLRS_GREEN   = 2,
    CLRS_ORANGE  = 3,
    CLRS_RED     = 4,
    CLRS_BROWN   = 5,

    CLRS_MAX
};

extern int CLR_PRIMARY;
extern int CLR_SECONDARY;

int multirom_ui(struct multirom_status *s, struct multirom_rom **to_boot);
void multirom_ui_init_header(void);
void multirom_ui_header_select(int tab);
void multirom_ui_destroy_tab(int tab);
int multirom_ui_touch_handler(touch_event *ev, void*);
void multirom_ui_switch(int tab);
void multirom_ui_fill_rom_list(listview *view, int mask);
void multirom_ui_auto_boot(void);
void multirom_ui_refresh_usb_handler(void);
void multirom_ui_start_pong(int action);
void multirom_ui_setup_colors(int clr, int *primary, int *secondary);

void *multirom_ui_tab_rom_init(int tab_type);
void multirom_ui_tab_rom_destroy(void *data);
void multirom_ui_tab_rom_selected(listview_item *prev, listview_item *now);
void multirom_ui_tab_rom_boot_btn(int action);
void multirom_ui_tab_rom_refresh_usb(int action);
void multirom_ui_tab_rom_update_usb(void *data);
void multirom_ui_tab_rom_set_empty(void *data, int empty);

void *multirom_ui_tab_misc_init(void);
void multirom_ui_tab_misc_destroy(void *data);
void multirom_ui_tab_misc_copy_log(int action);
void multirom_ui_tab_misc_change_clr(int clr);

void multirom_ui_reboot_btn(int action);

#endif
