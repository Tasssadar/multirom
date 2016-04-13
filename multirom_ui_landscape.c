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

#include "multirom_ui.h"
#include "multirom_ui_themes.h"
#include "multirom.h"
#include "version.h"
#include "lib/framebuffer.h"
#include "lib/util.h"
#include "lib/button.h"
#include "lib/input.h"
#include "lib/log.h"
#include "lib/animation.h"
#include "lib/notification_card.h"
#include "lib/tabview.h"
#include "lib/colors.h"

#define HEADER_HEIGHT (80*DPI_MUL)
#define TABS_HEIGHT (HEADER_HEIGHT - STATUS_HEIGHT)
#define MIRI_W (60*DPI_MUL)

#define LISTVIEW_MARGIN (20*DPI_MUL)

#define REFRESHBTN_W (400*DPI_MUL)
#define REFRESHBTN_H (60*DPI_MUL)

#define MISCBTN_W (530*DPI_MUL)
#define MISCBTN_H (100*DPI_MUL)

#define CLRBTN_W (50*DPI_MUL)
#define CLRBTN_B (10*DPI_MUL)
#define CLRBTN_TOTAL (CLRBTN_W+CLRBTN_B)
#define CLRBTN_Y (1150*DPI_MUL)
#define CLRBTN_MARGIN (8*DPI_MUL)

#define SELECTED_RECT_H (6*DPI_MUL)
#define BTN_SHADOW_OFF (5*DPI_MUL)

static void destroy(UNUSED multirom_theme_data *t)
{

}

static void header_set_tab_selector_pos(multirom_theme_data *t, float pos)
{
    const int TAB_BTN_WIDTH = t->tab_btns[0]->w;
    int dest_x = t->tab_btns[0]->x + TAB_BTN_WIDTH*pos;
    int dest_w = TAB_BTN_WIDTH;

    const int selected = imin(TAB_COUNT-1, imax(0, (int)(pos+0.5f)));
    int i, rect_i = 0;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        if(selected == i)
            continue;

        t->selected_rect[rect_i]->x = t->tab_texts[i]->x;
        t->selected_rect[rect_i]->y = t->tab_texts[i]->y;
        t->selected_rect[rect_i]->w = t->tab_texts[i]->w;
        t->selected_rect[rect_i]->h = t->tab_texts[i]->h;
        ++rect_i;
    }

    if(dest_x < t->tab_btns[0]->x)
    {
        dest_w -= t->tab_btns[0]->x - dest_x;
        dest_x = t->tab_btns[0]->x;
    }
    else if(dest_x > t->tab_btns[TAB_COUNT-1]->x)
    {
        dest_w = (t->tab_btns[TAB_COUNT-1]->x + t->tab_btns[TAB_COUNT-1]->w) - dest_x;
    }

    t->selected_tab_rect->x = dest_x;
    t->selected_tab_rect->w = dest_w;
}

