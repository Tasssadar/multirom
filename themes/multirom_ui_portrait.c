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

#include "../multirom_ui.h"
#include "../multirom_ui_themes.h"
#include "../multirom.h"
#include "../framebuffer.h"
#include "../util.h"
#include "../button.h"
#include "../version.h"
#include "../input.h"

#define HEADER_HEIGHT (75*DPI_MUL)
#define TAB_BTN_WIDTH (165*DPI_MUL)

#define ROMS_FOOTER_H (130*DPI_MUL)
#define ROMS_HEADER_H (90*DPI_MUL)

#define BOOTBTN_W (300*DPI_MUL)
#define BOOTBTN_H (80*DPI_MUL)

#define REFRESHBTN_W (400*DPI_MUL)
#define REFRESHBTN_H (60*DPI_MUL)

#define MISCBTN_W (530*DPI_MUL)
#define MISCBTN_H (100*DPI_MUL)

#define CLRBTN_W (50*DPI_MUL)
#define CLRBTN_B (10*DPI_MUL)
#define CLRBTN_TOTAL (CLRBTN_W+CLRBTN_B)
#define CLRBTN_Y (1150*DPI_MUL)

static button *pong_btn = NULL;

static void destroy(multirom_theme_data *t)
{
    button_destroy(pong_btn);
    pong_btn = NULL;
}

static void init_header(multirom_theme_data *t)
{
    button **tab_btns = t->tab_btns;
    fb_text **tab_texts = t->tab_texts;

    int i, text_x, text_y;
    int x = fb_width - (TAB_BTN_WIDTH*TAB_COUNT);

    static const char *str[] = { "Internal", "USB", "Misc", "MultiROM" };

    text_x = center_x(0, x, SIZE_EXTRA, str[3]);
    fb_add_text(text_x, 5, WHITE, SIZE_EXTRA, str[3]);

    pong_btn = mzalloc(sizeof(button));
    pong_btn->w = x;
    pong_btn->h = HEADER_HEIGHT;
    pong_btn->clicked = &multirom_ui_start_pong;
    button_init_ui(pong_btn, NULL, 0);

    for(i = 0; i < TAB_COUNT; ++i)
    {
        text_x = center_x(x, TAB_BTN_WIDTH, SIZE_NORMAL, str[i]);
        text_y = center_y(0, HEADER_HEIGHT, SIZE_NORMAL);
        tab_texts[i] = fb_add_text(text_x, text_y, WHITE, SIZE_NORMAL, str[i]);

        fb_add_rect(x, 0, 2, HEADER_HEIGHT, WHITE);

        tab_btns[i] = malloc(sizeof(button));
        memset(tab_btns[i], 0, sizeof(button));
        tab_btns[i]->x = x;
        tab_btns[i]->w = TAB_BTN_WIDTH;
        tab_btns[i]->h = HEADER_HEIGHT;
        tab_btns[i]->action = i;
        tab_btns[i]->clicked = &multirom_ui_switch;
        button_init_ui(tab_btns[i], NULL, 0);

        keyaction_add(tab_btns[i]->x, tab_btns[i]->y, button_keyaction_call, tab_btns[i]);

        x += TAB_BTN_WIDTH;
    }

    fb_add_rect(0, HEADER_HEIGHT, fb_width, 2, WHITE);
}

static void header_select(multirom_theme_data *t, int tab)
{
    int i;
    for(i = 0; i < TAB_COUNT; ++i)
        t->tab_texts[i]->color = (i == tab) ? BLACK : WHITE;

    if(!t->selected_tab_rect)
        t->selected_tab_rect = fb_add_rect(0, 0, TAB_BTN_WIDTH, HEADER_HEIGHT, WHITE);

    t->selected_tab_rect->head.x = fb_width - (TAB_BTN_WIDTH * (TAB_COUNT - tab));
}

static void tab_rom_init(multirom_theme_data *t, tab_data_roms *d, int tab_type)
{
    int base_y = fb_height-ROMS_FOOTER_H;

    d->rom_name = fb_add_text(0, center_y(base_y, ROMS_FOOTER_H, SIZE_NORMAL),
                              WHITE, SIZE_NORMAL, "");

    d->list->y = HEADER_HEIGHT+ROMS_HEADER_H;
    d->list->w = fb_width;
    d->list->h = fb_height - d->list->y - ROMS_FOOTER_H-20;

    // header
    int y = center_y(HEADER_HEIGHT, ROMS_HEADER_H, SIZE_BIG);
    d->title_text = fb_add_text(0, y, CLR_PRIMARY, SIZE_BIG, "");
    list_add(d->title_text, &d->ui_elements);

    // footer
    fb_rect *sep = fb_add_rect(0, fb_height-ROMS_FOOTER_H, fb_width, 2, CLR_PRIMARY);
    list_add(sep, &d->ui_elements);

    // boot btn
    d->boot_btn->x = fb_width - BOOTBTN_W - 20;
    d->boot_btn->y = base_y + (ROMS_FOOTER_H-BOOTBTN_H)/2;
    d->boot_btn->w = BOOTBTN_W;
    d->boot_btn->h = BOOTBTN_H;

    keyaction_add(d->boot_btn->x, d->boot_btn->y, button_keyaction_call, d->boot_btn);
}

