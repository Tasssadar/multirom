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
#include "../log.h"
#include "../animation.h"
#include "../notification_card.h"

#define HEADER_HEIGHT (110*DPI_MUL)
#define STATUS_HEIGHT (33*DPI_MUL)
#define TABS_HEIGHT (HEADER_HEIGHT - STATUS_HEIGHT)
#define MIRI_W (90*DPI_MUL)

#define ROMS_HEADER_H (20*DPI_MUL)

#define BOOTBTN_W (280*DPI_MUL)
#define BOOTBTN_H (80*DPI_MUL)

#define REFRESHBTN_W (400*DPI_MUL)
#define REFRESHBTN_H (60*DPI_MUL)

#define MISCBTN_W (530*DPI_MUL)
#define MISCBTN_H (100*DPI_MUL)

#define CLRBTN_W (50*DPI_MUL)
#define CLRBTN_B (10*DPI_MUL)
#define CLRBTN_TOTAL (CLRBTN_W+CLRBTN_B)
#define CLRBTN_Y (1150*DPI_MUL)

#define SELECTED_RECT_H (10*DPI_MUL)

static button *pong_btn = NULL;
static void destroy(multirom_theme_data *t)
{
    if(pong_btn)
    {
        button_destroy(pong_btn);
        pong_btn = NULL;
    }
}

static void init_header(multirom_theme_data *t)
{
    button **tab_btns = t->tab_btns;
    fb_text **tab_texts = t->tab_texts;
    const int TAB_BTN_WIDTH = fb_width*0.21;
    int i;
    int x = fb_width - (TAB_BTN_WIDTH*TAB_COUNT);
    static const char *str[] = { "Internal", "USB", "Misc", "MultiROM" };
    char buff[64];

    fb_add_rect_lvl(100, 0, 0, fb_width, HEADER_HEIGHT, C_HIGHLIGHT_BG);
    ncard_set_top_offset(HEADER_HEIGHT);

    int maxW = 0;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        fb_text_proto *p = fb_text_create(0, 0, C_HIGHLIGHT_TEXT, SIZE_BIG, str[i]);
        p->level = 101;
        tab_texts[i] = fb_text_finalize(p);
        maxW = imax(maxW, tab_texts[i]->w);
    }

    maxW += (30*DPI_MUL);
    x = fb_width/2 - (maxW*TAB_COUNT)/2;

    snprintf(buff, sizeof(buff), ":/miri_%dx%d.png", (int)MIRI_W, (int)MIRI_W);
    fb_img *logo = fb_add_png_img_lvl(101, x/2 - MIRI_W/2, HEADER_HEIGHT/2 - MIRI_W/2, MIRI_W, MIRI_W, buff);
    if(logo)
    {
        pong_btn = mzalloc(sizeof(button));
        pong_btn->x = logo->x;
        pong_btn->y = logo->y;
        pong_btn->w = logo->w;
        pong_btn->h = logo->h;
        pong_btn->clicked = &multirom_ui_start_pong;
        button_init_ui(pong_btn, NULL, 0);
    }

    for(i = 0; i < TAB_COUNT; ++i)
    {
        center_text(tab_texts[i], x, 0, maxW, HEADER_HEIGHT);

        tab_btns[i] = malloc(sizeof(button));
        memset(tab_btns[i], 0, sizeof(button));
        tab_btns[i]->x = x;
        tab_btns[i]->y = 0;
        tab_btns[i]->w = maxW;
        tab_btns[i]->h = HEADER_HEIGHT;
        tab_btns[i]->action = i;
        tab_btns[i]->clicked = &multirom_ui_switch;
        button_init_ui(tab_btns[i], NULL, 0);

        keyaction_add(tab_btns[i]->x, tab_btns[i]->y, button_keyaction_call, tab_btns[i]);

        x += maxW;
    }
}

static void header_select(multirom_theme_data *t, int tab)
{
    int i;
    const int TAB_BTN_WIDTH = t->tab_btns[0]->w;

    int dest_x = t->tab_btns[tab]->x;
    if(!t->selected_tab_rect)
        t->selected_tab_rect = fb_add_rect_lvl(101, dest_x, HEADER_HEIGHT-SELECTED_RECT_H, TAB_BTN_WIDTH, SELECTED_RECT_H, C_HIGHLIGHT_TEXT);
    else
    {
        anim_cancel_for(t->selected_tab_rect, 0);

        item_anim *anim = item_anim_create(t->selected_tab_rect, 150, INTERPOLATOR_DECELERATE);
        anim->targetX = dest_x;
        item_anim_add(anim);
    }
}

static void tab_rom_init(multirom_theme_data *t, tab_data_roms *d, int tab_type)
{
    d->list->x = ROMS_HEADER_H;
    d->list->y = HEADER_HEIGHT+ROMS_HEADER_H;
    d->list->w = fb_width - ROMS_HEADER_H;
    d->list->h = fb_height - d->list->y - ROMS_HEADER_H;
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

    fb_text *text = fb_add_text(0, 0, C_TEXT_SECONDARY, SIZE_SMALL, "MultiROM v%d"VERSION_DEV_FIX" with trampoline v%d.",
                               VERSION_MULTIROM, multirom_get_trampoline_ver());
    text->y = fb_height - text->h;
    list_add(text, &d->ui_elements);

    text = fb_add_text(0, 0, C_TEXT_SECONDARY, SIZE_SMALL, "Battery: %d%%", multirom_get_battery());
    text->x = fb_width - text->w;
    text->y = fb_height - text->h;
    list_add(text, &d->ui_elements);

    const int max_colors = multirom_ui_get_color_theme_count();
    x = fb_width/2 - (max_colors*CLRBTN_TOTAL)/2;
    fb_rect *r;
    for(i = 0; i < max_colors; ++i)
    {
        const struct multirom_color_theme *th = multirom_ui_get_color_theme(i);

        if(i == color_scheme)
        {
            r = fb_add_rect(x, CLRBTN_Y, CLRBTN_TOTAL, CLRBTN_TOTAL, C_TEXT_SECONDARY);
            list_add(r, &d->ui_elements);
        }

        r = fb_add_rect(x+CLRBTN_B/2, CLRBTN_Y+CLRBTN_B/2, CLRBTN_W, CLRBTN_W, th->highlight_bg);
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
};
