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
#include <pthread.h>
#include <stdio.h>

#include "pw_ui.h"
#include "encmnt_defines.h"
#include "../lib/framebuffer.h"
#include "../lib/colors.h"
#include "../lib/log.h"
#include "../lib/input.h"
#include "../lib/keyboard.h"
#include "../lib/util.h"
#include "../lib/notification_card.h"
#include "../lib/animation.h"
#include "../lib/workers.h"

#include "crypto/lollipop/cryptfs.h"

#define HEADER_HEIGHT (110*DPI_MUL)

struct pwui_type_pass_data {
    fb_text *passwd_text;
    fb_rect *cursor_rect;
    struct keyboard *keyboard;
};

static pthread_mutex_t exit_code_mutex = PTHREAD_MUTEX_INITIALIZER;
static int exit_code = ENCMNT_UIRES_ERROR;
static void *pwui_type_data = NULL;
static fb_text *invalid_pass_text = NULL;
static button *boot_primary_btn = NULL;

static void boot_internal_clicked(void *data)
{
    ncard_builder *b = ncard_create_builder();
    ncard_set_pos(b, NCARD_POS_CENTER);
    ncard_set_text(b, "Booting the primary ROM...");
    ncard_show(b, 1);

    pthread_mutex_lock(&exit_code_mutex);
    exit_code = ENCMNT_UIRES_BOOT_INTERNAL;
    pthread_mutex_unlock(&exit_code_mutex);
}

static void fade_rect_alpha_step(void *data, float interpolated)
{
    fb_rect *r = data;
    r->color = (((int)(0xFF*interpolated)) << 24);
    fb_request_draw();
}

static void reveal_rect_alpha_step(void *data, float interpolated)
{
    fb_rect *r = data;
    interpolated = 1.f - interpolated;
    r->color = (r->color & ~(0xFF << 24)) | (((int)(0xFF*interpolated)) << 24);
    fb_request_draw();
}

static void try_password(const char *pass)
{
    fb_text_set_content(invalid_pass_text, "");

    ncard_builder *b = ncard_create_builder();
    ncard_set_pos(b, NCARD_POS_CENTER);
    ncard_set_text(b, "Verifying password...");
    ncard_show(b, 1);

    if(cryptfs_check_passwd(pass) != 0)
    {
        ncard_hide();
        fb_text_set_content(invalid_pass_text, "Invalid password!");
        center_text(invalid_pass_text, 0, -1, fb_width, -1);
    }
    else
    {
        ncard_builder *b = ncard_create_builder();
        ncard_set_pos(b, NCARD_POS_CENTER);
        ncard_set_text(b, "Correct!");
        ncard_show(b, 1);

        fb_rect *r = fb_add_rect_lvl(10000, 0, 0, fb_width, fb_height, 0x00000000);
        call_anim *a = call_anim_create(r, fade_rect_alpha_step, 500, INTERPOLATOR_ACCELERATE);
        call_anim_add(a);

        pthread_mutex_lock(&exit_code_mutex);
        exit_code = ENCMNT_UIRES_PASS_OK;
        pthread_mutex_unlock(&exit_code_mutex);
    }
}

static void type_pass_key_pressed(void *data, uint8_t code)
{
    struct pwui_type_pass_data *d = data;

    if(code < 128)
    {
        char *old = fb_text_get_content(d->passwd_text);
        char *new_text = malloc(strlen(old)+2);
        sprintf(new_text, "%s%c", old, (char)code);
        fb_text_set_content(d->passwd_text, new_text);
        center_text(d->passwd_text, 0, 0, fb_width, fb_height);
        free(new_text);
        return;
    }

    switch(code)
    {
        case OSK_BACKSPACE:
        {
            char *old = fb_text_get_content(d->passwd_text);
            int len = strlen(old);
            if(len <= 0)
                break;

            char *new_text = strdup(old);
            new_text[len-1] = 0;
            fb_text_set_content(d->passwd_text, new_text);
            center_text(d->passwd_text, 0, 0, fb_width, fb_height);
            free(new_text);
            break;
        }
        case OSK_CLEAR:
            fb_text_set_content(d->passwd_text, "");
            break;
        case OSK_ENTER:
            try_password(fb_text_get_content(d->passwd_text));
            break;
    }
}

