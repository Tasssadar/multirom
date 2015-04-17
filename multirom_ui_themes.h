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

#include "lib/framebuffer.h"
#include "lib/button.h"
#include "lib/progressdots.h"
#include "lib/listview.h"
#include "lib/tabview.h"
#include "multirom_ui.h"

// universal themes has these as width and height,
// instead of real resolution
#define TH_PORTRAIT  (-1)
#define TH_LANDSCAPE (-2)

typedef struct
{
    listview *list;
    button **buttons;
    void **ui_elements;
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
    fb_rect *selected_rect[TAB_COUNT-1];
    button *tab_btns[TAB_COUNT];
    tabview *tabs;
    int selected_tab;
    void *tab_data[TAB_COUNT];
} multirom_theme_data;

struct multirom_theme
{
    int16_t width;
    int16_t height;

    void (*destroy)(multirom_theme_data *t);
    void (*init_header)(multirom_theme_data *t);
    void (*header_set_tab_selector_pos)(multirom_theme_data *t, float pos);
    void (*tab_rom_init)(multirom_theme_data *t, tab_data_roms *d, int tab_type);
    void (*tab_misc_init)(multirom_theme_data *t, tab_data_misc *d, int color_scheme);
    int (*get_tab_width)(multirom_theme_data *t);
    int (*get_tab_height)(multirom_theme_data *t);
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
