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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <pthread.h>
#include <dirent.h>
#include <assert.h>

#include "input.h"
#include "input_priv.h"
#include "framebuffer.h"
#include "util.h"
#include "log.h"
#include "workers.h"
#include "containers.h"
#include "notification_card.h"

// for touch calculation
int mt_screen_res[2] = { 0 };
touch_event mt_events[MAX_FINGERS];
int mt_slot = 0;
int mt_switch_xy = 0;
int mt_range_x[2] = { 0 };
int mt_range_y[2] = { 0 };

static struct pollfd ev_fds[MAX_DEVICES];
static unsigned ev_count = 0;
static volatile int input_run = 0;

static int key_queue[10];
static int8_t key_itr = 10;
static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t input_thread;

static pthread_mutex_t input_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t input_start_cond = PTHREAD_COND_INITIALIZER;

static handler_list_it *mt_handlers = NULL;
static handlers_ctx **inactive_ctx = NULL;

#define DIV_ROUND_UP(n,d)  (((n) + (d) - 1) / (d))
#define BIT(nr)            (1UL << (nr))
#define BIT_MASK(nr)       (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)       ((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE      8
#define BITS_PER_LONG      (sizeof(long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)  DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

static void get_abs_min_max(int fd)
{
    struct input_absinfo absinfo;

    if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo) >= 0)
    {
        mt_range_x[0] = absinfo.minimum;
        mt_range_x[1] = absinfo.maximum;
    }

    if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo) >= 0)
    {
        mt_range_y[0] = absinfo.minimum;
        mt_range_y[1] = absinfo.maximum;
    }

    mt_switch_xy = (mt_range_x[1] > mt_range_y[1]);
    if(mt_switch_xy)
    {
        int tmp[2];
        memcpy(tmp, mt_range_x, 2*sizeof(int));
        memcpy(mt_range_x, mt_range_y, 2*sizeof(int));
        memcpy(mt_range_y, tmp, 2*sizeof(int));
    }
}

static int ev_init(void)
{
    DIR *dir;
    struct dirent *de;
    int fd;
    long absbit[BITS_TO_LONGS(ABS_CNT)];

    ev_count = 0;
    mt_screen_res[0] = fb_get_vi_xres();
    mt_screen_res[1] = fb_get_vi_yres();

    init_touch_specifics();

    dir = opendir("/dev/input");
    if(!dir)
        return -1;

    while((de = readdir(dir)))
    {
        if(strncmp(de->d_name,"event",5))
            continue;

        fd = openat(dirfd(dir), de->d_name, O_RDONLY | O_CLOEXEC);
        if(fd < 0)
            continue;

        ev_fds[ev_count].fd = fd;
        ev_fds[ev_count].events = POLLIN;

        if (ioctl(fd, EVIOCGBIT(EV_ABS, ABS_CNT), absbit) >= 0)
        {
             if ((absbit[BIT_WORD(ABS_MT_POSITION_X)] & BIT_MASK(ABS_MT_POSITION_X)) &&
                (absbit[BIT_WORD(ABS_MT_POSITION_Y)] & BIT_MASK(ABS_MT_POSITION_Y)))
             {
                 get_abs_min_max(fd);
             }
        }

        ev_count++;
        if(ev_count == MAX_DEVICES) break;
    }
    closedir(dir);

    return 0;
}

static void ev_exit(void)
{
    destroy_touch_specifics();

    while (ev_count > 0) {
        close(ev_fds[--ev_count].fd);
    }
}

static int ev_get(struct input_event *ev, unsigned dont_wait)
{
    int r;
    unsigned n;

    do {
        r = poll(ev_fds, ev_count, dont_wait ? 0 : -1);

        if(r > 0) {
            for(n = 0; n < ev_count; n++) {
                if(ev_fds[n].revents & POLLIN) {
                    r = read(ev_fds[n].fd, ev, sizeof(*ev));
                    if(r == sizeof(*ev)) return 0;
                }
            }
        }
    } while(dont_wait == 0);

    return -1;
}

#define IS_KEY_HANDLED(key) (key >= KEY_VOLUMEDOWN && key <= KEY_POWER)

