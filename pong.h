#ifndef PONG_H
#define PONG_H

#include "input.h"

void pong(void);
int pong_touch_handler(touch_event *ev, void *data);
void pong_spawn_ball(int side);
void pong_calc_movement(void);
void pong_add_score(int side);
void pong_handle_ai(void);
int pong_do_movement(int step);
int pong_get_collision(int x, int y);

#endif
