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

static handler_list_it *mt_handlers = NULL;
static handlers_ctx **inactive_ctx = NULL;
static int mt_handlers_mode = HANDLERS_FIRST;

#define DIV_ROUND_UP(n,d)  (((n) + (d) - 1) / (d))
#define BIT(nr)            (1UL << (nr))
#define BIT_MASK(nr)       (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)       ((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE      8
#define BITS_PER_LONG      (sizeof(long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)  DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

static void get_abs_min_max(int fd)
{
    int abs[5];
    if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), abs) >= 0)
        memcpy(mt_range_x, abs+1, 2*sizeof(int));

    if(ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), abs) >= 0)
        memcpy(mt_range_y, abs+1, 2*sizeof(int));


    mt_switch_xy = (mt_range_x[1] > mt_range_y[1]);
    if(mt_switch_xy)
    {
        memcpy(abs, mt_range_x, 2*sizeof(int));
        memcpy(mt_range_x, mt_range_y, 2*sizeof(int));
        memcpy(mt_range_y, abs, 2*sizeof(int));
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

        fd = openat(dirfd(dir), de->d_name, O_RDONLY);
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

static void handle_key_event(struct input_event *ev)
{
    if(!IS_KEY_HANDLED(ev->code))
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

static inline int64_t get_us_diff(struct timeval now, struct timeval prev)
{
    return ((int64_t)(now.tv_sec - prev.tv_sec))*1000000+
        (now.tv_usec - prev.tv_usec);
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
    touch_handler *h;
    handler_list_it *it;

    for(i = 0; i < ARRAY_SIZE(mt_events); ++i)
    {
        mt_events[i].us_diff = get_us_diff(ev_time, mt_events[i].time);
        mt_events[i].time = ev_time;

        if(!mt_events[i].changed)
            continue;

        if(mt_events[i].changed & TCHNG_POS)
            mt_recalc_pos_rotation(&mt_events[i]);

        it = mt_handlers;
        while(it)
        {
            h = it->handler;
            if((*h->callback)(&mt_events[i], h->data) == 0 && mt_handlers_mode == HANDLERS_FIRST)
                break;
            it = it->next;
        }

        mt_events[i].changed = 0;
    }
}

static void *input_thread_work(void *cookie)
{
    ev_init();
    struct input_event ev;

    memset(mt_events, 0, sizeof(mt_events));

    key_itr = 10;
    mt_slot = 0;

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
    pthread_exit(NULL);
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

void start_input_thread(void)
{
    if(input_run)
        return;

    input_run = 1;
    pthread_create(&input_thread, NULL, input_thread_work, NULL);
}

void stop_input_thread(void)
{
    if(!input_run)
        return;
    input_run = 0;
    pthread_join(input_thread, NULL);
}

void add_touch_handler(touch_callback callback, void *data)
{
    touch_handler *handler = malloc(sizeof(touch_handler));
    handler->data = data;
    handler->callback = callback;

    handler_list_it *new_it = malloc(sizeof(handler_list_it));
    memset(new_it, 0, sizeof(handler_list_it));
    new_it->handler = handler;

    pthread_mutex_lock(&touch_mutex);

    handler_list_it **it = &mt_handlers;
    while(*it)
    {
        if(!(*it)->next)
            new_it->prev = *it;

        it = &((*it)->next);
    }
    *it = new_it;

    pthread_mutex_unlock(&touch_mutex);
}

void rm_touch_handler(touch_callback callback, void *data)
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

void set_touch_handlers_mode(int mode)
{
    mt_handlers_mode = mode;
}

void input_push_context(void)
{
    handlers_ctx *ctx = malloc(sizeof(handlers_ctx));
    memset(ctx, 0, sizeof(handlers_ctx));

    pthread_mutex_lock(&touch_mutex);

    ctx->handlers_mode = mt_handlers_mode;
    ctx->handlers = mt_handlers;

    mt_handlers_mode = HANDLERS_FIRST;
    mt_handlers = NULL;

    pthread_mutex_unlock(&touch_mutex);

    list_add(ctx, &inactive_ctx);
}

void input_pop_context(void)
{
    if(!inactive_ctx)
        return;

    int idx = list_item_count(inactive_ctx)-1;
    handlers_ctx *ctx = inactive_ctx[idx];

    pthread_mutex_lock(&touch_mutex);

    mt_handlers_mode = ctx->handlers_mode;
    mt_handlers = ctx->handlers;

    pthread_mutex_unlock(&touch_mutex);

    list_rm_noreorder(ctx, &inactive_ctx, &free);
}

struct keyaction
{
    int x, y;
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
    int (*destroy_msgbox)(void);
};

static struct keyaction_ctx keyaction_ctx = {
    .actions_len = 0,
    .actions = NULL,
    .cur_act = NULL,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .repeat = KEYACT_NONE,
    .enable = 0,
    .destroy_msgbox = NULL,
};

#define REPEAT_TIME_FIRST 500
#define REPEAT_TIME 100

static int compare_keyactions(const void* k1, const void* k2)
{
    const struct keyaction *a1 = *((const struct keyaction **)k1);
    const struct keyaction *a2 = *((const struct keyaction **)k2);

    if(a1->y < a2->y)
        return -1;
    else if(a1->y > a2->y)
        return 1;
    else
    {
        if(a1->x < a2->x)
            return -1;
        else if(a1->x > a2->x)
            return 1;
    }
    return 0;
}

void keyaction_add(int x, int y, keyaction_call call, void *data)
{
    struct keyaction *k = mzalloc(sizeof(struct keyaction));
    k->x = x;
    k->y = y;
    k->data = data;
    k->call = call;

    pthread_mutex_lock(&keyaction_ctx.lock);

    list_add(k, &keyaction_ctx.actions);
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

                list_rm_at(i, &keyaction_ctx.actions, &free);
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
            if(action == KEYACT_UP)
                c->cur_act = (a != c->actions) ? *(--a) : NULL;
            else
                c->cur_act = *(++a);

            if(c->cur_act)
                c->cur_act->call(c->cur_act->data, action);
            return;
        }
    }
    // should never be reached
    ERROR("keyaction_call_cur_act: current action not found in actions!\n");
}

static void keyaction_repeat_worker(uint32_t diff, void *data)
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

    if(press == 1 && keyaction_ctx.destroy_msgbox() == 1)
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
        if(enable)
            workers_add(&keyaction_repeat_worker, &keyaction_ctx);
        else
            workers_remove(&keyaction_repeat_worker, &keyaction_ctx);
    }
    pthread_mutex_unlock(&keyaction_ctx.lock);
}

void keyaction_set_destroy_msgbox_handle(int (*handler)(void))
{
    pthread_mutex_lock(&keyaction_ctx.lock);
    keyaction_ctx.destroy_msgbox = handler;
    pthread_mutex_unlock(&keyaction_ctx.lock);
}
