#include <unistd.h>
#include "progressdots.h"
#include "multirom_ui.h"

// ms
#define SWITCH_SPEED 800
#define THREAD_SLEEP 50

static void *progdots_thread(void *data)
{
    progdots *p = (progdots*)data;
    int timer = SWITCH_SPEED;

    while(p->run)
    {
        if(timer <= THREAD_SLEEP)
        {
            if(++p->active_dot >= PROGDOTS_CNT)
                p->active_dot = 0;

            progdots_set_active(p, p->active_dot); 
            fb_draw();

            timer = SWITCH_SPEED;
        }
        else timer -= THREAD_SLEEP;

        usleep(THREAD_SLEEP*1000);
    }
    return NULL;
}

progdots *progdots_create(int x, int y)
{
    progdots *p = malloc(sizeof(progdots));
    memset(p, 0, sizeof(progdots));
    p->x = x;
    p->y = y;
    p->run = 1;

    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
    {
        p->dots[i] = fb_add_rect(x, y, PROGDOTS_H, PROGDOTS_H, (i == 0 ? CLR_PRIMARY : WHITE));
        x += PROGDOTS_H + (PROGDOTS_W - (PROGDOTS_CNT*PROGDOTS_H))/(PROGDOTS_CNT-1);
    }
    pthread_create(&p->thread, NULL, progdots_thread, p);
    fb_draw();
    return p;
}

void progdots_destroy(progdots *p)
{
    p->run = 0;
    pthread_join(p->thread, NULL);

    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
        fb_rm_rect(p->dots[i]);
    free(p);
}

void progdots_set_active(progdots *p, int dot)
{
    p->active_dot = dot;
    int i;
    for(i = 0; i < PROGDOTS_CNT; ++i)
        p->dots[i]->color = (i == dot ? CLR_PRIMARY : WHITE);
}