static int screenshot_trigger_handle_keyevent(int code, int pressed)
{
    static int power_pressed = 0;
    switch(code)
    {
        case KEY_POWER:
            power_pressed = pressed;
            break;
        case KEY_VOLUMEDOWN:
            if(power_pressed && pressed)
            {
                fb_save_screenshot();
                return 0;
            }
            break;
    }
    return -1;
}

static void handle_key_event(struct input_event *ev)
{
    if(!IS_KEY_HANDLED(ev->code))
        return;

    if(screenshot_trigger_handle_keyevent(ev->code, (ev->value != 0)) != -1)
        return;

    if(keyaction_handle_keyevent(ev->code, (ev->value != 0)) != -1)
        return;

    if(ev->value != 0)
        return;

    pthread_mutex_lock(&key_mutex);
    if(key_itr > 0)
        key_queue[--key_itr] = ev->code;
    pthread_mutex_unlock(&key_mutex);
}

int calc_mt_pos(int val, int *range, int d_max)
{
    int res = ((val-range[0])*100);
    res /= (range[1]-range[0]);
    return (res*d_max)/100;
}

static void mt_recalc_pos_rotation(touch_event *ev)
{
    switch(fb_rotation)
    {
        case 0:
            ev->x = ev->orig_x;
            ev->y = ev->orig_y;
            return;
        case 90:
            ev->x = ev->orig_y;
            ev->y = ev->orig_x;

            ev->y = fb_height - ev->y;
            break;
        case 180:
            ev->x = fb_width - ev->orig_x;
            ev->y = fb_height - ev->orig_y;
            break;
        case 270:
            ev->x = ev->orig_y;
            ev->y = ev->orig_x;

            ev->x = fb_width - ev->x;
            break;
    }
}

void touch_commit_events(struct timeval ev_time)
{
    pthread_mutex_lock(&touch_mutex);
    int has_handlers = (mt_handlers != NULL);
    pthread_mutex_unlock(&touch_mutex);

    if(!has_handlers)
        return;

    uint32_t i;
    int res;
    touch_handler *h;
    handler_list_it *it;

    for(i = 0; i < ARRAY_SIZE(mt_events); ++i)
    {
        mt_events[i].us_diff = timeval_us_diff(ev_time, mt_events[i].time);
        mt_events[i].time = ev_time;

        if(!mt_events[i].changed)
            continue;

        keyaction_clear_active();

        if(mt_events[i].changed & TCHNG_POS)
            mt_recalc_pos_rotation(&mt_events[i]);

        pthread_mutex_lock(&touch_mutex);
        it = mt_handlers;
        while(it)
        {
            h = it->handler;

            res = (*h->callback)(&mt_events[i], h->data);
            if(res == 0)
                mt_events[i].consumed = 1;
            else if(res == 1)
                break;

            it = it->next;
        }
        pthread_mutex_unlock(&touch_mutex);

        mt_events[i].consumed = 0;
        mt_events[i].changed = 0;
    }
}

static void *input_thread_work(UNUSED void *cookie)
{
    ev_init();
    struct input_event ev;

    memset(mt_events, 0, sizeof(mt_events));

    key_itr = 10;
    mt_slot = 0;

    pthread_mutex_lock(&input_start_mutex);
    pthread_cond_broadcast(&input_start_cond);
    pthread_mutex_unlock(&input_start_mutex);

    int res;
    while(input_run)
    {
        while(ev_get(&ev, 1) == 0)
        {
            switch(ev.type)
            {
                case EV_KEY:
                    handle_key_event(&ev);
                    break;
                case EV_ABS:
                    handle_abs_event(&ev);
                    break;
                case EV_SYN:
                    handle_syn_event(&ev);
                    break;
            }
        }
        usleep(10000);
    }
    ev_exit();
    return NULL;
}

int get_last_key(void)
{
    int res = -1;
    pthread_mutex_lock(&key_mutex);
    if(key_itr != 10)
        res = key_queue[key_itr++];
    pthread_mutex_unlock(&key_mutex);
    return res;
}

