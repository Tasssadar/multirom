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

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#include "lib/framebuffer.h"
#include "lib/input.h"
#include "lib/log.h"
#include "lib/listview.h"
#include "lib/util.h"
#include "lib/button.h"
#include "lib/progressdots.h"
#include "lib/workers.h"
#include "lib/containers.h"
#include "lib/animation.h"
#include "lib/notification_card.h"
#include "lib/tabview.h"
#include "lib/colors.h"

#include "multirom_ui.h"
#include "multirom_ui_themes.h"
#include "hooks.h"
#include "version.h"
#include "pong.h"

static struct multirom_status *mrom_status = NULL;
static struct multirom_rom *selected_rom = NULL;
static volatile int exit_ui_code = -1;
static volatile int loop_act = 0;
static multirom_themes_info *themes_info = NULL;
static multirom_theme *cur_theme = NULL;

static pthread_mutex_t exit_code_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct auto_boot_data
{
    ncard_builder *b;
    int seconds;
    int destroy;
    pthread_mutex_t mutex;
} auto_boot_data = {
    .b = NULL,
    .seconds = 0,
    .destroy = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

uint32_t CLR_PRIMARY = LBLUE;
uint32_t CLR_SECONDARY = LBLUE2;

#define LOOP_UPDATE_USB 0x01
#define LOOP_START_PONG 0x02
#define LOOP_CHANGE_CLR 0x04

static void list_block(char *path, int rec)
{
    ERROR("Listing %s", path);
    DIR *d = opendir(path);
    if(!d)
    {
        ERROR("Failed to open %s", path);
        return;
    }

    struct dirent *dr;
    struct stat info;
    while((dr = readdir(d)))
    {
        if(dr->d_name[0] == '.')
            continue;

        ERROR("%s/%s (%d)", path, dr->d_name, dr->d_type);
        if(dr->d_type == 4 && rec)
        {
            char name[256];
            sprintf(name, "%s/%s", path, dr->d_name);
            list_block(name, 1);
        }
    }

    closedir(d);
}

static void reveal_rect_alpha_step(void *data, float interpolated)
{
    fb_rect *r = data;
    interpolated = 1.f - interpolated;
    r->color = (r->color & ~(0xFF << 24)) | (((int)(0xFF*interpolated)) << 24);
    fb_request_draw();
}

int multirom_ui(struct multirom_status *s, struct multirom_rom **to_boot)
{
    if(s->auto_boot_rom && (s->auto_boot_type & AUTOBOOT_CHECK_KEYS))
    {
        start_input_thread_wait(1);
        int res = is_any_key_pressed() || get_last_key() != -1;
        stop_input_thread();

        if(!res)
        {
            *to_boot = s->auto_boot_rom;
            return UI_EXIT_BOOT_ROM;
        }
    }

    if(multirom_init_fb(s->rotation) < 0)
        return UI_EXIT_BOOT_ROM;

    fb_freeze(1);

    mrom_status = s;

    exit_ui_code = -1;
    selected_rom = NULL;

    colors_select(s->colors);
    themes_info = multirom_ui_init_themes();
    if((cur_theme = multirom_ui_select_theme(themes_info, fb_width, fb_height)) == NULL)
    {
        fb_freeze(0);

        ERROR("Couldn't find theme for resolution %dx%d!\n", fb_width, fb_height);
        fb_add_text(0, 0, WHITE, SIZE_SMALL, "Couldn't find theme for resolution %dx%d!\nPress POWER to reboot.", fb_width, fb_height);
        fb_force_draw();

        start_input_thread();
        while(wait_for_key() != KEY_POWER);
        stop_input_thread();

        fb_clear();
        fb_close();
        return UI_EXIT_REBOOT;
    }

    workers_start();
    anim_init(s->anim_duration_coef);

    multirom_ui_init_theme(TAB_INTERNAL);

    start_input_thread();
    keyaction_enable(1);

    fb_set_brightness(s->brightness);

    if(s->auto_boot_rom && s->auto_boot_seconds > 0 && (s->auto_boot_type & AUTOBOOT_CHECK_KEYS) == 0)
        multirom_ui_auto_boot();
    else
    {
        fb_rect *r = fb_add_rect_lvl(1000, 0, 0, fb_width, fb_height, BLACK);
        call_anim *a = call_anim_create(r, reveal_rect_alpha_step, 500, INTERPOLATOR_ACCELERATE);
        a->on_finished_call = fb_remove_item;
        a->on_finished_data = r;
        call_anim_add(a);
    }

    fb_freeze(0);

    fb_request_draw();

    while(1)
    {
        pthread_mutex_lock(&exit_code_mutex);
        if(exit_ui_code != -1)
        {
            pthread_mutex_unlock(&exit_code_mutex);
            break;
        }

        if(loop_act & LOOP_UPDATE_USB)
        {
            multirom_find_usb_roms(mrom_status);
            multirom_ui_tab_rom_update_usb();
            loop_act &= ~(LOOP_UPDATE_USB);
        }

        if(loop_act & LOOP_START_PONG)
        {
            loop_act &= ~(LOOP_START_PONG);
            keyaction_enable(0);
            input_push_context();
            anim_push_context();
            fb_push_context();

            pong();

            fb_pop_context();
            anim_pop_context();
            input_pop_context();
            keyaction_enable(1);
        }

        if(loop_act & LOOP_CHANGE_CLR)
        {
            pthread_mutex_unlock(&exit_code_mutex);
            fb_freeze(1);

            multirom_ui_destroy_theme();
            colors_select(s->colors);
            multirom_ui_init_theme(TAB_MISC);

            fb_freeze(0);
            fb_request_draw();

            pthread_mutex_lock(&exit_code_mutex);
            loop_act &= ~(LOOP_CHANGE_CLR);
        }

        pthread_mutex_unlock(&exit_code_mutex);

        usleep(100000);
    }

    keyaction_enable(0);
    keyaction_clear();

    ncard_builder *b = ncard_create_builder();
    ncard_set_pos(b, NCARD_POS_CENTER);

    switch(exit_ui_code)
    {
        case UI_EXIT_BOOT_ROM:
        {
            *to_boot = selected_rom;
            ncard_set_title(b, "Booting...");

            char buff[64];
            snprintf(buff, sizeof(buff), "<i>%s</i>", selected_rom->name);
            ncard_set_text(b, buff);
            break;
        }
        case UI_EXIT_REBOOT:
            ncard_set_text(b, "\nRebooting...\n\n");
            break;
        case UI_EXIT_REBOOT_RECOVERY:
            ncard_set_text(b, "\nRebooting to recovery...\n\n");
            break;
        case UI_EXIT_REBOOT_BOOTLOADER:
            ncard_set_text(b, "\nRebooting to bootloader...\n\n");
            break;
        case UI_EXIT_SHUTDOWN:
            ncard_set_text(b, "\nShutting down...\n\n");
            break;
    }

    ncard_show(b, 1);
    anim_stop(1);
    fb_freeze(1);
    fb_force_draw();

    multirom_ui_destroy_theme();
    multirom_ui_free_themes(themes_info);
    themes_info = NULL;

    stop_input_thread();
    workers_stop();

#if MR_DEVICE_HOOKS >= 2
    mrom_hook_before_fb_close();
#endif
    fb_close();
    return exit_ui_code;
}

void multirom_ui_init_theme(int tab)
{
    memset(themes_info->data, 0, sizeof(multirom_theme_data));
    themes_info->data->selected_tab = -1;

    multirom_ui_init_header();
    fb_set_background(C_BACKGROUND);

    themes_info->data->tabs->on_page_changed_by_swipe = multirom_ui_switch;
    themes_info->data->tabs->on_pos_changed = multirom_ui_change_header_selector_pos;

    int i;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        tabview_add_page(themes_info->data->tabs, -1);
        switch(i)
        {
            case TAB_USB:
            case TAB_INTERNAL:
                themes_info->data->tab_data[i] = multirom_ui_tab_rom_init(i);
                break;
            case TAB_MISC:
                themes_info->data->tab_data[i] = multirom_ui_tab_misc_init();
                break;
        }
    }
    add_touch_handler(&tabview_touch_handler, themes_info->data->tabs);
    tabview_update_positions(themes_info->data->tabs);
    multirom_ui_switch(tab);
}

