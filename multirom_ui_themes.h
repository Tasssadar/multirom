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

#ifndef MULTIROM_UI_P_H
#define MULTIROM_UI_P_H

#include "framebuffer.h"
#include "button.h"
#include "progressdots.h"
#include "listview.h"

typedef struct 
{
    listview *list;
    button **buttons;
    void **ui_elements;
    fb_text *rom_name;
    fb_text *title_text;
    fb_text *usb_text;
    button *boot_btn;
    progdots *usb_prog;
} tab_data_roms;

typedef struct 
{
    button **buttons;
    void **ui_elements;
} tab_data_misc;

typedef struct
{
    fb_text *tab_texts[TAB_COUNT];
    fb_rect *selected_tab_rect;
    button *tab_btns[TAB_COUNT];
    int selected_tab;
    void *tab_data;
} multirom_theme_data;

struct multirom_theme
{
    uint16_t width;
    uint16_t height;
    multirom_theme_data *data;

    void (*destroy)(struct multirom_theme *t);
    void (*init_header)(struct multirom_theme *t);
    void (*header_select)(struct multirom_theme *t, int tab);
    void (*tab_rom_init)(struct multirom_theme *t, tab_data_roms *d, int tab_type);
    void (*tab_misc_init)(struct multirom_theme *t, tab_data_misc *d, int color_scheme);
    int (*get_tab_width)(struct multirom_theme *t);
    int (*get_tab_height)(struct multirom_theme *t);
    void (*center_rom_name)(tab_data_roms *d, const char *rom_name);
};
typedef struct multirom_theme multirom_theme;

typedef struct
{
    multirom_theme **themes;
    multirom_theme_data *data;
} multirom_themes_info;

multirom_themes_info *multirom_ui_init_themes(void);
void multirom_ui_free_themes(multirom_themes_info *info);
multirom_theme *multirom_ui_select_theme(multirom_themes_info *i, int w, int h);

#endif