int wait_for_key(void)
{
    int res = -1;
    while(res == -1)
    {
        res = get_last_key();
        usleep(10000);
    }
    return res;
}

int is_any_key_pressed(void)
{
    size_t n, i;
    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    for(n = 0; n < ev_count; ++n)
    {
        if(ioctl(ev_fds[n].fd, EVIOCGKEY(KEY_CNT), keys) >= 0)
            for(i = 0; i < BITS_TO_LONGS(KEY_CNT); ++i)
                if(keys[i] != 0)
                    return 1;
    }
    return 0;
}

void start_input_thread(void)
{
    start_input_thread_wait(0);
}

void start_input_thread_wait(int wait_for_start)
{
    pthread_mutex_lock(&input_start_mutex);
    if(input_run)
    {
        pthread_mutex_unlock(&input_start_mutex);
        return;
    }

    input_run = 1;
    pthread_create(&input_thread, NULL, input_thread_work, NULL);
    if(wait_for_start)
        pthread_cond_wait(&input_start_cond, &input_start_mutex);
    pthread_mutex_unlock(&input_start_mutex);
}

void stop_input_thread(void)
{
    pthread_mutex_lock(&input_start_mutex);
    if(!input_run)
    {
        pthread_mutex_unlock(&input_start_mutex);
        return;
    }

    input_run = 0;
    pthread_join(input_thread, NULL);
    pthread_mutex_unlock(&input_start_mutex);
}


static void add_touch_handler_priv(touch_callback callback, void *data)
{
    touch_handler *handler = mzalloc(sizeof(touch_handler));
    handler->data = data;
    handler->callback = callback;

    handler_list_it *new_it = mzalloc(sizeof(handler_list_it));
    new_it->handler = handler;

    pthread_mutex_lock(&touch_mutex);

    handler_list_it *it = mt_handlers;
    if(mt_handlers)
        it->prev = new_it;
    new_it->next = it;
    mt_handlers = new_it;

    pthread_mutex_unlock(&touch_mutex);
}

static void rm_touch_handler_priv(touch_callback callback, void *data)
{
    pthread_mutex_lock(&touch_mutex);

    handler_list_it *it = mt_handlers;
    while(it)
    {
        if(it->handler->callback != callback || it->handler->data != data)
        {
            it = it->next;
            continue;
        }

        if(it->prev)
            it->prev->next = it->next;
        if(it->next)
            it->next->prev = it->prev;

        if(it == mt_handlers)
            mt_handlers = it->next;

        free(it->handler);
        free(it);
        break;
    }

    pthread_mutex_unlock(&touch_mutex);
}

typedef void (*handler_call)(touch_callback, void*);
struct handler_thread_data
{
    handler_call handler;
    touch_callback callback;
    void *data;
};

static void *touch_handler_thread_work(void *data)
{
    struct handler_thread_data *d = data;
    d->handler(d->callback, d->data);
    free(d);
    return NULL;
}

static void touch_handler_thread_dispatcher(int force_async, handler_call h_c, touch_callback callback, void *data)
{
    if(force_async || pthread_self() == input_thread)
    {
        struct handler_thread_data *d = mzalloc(sizeof(struct handler_thread_data));
        d->handler = h_c;
        d->callback = callback;
        d->data = data;

        pthread_t handler_thread;
        pthread_create(&handler_thread, NULL, touch_handler_thread_work, d);
    }
    else
        h_c(callback, data);
}

void add_touch_handler(touch_callback callback, void *data)
{
   touch_handler_thread_dispatcher(0, add_touch_handler_priv, callback, data);
}

void rm_touch_handler(touch_callback callback, void *data)
{
    touch_handler_thread_dispatcher(0, rm_touch_handler_priv, callback, data);
}

void add_touch_handler_async(touch_callback callback, void *data)
{
   touch_handler_thread_dispatcher(1, add_touch_handler_priv, callback, data);
}

void rm_touch_handler_async(touch_callback callback, void *data)
{
    touch_handler_thread_dispatcher(1, rm_touch_handler_priv, callback, data);
}