void multirom_ui_destroy_theme(void)
{
    cur_theme->destroy(themes_info->data);

    tabview_destroy(themes_info->data->tabs);
    themes_info->data->tabs = NULL;

    int i;
    for(i = 0; i < TAB_COUNT; ++i)
    {
        button_destroy(themes_info->data->tab_btns[i]);
        themes_info->data->tab_btns[i] = NULL;
        multirom_ui_destroy_tab(i);
    }

    fb_clear();
}

void multirom_ui_init_header(void)
{
    cur_theme->init_header(themes_info->data);
}

void multirom_ui_change_header_selector_pos(float pos)
{
    cur_theme->header_set_tab_selector_pos(themes_info->data, pos);
}

void multirom_ui_destroy_tab(int tab)
{
    switch(tab)
    {
        case -1:
            break;
        case TAB_USB:
        case TAB_INTERNAL:
            multirom_ui_tab_rom_destroy(themes_info->data->tab_data[tab]);
            break;
        case TAB_MISC:
            multirom_ui_tab_misc_destroy(themes_info->data->tab_data[tab]);
            break;
        default:
            assert(0);
            break;
    }
    themes_info->data->tab_data[tab] = NULL;
}

void multirom_ui_switch_btn(void *data)
{
    multirom_ui_switch(*((int*)data));
}

