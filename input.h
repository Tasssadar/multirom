#ifndef INPUT_H
#define INPUT_H

#include <sys/time.h>

#define KEY_VOLUMEUP 115
#define KEY_VOLUMEDOWN 114
#define KEY_POWER 116

enum 
{
    TCHNG_POS       = 0x01,
    //TCHNG_PRESSURE  = 0x02, // unused
    TCHNG_ADDED     = 0x04,
    TCHNG_REMOVED   = 0x08
};

typedef struct
{
    int id;
    int x;
    int y;
    int changed;

    struct timeval time;
    int64_t us_diff;
} touch_event;

typedef int (*touch_callback)(touch_event*, void*); // event, data
typedef struct
{
    void *data;
    touch_callback callback;
} touch_handler;

void start_input_thread(void);
void stop_input_thread(void);

int get_last_key(void);
int wait_for_key(void);

void add_touch_handler(touch_callback callback, void *data);
void rm_touch_handler(touch_callback callback, void *data);

#endif