static void init_header(multirom_theme_data *t)
{
    button **tab_btns = t->tab_btns;
    fb_text **tab_texts = t->tab_texts;
    const int TAB_BTN_WIDTH = fb_width*0.21;
    int i, x;
#ifdef MR_UNIFIED_TABS
    static const char *str[] = { "ROMS", "MISC" };
#else
    static const char *str[] = { "INTERNAL", "EXTERNAL", "MISC" };
#endif
    char buff[64];

    fb_add_rect_lvl(100, 0, 0, fb_width, HEADER_HEIGHT, C_HIGHLIGHT_BG);
    fb_add_rect(0, HEADER_HEIGHT, fb_width, (3*DPI_MUL), C_BTN_FAKE_SHADOW);
    ncard_set_top_offset(HEADER_HEIGHT);

    int maxW = 0;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        fb_text_proto *p = fb_text_create(0, 0, C_HIGHLIGHT_TEXT, SIZE_NORMAL, str[i]);
        p->level = 110;
        p->style = STYLE_MEDIUM;
        tab_texts[i] = fb_text_finalize(p);
        maxW = imax(maxW, tab_texts[i]->w);
    }

    maxW += (30*DPI_MUL);
    x = fb_width/2 - (maxW*TAB_COUNT)/2;

    snprintf(buff, sizeof(buff), ":/miri_%dx%d.png", (int)MIRI_W, (int)MIRI_W);
    fb_add_png_img_lvl(110, 10*DPI_MUL, HEADER_HEIGHT/2 - MIRI_W/2, MIRI_W, MIRI_W, buff);

    for(i = 0; i < TAB_COUNT; ++i)
    {
        center_text(tab_texts[i], x, 0, maxW, HEADER_HEIGHT);

        tab_btns[i] = mzalloc(sizeof(button));
        tab_btns[i]->x = x;
        tab_btns[i]->y = 0;
        tab_btns[i]->w = maxW;
        tab_btns[i]->h = HEADER_HEIGHT;
        tab_btns[i]->clicked_data = malloc(sizeof(int));
        *((int*)tab_btns[i]->clicked_data) = i;
        tab_btns[i]->clicked = &multirom_ui_switch_btn;
        tab_btns[i]->level_off = 100;
        button_init_ui(tab_btns[i], "", 0);

        keyaction_add(tab_btns[i], button_keyaction_call, tab_btns[i]);

        x += maxW;

         if(i < TAB_COUNT-1)
            t->selected_rect[i] = fb_add_rect_lvl(120, 0, 0, 0, 0, (0x4C << 24) | (C_HIGHLIGHT_BG & 0x00FFFFFF));
    }

    t->selected_tab_rect = fb_add_rect_lvl(110, tab_btns[0]->x, HEADER_HEIGHT-SELECTED_RECT_H + (3*DPI_MUL), maxW, SELECTED_RECT_H, C_HIGHLIGHT_TEXT);
    t->tabs = tabview_create(0, HEADER_HEIGHT, fb_width, fb_height-HEADER_HEIGHT);
    header_set_tab_selector_pos(t, 0.f);
}

static void tab_rom_init(UNUSED multirom_theme_data *t, tab_data_roms *d, UNUSED int tab_type)
{
    d->list->x = fb_width/2 - fb_height/2;
    d->list->y = HEADER_HEIGHT + LISTVIEW_MARGIN;
    d->list->w = fb_height;
    d->list->h = fb_height - d->list->y - LISTVIEW_MARGIN;
}