void multirom_ui_switch(int tab)
{
    if(tab == themes_info->data->selected_tab)
        return;

    tabview_set_active_page(themes_info->data->tabs, tab,
            themes_info->data->selected_tab == -1 ? 0 : 200);
    themes_info->data->selected_tab = tab;
}

void multirom_ui_fill_rom_list(listview *view, int mask)
{
    int i;
    struct multirom_rom *rom;
    void *data;
    char part_desc[64];
    for(i = 0; mrom_status->roms && mrom_status->roms[i]; ++i)
    {
        rom = mrom_status->roms[i];

        if(!(M(rom->type) & mask))
            continue;

        if(rom->partition)
            sprintf(part_desc, "%s (%s)", rom->partition->name, rom->partition->fs);

        if(rom->type == ROM_DEFAULT && mrom_status->hide_internal)
            continue;

        data = rom_item_create(rom->name, rom->partition ? part_desc : NULL, rom->icon_path);
        listview_add_item(view, rom->id, data);
    }
}

static void multirom_ui_destroy_auto_boot_data(void)
{
    if(auto_boot_data.b)
    {
        ncard_destroy_builder(auto_boot_data.b);
        auto_boot_data.b = NULL;
    }
    auto_boot_data.destroy = 1;
}

static void multirom_ui_auto_boot_hidden(UNUSED void *data)
{
    pthread_mutex_lock(&auto_boot_data.mutex);
    multirom_ui_destroy_auto_boot_data();
    pthread_mutex_unlock(&auto_boot_data.mutex);
}

static void multirom_ui_auto_boot_now(void *data)
{
    multirom_ui_auto_boot_hidden(data);

    pthread_mutex_lock(&exit_code_mutex);
    selected_rom = mrom_status->auto_boot_rom;
    exit_ui_code = UI_EXIT_BOOT_ROM;
    pthread_mutex_unlock(&exit_code_mutex);
}

static void multirom_ui_auto_boot_tick(UNUSED void *data)
{
    char buff[128];

    pthread_mutex_lock(&auto_boot_data.mutex);

    if(auto_boot_data.destroy)
    {
        pthread_mutex_unlock(&auto_boot_data.mutex);
        return;
    }

    if(--auto_boot_data.seconds == 0)
    {
        multirom_ui_destroy_auto_boot_data();
        pthread_mutex_unlock(&auto_boot_data.mutex);

        pthread_mutex_lock(&exit_code_mutex);
        selected_rom = mrom_status->auto_boot_rom;
        exit_ui_code = UI_EXIT_BOOT_ROM;
        pthread_mutex_unlock(&exit_code_mutex);
    }
    else
    {
        call_anim *a = call_anim_create(NULL, NULL, 1000, INTERPOLATOR_LINEAR);
        a->duration = 1000; // in call_anim_create, duration is multiplied by coef - we don't want that here
        a->on_finished_call = multirom_ui_auto_boot_tick;
        call_anim_add(a);

        snprintf(buff, sizeof(buff), "\n<b>ROM:</b> <y>%s</y>\n\nBooting in %d second%s.",
            mrom_status->auto_boot_rom->name, auto_boot_data.seconds, auto_boot_data.seconds != 1 ? "s" : "");
        ncard_set_text(auto_boot_data.b, buff);
        ncard_show(auto_boot_data.b, 0);
    }

    pthread_mutex_unlock(&auto_boot_data.mutex);
}

