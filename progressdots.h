#ifndef PROGRESSDOTS_H
#define PROGRESSDOTS_H

#include <pthread.h>
#include "framebuffer.h"

#define PROGDOTS_W 300
#define PROGDOTS_H 10
#define PROGDOTS_CNT 8

typedef struct
{
    int x, y;
    pthread_t thread;
    volatile int run;
    fb_rect *dots[PROGDOTS_CNT];
    int active_dot;
} progdots;

progdots *progdots_create(int x, int y);
void progdots_destroy(progdots *p);
void progdots_set_active(progdots *p, int dot);

#endif
