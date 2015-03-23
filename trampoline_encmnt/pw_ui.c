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
#include "../lib/containers.h"

#include "crypto/lollipop/cryptfs.h"

#define HEADER_HEIGHT (110*DPI_MUL)
#define PWUI_DOT_R (15*DPI_MUL)
#define PWUI_DOT_ACTIVE_R (PWUI_DOT_R/2)
#define PWUI_DOT_ACTIVE_OFF (PWUI_DOT_R - PWUI_DOT_ACTIVE_R)
#define PWUI_LINE_W (12*DPI_MUL)
#define PWUI_DOTS_CNT 9

struct pwui_type_pass_data {
    fb_text *passwd_text;
    fb_rect *cursor_rect;
    struct keyboard *keyboard;
    char *pass_buf;
    char *pass_buf_stars;
    size_t pass_buf_cap;
};

struct pwui_type_pattern_data {
    fb_circle **dots;
    fb_circle **active_dots;
    fb_line **complete_lines;
    fb_line *cur_line;
    int connected_dots[PWUI_DOTS_CNT];
    size_t connected_dots_len;
    int touch_id;
};

static pthread_mutex_t exit_code_mutex = PTHREAD_MUTEX_INITIALIZER;
static int exit_code = ENCMNT_UIRES_ERROR;
static void *pwui_type_data = NULL;
static fb_text *invalid_pass_text = NULL;

static void boot_internal_clicked(UNUSED void *data)
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

static int try_password(const char *pass)
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
        return -1;
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
        return 0;
    }
}