static void type_pass_init(int pwtype)
{
    struct pwui_type_pass_data *d = mzalloc(sizeof(struct pwui_type_pass_data));
    d->keyboard = keyboard_create(pwtype == CRYPT_TYPE_PIN ? KEYBOARD_PIN : KEYBOARD_NORMAL,
            0, fb_height*0.65, fb_width, fb_height*0.35);
    keyboard_set_callback(d->keyboard, type_pass_key_pressed, d);

    d->passwd_text = fb_add_text(0, 0, C_TEXT, SIZE_BIG, "");
    center_text(d->passwd_text, 0, 0, fb_width, fb_height);

    pwui_type_data = d;
}

static void type_pass_destroy(int pwtype)
{
    struct pwui_type_pass_data *d = pwui_type_data;
    keyboard_destroy(d->keyboard);
    free(d);
    pwui_type_data = NULL;
}

static void init_ui(int pwtype)
{
    fb_add_rect_lvl(100, 0, 0, fb_width, HEADER_HEIGHT, C_HIGHLIGHT_BG);

    fb_text_proto *p = fb_text_create(0, 0, C_HIGHLIGHT_TEXT, SIZE_EXTRA, "Encrypted device");
    p->level = 110;
    fb_text *t = fb_text_finalize(p);
    center_text(t, -1, 0, -1, HEADER_HEIGHT);
    t->x = t->y;

    t = fb_add_text(0, HEADER_HEIGHT + 200*DPI_MUL, C_TEXT, SIZE_NORMAL, "Please enter your password:");
    center_text(t, 0, -1, fb_width, -1);

    invalid_pass_text = fb_add_text(0, 0, 0xFFFF0000, SIZE_BIG, "");
    center_text(invalid_pass_text, -1, HEADER_HEIGHT, -1, 200*DPI_MUL);

    boot_primary_btn = mzalloc(sizeof(button));
    boot_primary_btn->w = fb_width*0.30;
    boot_primary_btn->h = HEADER_HEIGHT;
    boot_primary_btn->x = fb_width - boot_primary_btn->w;
    boot_primary_btn->y = 0;
    boot_primary_btn->level_off = 101;
    boot_primary_btn->clicked = &boot_internal_clicked;
    button_init_ui(boot_primary_btn, "BOOT PRIMARY ROM", SIZE_SMALL);

    switch(pwtype)
    {
        case CRYPT_TYPE_PASSWORD:
        case CRYPT_TYPE_PIN:
            type_pass_init(pwtype);
            break;
    }
}

static void destroy_ui(int pwtype)
{
    switch(pwtype)
    {
        case CRYPT_TYPE_PASSWORD:
        case CRYPT_TYPE_PIN:
            type_pass_destroy(pwtype);
            break;
    }
    button_destroy(boot_primary_btn);
}

int pw_ui_run(int pwtype)
{
    if(fb_open(0) < 0)
    {
        ERROR("Failed to open framebuffer");
        return -1;
    }

    fb_freeze(1);
    fb_set_background(C_BACKGROUND);

    workers_start();
    anim_init(1.f);

    init_ui(pwtype);

    start_input_thread();

    fb_freeze(0);

    fb_rect *r = fb_add_rect_lvl(1000, 0, 0, fb_width, fb_height, BLACK);
    call_anim *a = call_anim_create(r, reveal_rect_alpha_step, 500, INTERPOLATOR_ACCELERATE);
    a->on_finished_call = fb_remove_item;
    a->on_finished_data = r;
    call_anim_add(a);

    while(1) {
        pthread_mutex_lock(&exit_code_mutex);
        const int c = exit_code;
        pthread_mutex_unlock(&exit_code_mutex);

        if(c != ENCMNT_UIRES_ERROR)
            break;

        usleep(100000);
    }

    anim_stop(1);
    fb_freeze(1);
    fb_force_draw();

    stop_input_thread();
    workers_stop();

    destroy_ui(pwtype);

    fb_clear();
    fb_close();
    return exit_code;
}