void multirom_ui_auto_boot(void)
{
    ncard_builder *b = ncard_create_builder();
    auto_boot_data.b = b;
    auto_boot_data.seconds = mrom_status->auto_boot_seconds + 1;
    auto_boot_data.destroy = 0;

    ncard_set_pos(b, NCARD_POS_CENTER);
    ncard_set_cancelable(b, 1);
    ncard_set_title(b, "Auto-boot");
    ncard_add_btn(b, BTN_NEGATIVE, "Cancel", ncard_hide_callback, NULL);
    ncard_add_btn(b, BTN_POSITIVE, "Boot now", multirom_ui_auto_boot_now, NULL);
    ncard_set_on_hidden(b, multirom_ui_auto_boot_hidden, NULL);
    ncard_set_from_black(b, 1);

    multirom_ui_auto_boot_tick(NULL);
}

void multirom_ui_refresh_usb_handler(void)
{
    pthread_mutex_lock(&exit_code_mutex);
    loop_act |= LOOP_UPDATE_USB;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_start_pong(UNUSED void *data)
{
    pthread_mutex_lock(&exit_code_mutex);
    loop_act |= LOOP_START_PONG;
    pthread_mutex_unlock(&exit_code_mutex);
}

void *multirom_ui_tab_rom_init(int tab_type)
{
    tab_data_roms *t = mzalloc(sizeof(tab_data_roms));
    themes_info->data->tab_data[tab_type] = t;

    t->list = mzalloc(sizeof(listview));
    t->list->item_draw = &rom_item_draw;
    t->list->item_hide = &rom_item_hide;
    t->list->item_height = &rom_item_height;
    t->list->item_destroy = &rom_item_destroy;
    t->list->item_confirmed = &multirom_ui_tab_rom_confirmed;

    cur_theme->tab_rom_init(themes_info->data, t, tab_type);

    listview_init_ui(t->list);
    tabview_add_item(themes_info->data->tabs, tab_type, t->list);

    if(tab_type == TAB_INTERNAL)
        multirom_ui_fill_rom_list(t->list, MASK_INTERNAL);
    else
        multirom_ui_fill_rom_list(t->list, MASK_USB_ROMS);

    listview_update_ui(t->list);

    int has_roms = (int)(t->list->items == NULL);
    multirom_ui_tab_rom_set_empty((void*)t, has_roms);

    if(tab_type == TAB_USB)
    {
        multirom_set_usb_refresh_handler(&multirom_ui_refresh_usb_handler);
        multirom_set_usb_refresh_thread(mrom_status, 1);
    }
    return t;
}

void multirom_ui_tab_rom_destroy(void *data)
{
    multirom_set_usb_refresh_thread(mrom_status, 0);
    pthread_mutex_lock(&exit_code_mutex);
    loop_act &= ~(LOOP_UPDATE_USB);
    pthread_mutex_unlock(&exit_code_mutex);

    tab_data_roms *t = (tab_data_roms*)data;

    list_clear(&t->buttons, &button_destroy);
    list_clear(&t->ui_elements, &fb_remove_item);

    listview_destroy(t->list);

    if(t->usb_prog)
        progdots_destroy(t->usb_prog);

    free(t);
}

void multirom_ui_tab_rom_confirmed(UNUSED listview_item *it)
{
    multirom_ui_tab_rom_boot();
}

void multirom_ui_tab_rom_boot(void)
{
    int cur_tab = themes_info->data->selected_tab;
    if(!themes_info->data->tab_data[cur_tab])
        return;

    tab_data_roms *t = themes_info->data->tab_data[cur_tab];
    if(!t->list->selected)
        return;

    struct multirom_rom *rom = multirom_get_rom_by_id(mrom_status, t->list->selected->id);
    if(!rom)
        return;

    int error = 0;
    ncard_builder *b = ncard_create_builder();
    ncard_set_pos(b, NCARD_POS_CENTER);
    ncard_add_btn(b, BTN_NEGATIVE, "ok", ncard_hide_callback, NULL);
    ncard_set_cancelable(b, 1);
    ncard_set_title(b, "Error");

    int m = M(rom->type);
    if(m & MASK_UNSUPPORTED)
    {
        ncard_set_text(b, "Unsupported ROM type, see XDA thread for more info!");
        error = 1;
    }
    else if (((m & MASK_KEXEC) || ((m & MASK_ANDROID) && rom->has_bootimg)) &&
        !multirom_has_kexec())
    {
        ncard_set_text(b, "Kexec-hardboot support is required to boot this ROM.\n\n"
                "Install kernel with kexec-hardboot support to your Internal ROM!");
        error = 1;
    }
    else if((m & MASK_KEXEC) && strchr(rom->name, ' '))
    {
        ncard_set_text(b, "ROM's name contains spaces. Please remove spaces from this ROM's name");
        error = 1;
    }

    if(error)
    {
        ncard_show(b, 1);
        return;
    }
    else
        ncard_destroy_builder(b);

    pthread_mutex_lock(&exit_code_mutex);
    selected_rom = rom;
    exit_ui_code = UI_EXIT_BOOT_ROM;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_tab_rom_update_usb(void)
{
    tab_data_roms *t = (tab_data_roms*)themes_info->data->tab_data[TAB_USB];
    listview_clear(t->list);

    multirom_ui_fill_rom_list(t->list, MASK_USB_ROMS);
    listview_update_ui(t->list);

    multirom_ui_tab_rom_set_empty(t, (int)(t->list->items == NULL));
    fb_request_draw();
}

void multirom_ui_tab_rom_refresh_usb(UNUSED int action)
{
    multirom_update_partitions(mrom_status);
}

void multirom_ui_tab_rom_set_empty(void *data, int empty)
{
    assert(empty == 0 || empty == 1);

    tab_data_roms *t = (tab_data_roms*)data;

    if(t->boot_btn)
        button_enable(t->boot_btn, !empty);

    if(empty && !t->usb_text)
    {
        fb_text_proto *p = fb_text_create(0, 0, C_TEXT, SIZE_NORMAL, "This list is refreshed automagically, just plug in the USB drive and wait.");
        p->wrap_w = t->list->w - 100*DPI_MUL;
        p->justify = JUSTIFY_CENTER;
        t->usb_text = fb_text_finalize(p);
        list_add(&t->ui_elements, t->usb_text);
        tabview_add_item(themes_info->data->tabs, TAB_USB, t->usb_text);

        center_text(t->usb_text, t->list->x, -1, t->list->w, -1);
        t->usb_text->y = t->list->y + t->list->h*0.2;

        int x = t->list->x + ((t->list->w/2) - (PROGDOTS_W/2));
        t->usb_prog = progdots_create(x, t->usb_text->y+100*DPI_MUL);
        tabview_add_item(themes_info->data->tabs, TAB_USB, t->usb_prog->rect);
        tabview_add_item(themes_info->data->tabs, TAB_USB, t->usb_prog);
    }
    else if(!empty && t->usb_text)
    {
        tabview_rm_item(themes_info->data->tabs, TAB_USB, t->usb_prog->rect);
        tabview_rm_item(themes_info->data->tabs, TAB_USB, t->usb_prog);
        progdots_destroy(t->usb_prog);
        t->usb_prog = NULL;

        tabview_rm_item(themes_info->data->tabs, TAB_USB, t->usb_text);
        list_rm(&t->ui_elements, t->usb_text, &fb_remove_item);
        t->usb_text = NULL;
    }
}

void *multirom_ui_tab_misc_init(void)
{
    tab_data_misc *t = mzalloc(sizeof(tab_data_misc));
    cur_theme->tab_misc_init(themes_info->data, t, mrom_status->colors);
    return t;
}

void multirom_ui_tab_misc_destroy(void *data)
{
    tab_data_misc *t = (tab_data_misc*)data;

    list_clear(&t->ui_elements, &fb_remove_item);
    list_clear(&t->buttons, &button_destroy);

    free(t);
}

void multirom_ui_tab_misc_change_clr(void *data)
{
    int clr = *((int*)data);

    if((loop_act & LOOP_CHANGE_CLR) || mrom_status->colors == clr)
        return;

    pthread_mutex_lock(&exit_code_mutex);
    mrom_status->colors = clr;
    loop_act |= LOOP_CHANGE_CLR;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_reboot_btn(void *data)
{
    int action = *((int*)data);
    pthread_mutex_lock(&exit_code_mutex);
    exit_ui_code = action;
    pthread_mutex_unlock(&exit_code_mutex);
}

void multirom_ui_tab_misc_copy_log(UNUSED void *data)
{
    multirom_dump_status(mrom_status);

    int res = multirom_copy_log(NULL, "../multirom_log.txt");

    static const char *text[] = { "Failed to copy log to sdcard!", "Error log was saved to:\n\n<s>/sdcard/multirom_log.txt</s>" };

    ncard_builder *b = ncard_create_builder();
    ncard_set_pos(b, NCARD_POS_CENTER);
    ncard_add_btn(b, BTN_NEGATIVE, "ok", ncard_hide_callback, NULL);
    ncard_set_title(b, "Save error log");
    ncard_set_text(b, text[res+1]);
    ncard_set_cancelable(b, 1);
    ncard_show(b, 1);
}
