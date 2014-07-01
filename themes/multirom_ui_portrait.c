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

#define HEADER_HEIGHT (75*DPI_MUL)

#define ROMS_FOOTER_H (130*DPI_MUL)
#define ROMS_HEADER_H (90*DPI_MUL)

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

    const int TAB_BTN_WIDTH = fb_width*0.21;

    int i;
    int x = fb_width - (TAB_BTN_WIDTH*TAB_COUNT);

    static const char *str[] = { "Internal", "USB", "Misc", "MultiROM" };

    fb_text *title = fb_add_text(0, 0, WHITE, SIZE_EXTRA, str[3]);
    center_text(title, 0, 0, x, HEADER_HEIGHT);

    pong_btn = mzalloc(sizeof(button));
    pong_btn->w = x;
    pong_btn->h = HEADER_HEIGHT;
    pong_btn->clicked = &multirom_ui_start_pong;
    button_init_ui(pong_btn, NULL, 0);

    for(i = 0; i < TAB_COUNT; ++i)
    {
        tab_texts[i] = fb_add_text(0, 0, WHITE, SIZE_NORMAL, str[i]);
        center_text(tab_texts[i], x, 0, TAB_BTN_WIDTH, HEADER_HEIGHT);

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
        fb_text_set_color(t->tab_texts[i], (i == tab) ? BLACK : WHITE);

    const int TAB_BTN_WIDTH = fb_width*0.21;

    if(!t->selected_tab_rect)
        t->selected_tab_rect = fb_add_rect(0, 0, TAB_BTN_WIDTH, HEADER_HEIGHT, WHITE);

    t->selected_tab_rect->x = fb_width - (TAB_BTN_WIDTH * (TAB_COUNT - tab));
}

static void tab_rom_init(multirom_theme_data *t, tab_data_roms *d, int tab_type)
{
    int base_y = fb_height-ROMS_FOOTER_H;

    d->rom_name = fb_add_text(0, 0, WHITE, SIZE_NORMAL, "");

    d->list->y = HEADER_HEIGHT+ROMS_HEADER_H;
    d->list->w = fb_width;
    d->list->h = fb_height - d->list->y - ROMS_FOOTER_H-20;

    // header
    d->title_text = fb_add_text(0, 0, CLR_PRIMARY, SIZE_BIG, "W");
    center_text(d->title_text, -1, HEADER_HEIGHT, -1, ROMS_HEADER_H);
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

static void animation_finished(void *data)
{
    fb_img *t = fb_add_text(50, HEADER_HEIGHT, CLR_PRIMARY, SIZE_BIG, "Finished!");
    tab_data_misc *d = data;
    list_add(t, &d->ui_elements);
    fb_request_draw();

    item_anim *anim = item_anim_create(t, 3000, INTERPOLATOR_LINEAR);
    anim->targetY = fb_height - t->h - 20;
    anim->targetX = fb_width - t->w;
    item_anim_add(anim);
}

static void tab_misc_init(multirom_theme_data *t, tab_data_misc *d, int color_scheme)
{
    int x = fb_width/2 - MISCBTN_W/2;
    int y = 270*DPI_MUL;

    fb_rect *rt = fb_add_rect(20*DPI_MUL, y, 0, 0, WHITE);

    list_add(rt, &d->ui_elements);

    item_anim *anim = item_anim_create(rt, 200, INTERPOLATOR_DECELERATE);
    anim->targetW = fb_width - 40*DPI_MUL;
    item_anim_add(anim);

    anim = item_anim_create(rt, 400, INTERPOLATOR_OVERSHOOT);
    anim->start_offset = 200;
    anim->targetH = fb_height - y - 200*DPI_MUL;
    item_anim_add(anim);

    anim = item_anim_create(rt, 200, INTERPOLATOR_ACCELERATE);
    anim->start_offset = 3000;
    anim->targetH = 10*DPI_MUL;
    item_anim_add(anim);

    anim = item_anim_create(rt, 500, INTERPOLATOR_ACCELERATE);
    anim->start_offset = 3200;
    anim->targetX = 20*DPI_MUL + (fb_width - 40*DPI_MUL);
    anim->targetW = 0;
    anim->on_finished_data = d;
    anim->on_finished_call = animation_finished;
    item_anim_add(anim);

    return;

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

    fb_text *text = fb_add_text(0, 0, WHITE, SIZE_SMALL, "MultiROM v%d"VERSION_DEV_FIX" with trampoline v%d.",
                               VERSION_MULTIROM, multirom_get_trampoline_ver());
    text->y = fb_height - text->h;
    list_add(text, &d->ui_elements);

    char bat_text[16];
    snprintf(bat_text, sizeof(bat_text), "Battery: %d%%", multirom_get_battery());
    text = fb_add_text_long(0, 0, WHITE, SIZE_SMALL, bat_text);
    text->x = fb_width - text->w;
    text->y = fb_height - text->h;
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

static void set_rom_name(tab_data_roms *d, const char *name)
{
    fb_text_set_content(d->rom_name, name);
    center_text(d->rom_name, 0, fb_height-ROMS_FOOTER_H, fb_width-BOOTBTN_W-20, ROMS_FOOTER_H);
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
    .set_rom_name = &set_rom_name
};