static void type_pass_key_pressed(void *data, uint8_t code)
{
    struct pwui_type_pass_data *d = data;

    if(code < 128)
    {
        size_t pass_len = strlen(d->pass_buf);
        while(d->pass_buf_cap < pass_len + 2)
        {
            d->pass_buf_cap *= 2;
            d->pass_buf = realloc(d->pass_buf, d->pass_buf_cap);
            d->pass_buf_stars = realloc(d->pass_buf_stars, d->pass_buf_cap);
        }

        if(pass_len > 0)
            d->pass_buf_stars[pass_len-1] = '*';
        d->pass_buf_stars[pass_len] = (char)code;
        d->pass_buf_stars[pass_len+1] = 0;

        d->pass_buf[pass_len++] = (char)code;
        d->pass_buf[pass_len] = 0;

        fb_text_set_content(d->passwd_text, d->pass_buf_stars);
        center_text(d->passwd_text, 0, 0, fb_width, fb_height);
        fb_request_draw();

        return;
    }

    switch(code)
    {
        case OSK_BACKSPACE:
        {
            size_t pass_len = strlen(d->pass_buf);
            if(pass_len == 0)
                break;

            d->pass_buf_stars[--pass_len] = 0;
            d->pass_buf[pass_len] = 0;

            fb_text_set_content(d->passwd_text, d->pass_buf_stars);
            center_text(d->passwd_text, 0, 0, fb_width, fb_height);
            fb_request_draw();
            break;
        }
        case OSK_CLEAR:
            d->pass_buf[0] = 0;
            d->pass_buf_stars[0] = 0;
            fb_text_set_content(d->passwd_text, "");
            fb_request_draw();
            break;
        case OSK_ENTER:
            try_password(d->pass_buf);
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

    d->pass_buf_cap = 12;
    d->pass_buf = mzalloc(d->pass_buf_cap);
    d->pass_buf_stars = mzalloc(d->pass_buf_cap);

    pwui_type_data = d;
}

static void type_pass_destroy(void)
{
    struct pwui_type_pass_data *d = pwui_type_data;

    keyboard_destroy(d->keyboard);
    free(d->pass_buf);
    free(d->pass_buf_stars);
    free(d);
    pwui_type_data = NULL;
}

static inline int type_pattern_in_dot(struct pwui_type_pattern_data *d, touch_event *ev)
{
    int i;
    fb_circle *c;
    for(i = 0; d->dots[i]; ++i)
    {
        c = d->dots[i];
        if(in_rect(ev->x, ev->y, c->x - PWUI_DOT_R*1.5, c->y - PWUI_DOT_R*1.5, c->w*3, c->h*3))
            return i;
    }
    return -1;
}

static inline int type_pattern_dot_used(struct pwui_type_pattern_data *d, int dot_idx)
{
    size_t i;
    for(i = 0; i < d->connected_dots_len; ++i)
        if(d->connected_dots[i] == dot_idx)
            return 1;
    return 0;
}

static inline void type_pattern_connect_dot(struct pwui_type_pattern_data *d,  int dot_idx)
{
    if(d->connected_dots_len >= PWUI_DOTS_CNT)
    {
        ERROR("d->connected_dots_len overflowed PWUI_DOTS_CNT!\n");
        return;
    }

    d->connected_dots[d->connected_dots_len++] = dot_idx;

    fb_circle *c = d->dots[dot_idx];
    c = fb_add_circle_lvl(100, c->x+PWUI_DOT_ACTIVE_OFF, c->y+PWUI_DOT_ACTIVE_OFF, PWUI_DOT_ACTIVE_R, C_HIGHLIGHT_TEXT);
    list_add(&d->active_dots, c);
}

static int type_pattern_touch_handler(touch_event *ev, void *data)
{
    struct pwui_type_pattern_data *d = data;

    if(d->touch_id == -1 && (ev->changed & TCHNG_ADDED) && !ev->consumed)
    {
        const int dot_idx = type_pattern_in_dot(d, ev);
        if(dot_idx == -1)
            return -1;

        d->touch_id = ev->id;
        d->connected_dots_len = 0;

        type_pattern_connect_dot(d, dot_idx);

        fb_circle *c = d->dots[dot_idx];
        d->cur_line = fb_add_line(c->x + PWUI_DOT_R, c->y + PWUI_DOT_R, ev->x, ev->y, PWUI_LINE_W, C_HIGHLIGHT_BG);
        fb_request_draw();
        return 0;
    }

    if(d->touch_id != ev->id || !d->cur_line)
        return -1;

    if(ev->changed & TCHNG_POS)
    {
        const int dot_idx = type_pattern_in_dot(d, ev);
        if(dot_idx != -1 && !type_pattern_dot_used(d, dot_idx))
        {
            fb_circle *c = d->dots[dot_idx];
            d->cur_line->x2 = c->x + PWUI_DOT_R;
            d->cur_line->y2 = c->y + PWUI_DOT_R;
            list_add(&d->complete_lines, d->cur_line);
            d->cur_line = fb_add_line(c->x + PWUI_DOT_R, c->y + PWUI_DOT_R, ev->x, ev->y, PWUI_LINE_W, C_HIGHLIGHT_BG);

            const int last_dot = d->connected_dots[d->connected_dots_len-1];
            // The line is vertical and has crossed a point in the middle
            if(dot_idx%3 == last_dot%3 && iabs(dot_idx - last_dot) > 3)
                type_pattern_connect_dot(d, 3 + dot_idx%3);
            // the line is horizontal and has crossed a point in the middle
            else if(dot_idx/3 == last_dot/3 && iabs(dot_idx - last_dot) > 1)
                type_pattern_connect_dot(d, (dot_idx/3)*3 + 1);
            // the line is diagonal and has crossed the middle point
            else if((dot_idx == 0 && last_dot == 8) || (dot_idx == 8 && last_dot == 0) ||
                    (dot_idx == 2 && last_dot == 6) || (dot_idx == 6 && last_dot == 2))
            {
                type_pattern_connect_dot(d, 4);
            }

            type_pattern_connect_dot(d, dot_idx);
        }
        else
        {
            d->cur_line->x2 = ev->x;
            d->cur_line->y2 = ev->y;
        }
        fb_request_draw();
    }

    if(ev->changed & TCHNG_REMOVED)
    {
        d->touch_id = -1;
        fb_rm_line(d->cur_line);
        d->cur_line = NULL;
        fb_request_draw();

        char *passwd = malloc(d->connected_dots_len+1);
        size_t i;
        for(i = 0; i < d->connected_dots_len; ++i)
            passwd[i] = '1' + d->connected_dots[i];
        passwd[i] = 0;

        if(try_password(passwd) < 0)
        {
            list_clear(&d->active_dots, fb_remove_item);
            list_clear(&d->complete_lines, fb_remove_item);
            fb_request_draw();
        }

        free(passwd);
    }

    return 0;
}

static void type_pattern_init(void)
{
    struct pwui_type_pattern_data *d = mzalloc(sizeof(struct pwui_type_pattern_data));
    int cx, cy;

    const int start_x = fb_width*0.2;
    const int step = (fb_width*0.6 - PWUI_DOT_R*2) / 2;
    int x = start_x;
    int y = fb_height/2 - fb_width/4;

    for(cy = 0; cy < 3; ++cy)
    {
        for(cx = 0; cx < 3; ++cx)
        {
            fb_circle *c = fb_add_circle(x, y, PWUI_DOT_R, C_HIGHLIGHT_BG);
            list_add(&d->dots, c);

            x += step;
        }
        x = start_x;
        y += step;
    }

    d->touch_id = -1;
    add_touch_handler(type_pattern_touch_handler, d);

    pwui_type_data = d;
}

static void type_pattern_destroy(void)
{
    struct pwui_type_pattern_data *d = pwui_type_data;
    list_clear(&d->dots, fb_remove_item);
    list_clear(&d->active_dots, fb_remove_item);
    list_clear(&d->complete_lines, fb_remove_item);
    fb_rm_line(d->cur_line);
    free(d);

    pwui_type_data = NULL;
}

static void init_ui(int pwtype)
{
    fb_add_rect_lvl(100, 0, 0, fb_width, HEADER_HEIGHT, C_HIGHLIGHT_BG);

    ncard_set_top_offset(HEADER_HEIGHT);

    fb_text_proto *p = fb_text_create(0, 0, C_HIGHLIGHT_TEXT, SIZE_EXTRA, "Encrypted device");
    p->level = 110;
    fb_text *t = fb_text_finalize(p);
    center_text(t, -1, 0, -1, HEADER_HEIGHT);
    t->x = t->y;

    t = fb_add_text(0, HEADER_HEIGHT + 200*DPI_MUL, C_TEXT, SIZE_NORMAL, "Please enter your password:");
    center_text(t, 0, -1, fb_width, -1);

    invalid_pass_text = fb_add_text(0, 0, 0xFFFF0000, SIZE_BIG, "");
    center_text(invalid_pass_text, -1, HEADER_HEIGHT, -1, 200*DPI_MUL);

    switch(pwtype)
    {
        case CRYPT_TYPE_PASSWORD:
        case CRYPT_TYPE_PIN:
            type_pass_init(pwtype);
            break;
        case CRYPT_TYPE_PATTERN:
            type_pattern_init();
            break;
        default:
            t = fb_add_text(0, 0, C_TEXT, SIZE_NORMAL, "Error: unknown password type %d", pwtype);
            center_text(t, 0, 0, fb_width, fb_height);
            break;
    }
}

static void destroy_ui(int pwtype)
{
    switch(pwtype)
    {
        case CRYPT_TYPE_PASSWORD:
        case CRYPT_TYPE_PIN:
            type_pass_destroy();
            break;
        case CRYPT_TYPE_PATTERN:
            type_pattern_destroy();
            break;
    }
}

static int pw_ui_shutdown_counter_touch_handler(UNUSED touch_event *ev, void *data)
{
    int *shutdown_counter = data;
    if(*shutdown_counter == 0)
        return -1;

    ncard_hide();

    *shutdown_counter = 0;
    return -1;
}

int pw_ui_run(int pwtype)
{
    int shutdown_counter = 0;

    if(fb_open(0) < 0)
    {
        ERROR("Failed to open framebuffer\n");
        return -1;
    }

    fb_freeze(1);
    fb_set_background(C_BACKGROUND);

    workers_start();
    anim_init(1.f);

    init_ui(pwtype);

    start_input_thread();
    add_touch_handler(pw_ui_shutdown_counter_touch_handler, &shutdown_counter);

    fb_freeze(0);

    fb_rect *r = fb_add_rect_lvl(1000, 0, 0, fb_width, fb_height, BLACK);
    call_anim *a = call_anim_create(r, reveal_rect_alpha_step, 500, INTERPOLATOR_ACCELERATE);
    a->on_finished_call = fb_remove_item;
    a->on_finished_data = r;
    call_anim_add(a);

    while(1)
    {
        pthread_mutex_lock(&exit_code_mutex);
        const int c = exit_code;
        pthread_mutex_unlock(&exit_code_mutex);

        if(c != ENCMNT_UIRES_ERROR)
            break;

        if(get_last_key() == KEY_POWER && (!ncard_is_visible() || shutdown_counter))
        {
            ++shutdown_counter;
            if(shutdown_counter == 1)
            {
                ncard_builder *b = ncard_create_builder();
                ncard_set_text(b, "Press power button again to shut down the device.");
                ncard_show(b, 1);
            }
            else
            {
                ncard_builder *b = ncard_create_builder();
                ncard_set_pos(b, NCARD_POS_CENTER);
                ncard_set_text(b, "Shutting down...");
                ncard_show(b, 1);
                break;
            }
        }

        usleep(100000);
    }

    anim_stop(1);
    fb_freeze(1);
    fb_force_draw();

    rm_touch_handler(pw_ui_shutdown_counter_touch_handler, &shutdown_counter);

    stop_input_thread();
    workers_stop();

    destroy_ui(pwtype);

    if(shutdown_counter == 2)
        do_reboot(REBOOT_SHUTDOWN);

    fb_clear();
    fb_close();
    return exit_code;
}
