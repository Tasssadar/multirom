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

#include "input.h"
#include "framebuffer.h"
#include "util.h"
#include "log.h"

#define MAX_DEVICES 16

// for touch calculation
static int screen_res[2] = { 0 };

static struct pollfd ev_fds[MAX_DEVICES];
static unsigned ev_count = 0;
static volatile int input_run = 0;

static int key_queue[10];
static int8_t key_itr = 10;
static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t input_thread;

static touch_event mt_events[10];
static int mt_slot = 0;
static int switch_xy = 0;
static int mt_range_x[2] = { 0 };
static int mt_range_y[2] = { 0 };

struct handler_list_it
{
    touch_handler *handler;

    struct handler_list_it *prev;
    struct handler_list_it *next;
};

typedef struct handler_list_it handler_list_it;

typedef struct
{
    int handlers_mode;
    handler_list_it *handlers;
} handlers_ctx;

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


    switch_xy = (mt_range_x[1] > mt_range_y[1]);
    if(switch_xy)
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
    screen_res[0] = fb->vi.xres;
    screen_res[1] = fb->vi.yres;

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
    if(ev->value != 0 || !IS_KEY_HANDLED(ev->code))
        return;

    pthread_mutex_lock(&key_mutex);
    if(key_itr > 0)
        key_queue[--key_itr] = ev->code;
    pthread_mutex_unlock(&key_mutex);
}

static int calc_mt_pos(int val, int *range, int d_max)
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

static void handle_touch_event(struct input_event *ev)
{
    switch(ev->code)
    {
        case ABS_MT_SLOT:
            if(ev->value < (int)ARRAY_SIZE(mt_events))
                mt_slot = ev->value;
            break;
        case ABS_MT_TRACKING_ID:
        {
            if(ev->value != -1)
            {
                mt_events[mt_slot].id = ev->value;
                mt_events[mt_slot].changed |= TCHNG_ADDED;
            }
            else
                mt_events[mt_slot].changed |= TCHNG_REMOVED;
            break;
        }
        case ABS_MT_POSITION_X:
        case ABS_MT_POSITION_Y:
        {
            if((ev->code == ABS_MT_POSITION_X) ^ (switch_xy != 0))
            {
                mt_events[mt_slot].orig_x = calc_mt_pos(ev->value, mt_range_x, screen_res[0]);
                if(switch_xy)
                    mt_events[mt_slot].orig_x = screen_res[0] - mt_events[mt_slot].orig_x;
            }
            else
                mt_events[mt_slot].orig_y = calc_mt_pos(ev->value, mt_range_y, screen_res[1]);

            mt_events[mt_slot].changed |= TCHNG_POS;
            break;
        }
    }
}

static void handle_touch_syn(struct input_event *ev)
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
        mt_events[i].us_diff = get_us_diff(ev->time, mt_events[i].time);
        mt_events[i].time = ev->time;

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
                    handle_touch_event(&ev);
                    break;
                case EV_SYN:
                    if(ev.code == SYN_REPORT)
                        handle_touch_syn(&ev);
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
