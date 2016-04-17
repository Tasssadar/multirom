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

#ifndef MULTIROM_UI_H
#define MULTIROM_UI_H

#include "multirom.h"
#include "lib/input.h"
#include "lib/listview.h"

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

int multirom_ui(struct multirom_status *s, struct multirom_rom **to_boot);
void multirom_ui_init_header(void);
void multirom_ui_change_header_selector_pos(float pos);
void multirom_ui_destroy_tab(int tab);
int multirom_ui_destroy_msgbox(void);
void multirom_ui_switch(int tab);
void multirom_ui_switch_btn(void *data);
int multirom_ui_fill_rom_list(listview *view, int mask);
void multirom_ui_auto_boot(void);
void multirom_ui_refresh_usb_handler(void);
void multirom_ui_start_pong(void *data);
void multirom_ui_init_theme(int tab);
void multirom_ui_destroy_theme(void);

void *multirom_ui_tab_rom_init(int tab_type);
void multirom_ui_tab_rom_destroy(void *data);
void multirom_ui_tab_rom_boot(void);
void multirom_ui_tab_rom_confirmed(listview_item *it);
void multirom_ui_tab_rom_refresh_usb(int action);
void multirom_ui_tab_rom_update_usb(void);
void multirom_ui_tab_rom_set_empty(void *data, int empty);

void *multirom_ui_tab_misc_init(void);
void multirom_ui_tab_misc_destroy(void *data);
void multirom_ui_tab_misc_copy_log(void *data);
void multirom_ui_tab_misc_change_clr(void *data);

void multirom_ui_reboot_btn(void *data);

#endif