void input_push_context(void)
{
    handlers_ctx *ctx = mzalloc(sizeof(handlers_ctx));

    pthread_mutex_lock(&touch_mutex);
    ctx->handlers = mt_handlers;
    mt_handlers = NULL;
    pthread_mutex_unlock(&touch_mutex);

    list_add(&inactive_ctx, ctx);
}

void input_pop_context(void)
{
    if(!inactive_ctx)
        return;

    int idx = list_item_count(inactive_ctx)-1;
    handlers_ctx *ctx = inactive_ctx[idx];

    pthread_mutex_lock(&touch_mutex);
    mt_handlers = ctx->handlers;
    pthread_mutex_unlock(&touch_mutex);

    list_rm_noreorder(&inactive_ctx, ctx, &free);
}

struct keyaction
{
    fb_item_pos *parent;
    void *data;
    keyaction_call call;
};

struct keyaction_ctx
{
    int actions_len;
    struct keyaction **actions;
    struct keyaction *cur_act;
    pthread_mutex_t lock;
    uint32_t repeat_timer;
    int repeat;
    int enable;
};

static struct keyaction_ctx keyaction_ctx = {
    .actions_len = 0,
    .actions = NULL,
    .cur_act = NULL,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .repeat = KEYACT_NONE,
    .enable = 0,
};

#define REPEAT_TIME_FIRST 500
#define REPEAT_TIME 150

static int compare_keyactions(const void* k1, const void* k2)
{
    const struct keyaction *a1 = *((const struct keyaction **)k1);
    const struct keyaction *a2 = *((const struct keyaction **)k2);

    if(a1->parent->y < a2->parent->y)
        return -1;
    else if(a1->parent->y > a2->parent->y)
        return 1;
    else
    {
        if(a1->parent->x < a2->parent->x)
            return -1;
        else if(a1->parent->x > a2->parent->x)
            return 1;
    }
    return 0;
}

void keyaction_add(void *parent, keyaction_call call, void *data)
{
    struct keyaction *k = mzalloc(sizeof(struct keyaction));
    k->parent = parent;
    k->data = data;
    k->call = call;

    pthread_mutex_lock(&keyaction_ctx.lock);

    list_add(&keyaction_ctx.actions, k);
    ++keyaction_ctx.actions_len;

    qsort(keyaction_ctx.actions, keyaction_ctx.actions_len,
          sizeof(struct keyaction *), &compare_keyactions);

    pthread_mutex_unlock(&keyaction_ctx.lock);
}

void keyaction_remove(keyaction_call call, void *data)
{
    pthread_mutex_lock(&keyaction_ctx.lock);
    if(keyaction_ctx.actions)
    {
        int i;
        struct keyaction *a;
        for(i = 0; keyaction_ctx.actions[i]; ++i)
        {
            a = keyaction_ctx.actions[i];
            if(a->call == call && a->data == data)
            {
                if(a == keyaction_ctx.cur_act)
                {
                    a->call(a->data, KEYACT_CLEAR);
                    keyaction_ctx.cur_act = NULL;
                }

                list_rm_at(&keyaction_ctx.actions, i, &free);
                --keyaction_ctx.actions_len;
                break;
            }
        }
    }
    pthread_mutex_unlock(&keyaction_ctx.lock);
}

void keyaction_clear(void)
{
    pthread_mutex_lock(&keyaction_ctx.lock);

    list_clear(&keyaction_ctx.actions, &free);
    keyaction_ctx.actions_len = 0;
    keyaction_ctx.repeat = KEYACT_NONE;
    keyaction_ctx.cur_act = NULL;

    pthread_mutex_unlock(&keyaction_ctx.lock);
}

static int keyaction_is_visible(struct keyaction *a)
{
    return (a->parent->x >= 0 && a->parent->y >= 0 &&
            a->parent->x + a->parent->w <= (int)fb_width &&
            a->parent->y + a->parent->h <= (int)fb_height);
}