static void tab_misc_init(multirom_theme_data *t, tab_data_misc *d, int color_scheme)
{
    int x = fb_width/2 - MISCBTN_W/2;
    int y = 270*DPI_MUL;

    button *b = mzalloc(sizeof(button));
    b->x = x;
    b->y = y;
    b->w = MISCBTN_W;
    b->h = MISCBTN_H;
    b->clicked = &multirom_ui_tab_misc_copy_log;
    button_init_ui(b, "Copy log to /sdcard", SIZE_BIG);
    list_add(b, &d->buttons);

    y += MISCBTN_H+70*DPI_MUL;

    static const char *texts[] =
    {
        "Reboot",               // 0
        "Reboot to recovery",   // 1
        "Reboot to bootloader", // 2
        "Shutdown",             // 3
        NULL
    };

    static const int exit_codes[] = {
        UI_EXIT_REBOOT, UI_EXIT_REBOOT_RECOVERY,
        UI_EXIT_REBOOT_BOOTLOADER, UI_EXIT_SHUTDOWN
    };

    int i;
    for(i = 0; texts[i]; ++i)
    {
        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = y;
        b->w = MISCBTN_W;
        b->h = MISCBTN_H;
        b->action = exit_codes[i];
        b->clicked = &multirom_ui_reboot_btn;
        button_init_ui(b, texts[i], SIZE_BIG);
        list_add(b, &d->buttons);

        y += MISCBTN_H+20*DPI_MUL;
        if(i == 2)
            y += 50*DPI_MUL;
    }

    fb_text *text = fb_add_text(0, fb_height-16*SIZE_SMALL, WHITE, SIZE_SMALL, "MultiROM v%d"VERSION_DEV_FIX" with trampoline v%d.",
                               VERSION_MULTIROM, multirom_get_trampoline_ver());
    list_add(text, &d->ui_elements);

    char bat_text[16];
    sprintf(bat_text, "Battery: %d%%", multirom_get_battery());
    text = fb_add_text_long(fb_width-strlen(bat_text)*8*SIZE_SMALL, fb_height-16*SIZE_SMALL, WHITE, SIZE_SMALL, bat_text);
    list_add(text, &d->ui_elements);

    x = fb_width/2 - (CLRS_MAX*CLRBTN_TOTAL)/2;
    uint32_t p, s;
    fb_rect *r;
    for(i = 0; i < CLRS_MAX; ++i)
    {
        multirom_ui_setup_colors(i, &p, &s);

        if(i == color_scheme)
        {
            r = fb_add_rect(x, CLRBTN_Y, CLRBTN_TOTAL, CLRBTN_TOTAL, WHITE);
            list_add(r, &d->ui_elements);
        }

        r = fb_add_rect(x+CLRBTN_B/2, CLRBTN_Y+CLRBTN_B/2, CLRBTN_W, CLRBTN_W, p);
        list_add(r, &d->ui_elements);

        b = mzalloc(sizeof(button));
        b->x = x;
        b->y = CLRBTN_Y;
        b->w = CLRBTN_TOTAL;
        b->h = CLRBTN_TOTAL;
        b->action = i;
        b->clicked = &multirom_ui_tab_misc_change_clr;
        button_init_ui(b, NULL, 0);
        list_add(b, &d->buttons);

        x += CLRBTN_TOTAL;
    }

    for(i = 0; d->buttons[i]; ++i)
        keyaction_add(d->buttons[i]->x, d->buttons[i]->y, button_keyaction_call, d->buttons[i]);
}

static int get_tab_width(multirom_theme_data *t)
{
    return fb_width;
}

static int get_tab_height(multirom_theme_data *t)
{
    return fb_height - HEADER_HEIGHT;
}

static void center_rom_name(tab_data_roms *d, const char *name)
{
    d->rom_name->head.x = center_x(0, fb_width-BOOTBTN_W-20, SIZE_NORMAL, name);
}

const struct multirom_theme theme_info_portrait = {
    .width = TH_PORTRAIT,
    .height = TH_PORTRAIT,

    .destroy = &destroy,
    .init_header = &init_header,
    .header_select = &header_select,
    .tab_rom_init = &tab_rom_init,
    .tab_misc_init = &tab_misc_init,
    .get_tab_width = &get_tab_width,
    .get_tab_height = &get_tab_height,
    .center_rom_name = &center_rom_name
};