static void tab_misc_init(multirom_theme_data *t, tab_data_misc *d, int color_scheme)
{
    int i;
    int x = fb_width/2 - (MISCBTN_W + 30*DPI_MUL);
    int y = HEADER_HEIGHT + ((fb_height - HEADER_HEIGHT)/2 - 2*(MISCBTN_H + 30*DPI_MUL));
    fb_rect *shadow;

    y += MISCBTN_H + 30*DPI_MUL;

    button *b = mzalloc(sizeof(button));
    b->x = x;
    b->y = y;
    b->w = MISCBTN_W;
    b->h = MISCBTN_H;
    b->clicked = &multirom_ui_tab_misc_copy_log;
    shadow = fb_add_rect_lvl(LEVEL_RECT, b->x + BTN_SHADOW_OFF, b->y + BTN_SHADOW_OFF, b->w, b->h, C_BTN_FAKE_SHADOW);
    button_init_ui(b, "COPY LOG TO /SDCARD", SIZE_NORMAL);
    list_add(&d->buttons, b);
    list_add(&d->ui_elements, shadow);
    tabview_add_item(t->tabs, TAB_MISC, b->text);
    tabview_add_item(t->tabs, TAB_MISC, b->rect);
    tabview_add_item(t->tabs, TAB_MISC, b);

    const int max_colors = colors_count();
    x += (MISCBTN_W/2 - (max_colors*(CLRBTN_TOTAL+CLRBTN_MARGIN))/2);
    y += MISCBTN_H+30*DPI_MUL + (MISCBTN_H/2 - CLRBTN_TOTAL/2);
    fb_rect *r;
    for(i = 0; i < max_colors; ++i)
    {
        const struct mrom_color_theme *th = colors_get(i);

        r = fb_add_rect(x, y, CLRBTN_TOTAL, CLRBTN_TOTAL, i == color_scheme ? 0xFFFFCC00 : WHITE);
        list_add(&d->ui_elements, r);

        r = fb_add_rect(x+CLRBTN_B/2, y+CLRBTN_B/2, CLRBTN_W, CLRBTN_W, th->highlight_bg);
        list_add(&d->ui_elements, r);

        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = y;
        b->w = CLRBTN_TOTAL;
        b->h = CLRBTN_TOTAL;
        b->clicked_data = malloc(sizeof(int));
        *((int*)b->clicked_data) = i;
        b->clicked = &multirom_ui_tab_misc_change_clr;
        button_init_ui(b, NULL, 0);
        list_add(&d->buttons, b);
        tabview_add_item(t->tabs, TAB_MISC, b);

        x += CLRBTN_TOTAL + CLRBTN_MARGIN;
    }

    x = fb_width/2 - (MISCBTN_W + 30*DPI_MUL) + MISCBTN_W + 30*DPI_MUL;
    y = HEADER_HEIGHT + ((fb_height - HEADER_HEIGHT)/2 - 2*(MISCBTN_H + 30*DPI_MUL));

    static const char *texts[] =
    {
        "REBOOT",               // 0
        "REBOOT TO RECOVERY",   // 1
        "REBOOT TO BOOTLOADER", // 2
        "SHUTDOWN",             // 3
        NULL
    };

    static const int exit_codes[] = {
        UI_EXIT_REBOOT, UI_EXIT_REBOOT_RECOVERY,
        UI_EXIT_REBOOT_BOOTLOADER, UI_EXIT_SHUTDOWN
    };

    for(i = 0; texts[i]; ++i)
    {
        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = y;
        b->w = MISCBTN_W;
        b->h = MISCBTN_H;
        b->clicked_data = malloc(sizeof(int));
        *((int*)b->clicked_data) = exit_codes[i];
        b->clicked = &multirom_ui_reboot_btn;
        shadow = fb_add_rect_lvl(LEVEL_RECT, b->x + BTN_SHADOW_OFF, b->y + BTN_SHADOW_OFF, b->w, b->h, C_BTN_FAKE_SHADOW);
        button_init_ui(b, texts[i], SIZE_NORMAL);
        list_add(&d->buttons, b);
        list_add(&d->ui_elements, shadow);
        tabview_add_item(t->tabs, TAB_MISC, b->text);
        tabview_add_item(t->tabs, TAB_MISC, b->rect);
        tabview_add_item(t->tabs, TAB_MISC, b);

        y += MISCBTN_H+30*DPI_MUL;
    }

    fb_text *text = fb_add_text(5*DPI_MUL, 0, C_TEXT_SECONDARY, SIZE_SMALL, "MultiROM v%d"VERSION_DEV_FIX" with trampoline v%d.",
                               VERSION_MULTIROM, multirom_get_trampoline_ver());
    text->y = fb_height - text->h;
    list_add(&d->ui_elements, text);

    text = fb_add_text(0, 0, C_TEXT_SECONDARY, SIZE_SMALL, "Battery: %d%%", multirom_get_battery());
    text->x = fb_width - text->w - 5*DPI_MUL;
    text->y = fb_height - text->h;
    list_add(&d->ui_elements, text);

    for(i = 0; d->buttons[i]; ++i)
        keyaction_add(d->buttons[i], button_keyaction_call, d->buttons[i]);

    tabview_add_items(t->tabs, TAB_MISC, d->ui_elements);
}

static int get_tab_width(UNUSED multirom_theme_data *t)
{
    return fb_width;
}

static int get_tab_height(UNUSED multirom_theme_data *t)
{
    return fb_height - HEADER_HEIGHT;
}

const struct multirom_theme theme_info_landscape = {
    .width = TH_LANDSCAPE,
    .height = TH_LANDSCAPE,

    .destroy = &destroy,
    .init_header = &init_header,
    .header_set_tab_selector_pos = &header_set_tab_selector_pos,
    .tab_rom_init = &tab_rom_init,
    .tab_misc_init = &tab_misc_init,
    .get_tab_width = &get_tab_width,
    .get_tab_height = &get_tab_height,
};