// expects locked mutex
static void keyaction_call_cur_act(struct keyaction_ctx *c, int action)
{
    if(!c->cur_act)
        return;

    keyaction_call call = c->cur_act->call;
    void *data = c->cur_act->data;
    int res;

    pthread_mutex_unlock(&c->lock);
    res = (*call)(data, action);
    pthread_mutex_lock(&c->lock);

    if (res != 1 || (action != KEYACT_UP && action != KEYACT_DOWN))
        return;

    struct keyaction **a = c->actions;
    for(; *a; ++a)
    {
        if(*a == c->cur_act)
        {
            do
            {
                if(action == KEYACT_UP)
                    c->cur_act = (a != c->actions) ? *(--a) : NULL;
                else
                    c->cur_act = *(++a);

                if(c->cur_act)
                    ERROR("act %d %d %d %d\n", c->cur_act->parent->x, c->cur_act->parent->y, c->cur_act->parent->w, c->cur_act->parent->h);
            }
            while(c->cur_act && !keyaction_is_visible(c->cur_act));

            if(c->cur_act)
                c->cur_act->call(c->cur_act->data, action);
            return;
        }
    }
    // should never be reached
    ERROR("keyaction_call_cur_act: current action not found in actions!\n");
}

static int keyaction_repeat_worker(uint32_t diff, void *data)
{
    struct keyaction_ctx *c = data;

    pthread_mutex_lock(&c->lock);
    if(c->repeat != KEYACT_NONE)
    {
        if(c->repeat_timer <= diff)
        {
            keyaction_call_cur_act(c, c->repeat);
            c->repeat_timer = REPEAT_TIME;
        }
        else
            c->repeat_timer -= diff;
    }
    pthread_mutex_unlock(&c->lock);

    return 0;
}

void keyaction_clear_active(void)
{
    pthread_mutex_lock(&keyaction_ctx.lock);
    if(keyaction_ctx.enable && keyaction_ctx.cur_act)
    {
        keyaction_call_cur_act(&keyaction_ctx, KEYACT_CLEAR);
        keyaction_ctx.repeat = KEYACT_NONE;
        keyaction_ctx.cur_act = NULL;
    }
    pthread_mutex_unlock(&keyaction_ctx.lock);
}

int keyaction_handle_keyevent(int key, int press)
{
    int res = -1;
    int act = KEYACT_NONE;
    switch(key)
    {
        case KEY_POWER:
            act = KEYACT_CONFIRM;
            break;
        case KEY_VOLUMEDOWN:
            act = KEYACT_DOWN;
            break;
        case KEY_VOLUMEUP:
            act = KEYACT_UP;
            break;
    }

    pthread_mutex_lock(&keyaction_ctx.lock);
    if(keyaction_ctx.enable == 0 || !keyaction_ctx.actions)
        goto exit;

    res = 0;

    if(press == 1 && ncard_try_cancel())
        goto exit;

    if(keyaction_ctx.repeat == act && press == 0)
        keyaction_ctx.repeat = KEYACT_NONE;
    else if(keyaction_ctx.repeat == KEYACT_NONE && press == 1)
    {
        if(keyaction_ctx.cur_act == NULL)
        {
            if(act == KEYACT_DOWN)
                keyaction_ctx.cur_act = *keyaction_ctx.actions;
            else if(act == KEYACT_UP)
                keyaction_ctx.cur_act = *(keyaction_ctx.actions + keyaction_ctx.actions_len - 1);
            else
                goto exit;
        }

        keyaction_call_cur_act(&keyaction_ctx, act);

        if(act != KEYACT_CONFIRM)
        {
            keyaction_ctx.repeat = act;
            keyaction_ctx.repeat_timer = REPEAT_TIME_FIRST;
        }
    }

exit:
    pthread_mutex_unlock(&keyaction_ctx.lock);
    return res;
}

void keyaction_enable(int enable)
{
    pthread_mutex_lock(&keyaction_ctx.lock);
    if(enable != keyaction_ctx.enable)
    {
        keyaction_ctx.enable = enable;
        pthread_mutex_unlock(&keyaction_ctx.lock);

        if(enable)
            workers_add(&keyaction_repeat_worker, &keyaction_ctx);
        else
            workers_remove(&keyaction_repeat_worker, &keyaction_ctx);
    }
    else
        pthread_mutex_unlock(&keyaction_ctx.lock);
}
