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
#include "multirom_ui.h"
#include "tabview.h"

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

struct multirom_color_theme
{
    uint32_t background;
    uint32_t highlight_bg;
    uint32_t highlight_hover;
    uint32_t highlight_text;
    uint32_t text;
    uint32_t text_secondary;
    uint32_t ncard_bg;
    uint32_t ncard_text;
    uint32_t ncard_text_secondary;
    uint32_t ncard_shadow;
    uint32_t rom_highlight;
    uint32_t rom_highlight_shadow;
    uint32_t keyaction_frame;
};

extern const struct multirom_color_theme *color_theme;
#define C_BACKGROUND (color_theme->background)
#define C_HIGHLIGHT_BG (color_theme->highlight_bg)
#define C_HIGHLIGHT_HOVER (color_theme->highlight_hover)
#define C_HIGHLIGHT_TEXT (color_theme->highlight_text)
#define C_TEXT (color_theme->text)
#define C_TEXT_SECONDARY (color_theme->text_secondary)
#define C_NCARD_BG (color_theme->ncard_bg)
#define C_NCARD_TEXT (color_theme->ncard_text)
#define C_NCARD_TEXT_SECONDARY (color_theme->ncard_text_secondary)
#define C_NCARD_SHADOW (color_theme->ncard_shadow)
#define C_ROM_HIGHLIGHT (color_theme->rom_highlight)
#define C_ROM_HIGHLIGHT_SHADOW (color_theme->rom_highlight_shadow)
#define C_KEYACT_FRAME (color_theme->keyaction_frame)

void multirom_ui_select_color(size_t color_theme_idx);
const struct multirom_color_theme *multirom_ui_get_color_theme(size_t color_theme_idx);
int multirom_ui_get_color_theme_count(void);

#endif
