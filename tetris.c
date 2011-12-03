#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cutils/memory.h>
#include <dirent.h>
#include <sys/poll.h>
#include <sys/reboot.h>
#include <stdarg.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <pthread.h>
#include <dirent.h>

#include "init.h"
#include "bootmgr.h"
#include "tetris.h"

volatile unsigned char state;
volatile unsigned char run_thread;
static pthread_mutex_t tetris_draw_mutex = PTHREAD_MUTEX_INITIALIZER;

tetris_piece *current; // Piece which is currently in air
tetris_piece *preview;
tetris_piece ***pieces;
char cur_id;
pthread_t *t_tetris;
uint32_t score;
uint8_t level;
uint16_t cleared;
uint16_t update_t;

void tetris_init()
{
    tetris_set_defaults();

    pieces = (tetris_piece***)malloc(sizeof(tetris_piece*)*TETRIS_W);
    uint16_t y, z;
    for(y = 0; y < TETRIS_W; ++y)
    {
        pieces[y] = (tetris_piece**)malloc(sizeof(tetris_piece*)*TETRIS_H);
        for(z = 0; z < TETRIS_H; ++z)
            pieces[y][z] = NULL;
    }

    t_tetris = (pthread_t*)malloc(sizeof(pthread_t));
    pthread_create(t_tetris, NULL, tetris_thread, NULL);

    bootmgr_display->bg_img = 0;
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_print_fill(9,                      29,  TETRIS_W*BLOCK_SIZE+1, 1,                     WHITE, TETRIS_BORDER_TOP);
    bootmgr_print_fill(9,                      29,  1,                     TETRIS_H*BLOCK_SIZE+1, WHITE, TETRIS_BORDER_LEFT);
    bootmgr_print_fill(TETRIS_W*BLOCK_SIZE+10, 29,  1,                     TETRIS_H*BLOCK_SIZE+1, WHITE, TETRIS_BORDER_RIGHT);
    bootmgr_print_fill(9,                      470, TETRIS_W*BLOCK_SIZE+1, 1,                     WHITE, TETRIS_BORDER_BOTTOM);

    bootmgr_printf(10, 0, WHITE, "Score: 0");
    bootmgr_printf(10, 0, WHITE, "Score: %5u   Level: %2u", score, level);
    bootmgr_printf(243, 1, WHITE, "Next");
    bootmgr_printf(243, 2, WHITE, "piece:");
    bootmgr_printf(10+((220 - 21*8)/2), 15, WHITE, "Press \"Home\" to start");
    bootmgr_draw();
}

void tetris_set_defaults()
{
    run_thread = 1;
    state = 0;
    current = NULL;
    preview = NULL;
    cur_id = 0;
    score = 0;
    level = 0;
    cleared = 0;
    update_t = 500;
}

void tetris_exit()
{
    run_thread = 0;
    pthread_join(t_tetris, NULL);
    free(t_tetris);

    bootmgr_display->bg_img = 1;
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_draw();
    tetris_clear(1);
}

void tetris_clear(char do_free)
{
    free(current);
    free(preview);

    uint8_t x,y;
    for(x = 0; x < TETRIS_W; ++x)
    {
        for(y = 0; y < TETRIS_H; ++y)
        {
            tetris_piece *p = pieces[x][y];
            tetris_set_piece(p, NULL);
            free(p);
        }
        if(do_free)
            free(pieces[x]);
    }
    if(do_free)
        free(pieces);
}

void tetris_set_piece(tetris_piece *p, tetris_piece *val)
{
    if(!p)
        return;

    uint8_t x,y;
    for(x = 0; x < TETRIS_W; ++x)
        for(y = 0; y < TETRIS_H; ++y)
            if(pieces[x][y] == p)
                pieces[x][y] = val;
}

void tetris_key(int key)
{
    switch(key)
    {
        case KEY_VOLUMEDOWN:
        case KEY_VOLUMEUP:
            tetris_exit();
            break;
        case KEY_HOME:
        {
            if(!(state & TETRIS_STARTED))
            {
                if(state & TETRIS_FINISHED)
                {
                    tetris_clear(0);
                    tetris_set_defaults();
                    bootmgr_erase_text(14);
                }
                bootmgr_erase_text(15);
                state = (TETRIS_STARTED | TETRIS_SPAWN_NEW);
            }
            else
            {
                pthread_mutex_lock(&tetris_draw_mutex);
                tetris_move_piece(current, TETRIS_DOWN_FAST);
                tetris_draw(0);
                pthread_mutex_unlock(&tetris_draw_mutex);
            }
            break;
        }
        case KEY_BACK:
        {
            if(state & TETRIS_STARTED)
            {
                pthread_mutex_lock(&tetris_draw_mutex);
                tetris_rotate_piece();
                tetris_draw(0);
                pthread_mutex_unlock(&tetris_draw_mutex);
            }
            break;
        }
        case KEY_MENU:
        case KEY_SEARCH:
        {
            if(state & TETRIS_STARTED)
            {
                pthread_mutex_lock(&tetris_draw_mutex);
                if(tetris_can_move_piece(current, key == KEY_MENU ? TETRIS_LEFT : TETRIS_RIGHT))
                    tetris_move_piece(current, key == KEY_MENU ? TETRIS_LEFT : TETRIS_RIGHT);
                tetris_draw(0);
                pthread_mutex_unlock(&tetris_draw_mutex);
            }
            break;
        }
    }
}

void *tetris_thread(void *cookie)
{
    while(run_thread)
    {
        if(!(state & TETRIS_STARTED))
        {
            usleep(100000);
            continue;
        }
        if(state & TETRIS_SPAWN_NEW)
        {
            state &= ~(TETRIS_SPAWN_NEW);
            tetris_spawn_new();
        }
        tetris_draw(1);
        usleep(update_t*1000);
    }
    return NULL;
}

void tetris_draw(unsigned char move)
{
    if(move && current)
    {
        pthread_mutex_lock(&tetris_draw_mutex);
        if(!tetris_can_move_piece(current, TETRIS_DOWN))
        {
            if(!current->moved)
            {
                bootmgr_print_fill(11, 14*16, 219, 32, BLACK, 1);
                bootmgr_printf(10+((220 - 9*8)/2),  14, WHITE, "Game over");
                bootmgr_printf(10+((220 - 23*8)/2), 15, WHITE, "Press \"Home\" to restart");
                bootmgr_draw_fills();
                bootmgr_draw_text();
                fb_update(&fb);
                state = TETRIS_FINISHED;
                pthread_mutex_unlock(&tetris_draw_mutex);
                return;
            }

            uint8_t x,y;
            for(x = 0; x < 5; ++x)
                for(y = current->y < 2 ? current->y : 0; y < 5; ++y)
                    if(p_shape[current->type][current->rotation][y][x])
                        pieces[current->x+(x-2)][current->y+(y-2)] = current;

            tetris_check_line();
            tetris_spawn_new();
        }
        else
            tetris_move_piece(current, TETRIS_DOWN);
        pthread_mutex_unlock(&tetris_draw_mutex);
    }

    android_memset16(fb.bits, BLACK, BOOTMGR_DIS_W*BOOTMGR_DIS_H*2);

    bootmgr_draw_fills();
    bootmgr_draw_text();


    tetris_piece *itr = current;
    uint8_t i = 0;
    uint16_t *bits;

    uint8_t y, x;
    do
    {
        for(y = 0; y < 5; ++y)
        {
            uint8_t len = 0;
            int8_t st = -1;
            for(x = 0; x < (i ? 4 : 5); ++x)
            {
                if(p_shape[itr->type][itr->rotation][y][x])
                {
                    if(st == -1)
                        st = x;
                    ++len;
                }
            }
            if(st == -1)
                continue;

            bits = fb.bits;
            bits += BOOTMGR_DIS_W*(30+((itr->y-(2-y))*BLOCK_SIZE)) + 10 + (itr->x-(2-st))*BLOCK_SIZE;
            for(x = 0; x < BLOCK_SIZE; ++x)
            {
                android_memset16(bits, itr->color, len*BLOCK_SIZE*2);
                bits += BOOTMGR_DIS_W;
            }
        }
        itr = preview;
        ++i;
    }while(i < 2);

    for(x = 0; x < TETRIS_W; ++x)
    {
        for(y = 0; y < TETRIS_H; ++y)
        {
            if(!pieces[x][y])
                continue;

            bits = fb.bits;
            bits += BOOTMGR_DIS_W*(30+(y*BLOCK_SIZE)) + 10 + x*BLOCK_SIZE;

            for(i = 0; i < BLOCK_SIZE; ++i)
            {
                android_memset16(bits, pieces[x][y]->color, BLOCK_SIZE*2);
                bits += BOOTMGR_DIS_W;
            }
        }
    }

    fb_update(&fb);
}

void tetris_spawn_new()
{
    char type;
    char rotation;

    if(preview)
        current = preview;
    else
    {
        type = rand()%7;
        rotation = rand()%4;
        current = (tetris_piece*)malloc(sizeof(tetris_piece));
        current->id = cur_id++;
        current->type = type;
        current->rotation = rotation;
        current->color = tetris_get_color_for_type(type);
        current->moved = 0;
    }

    uint8_t x,y;
    current->y = 100;
    current->x = 5;
    for(y = 0; y < 5 && current->y == 100; ++y)
        for(x = 0; x < 5 && current->y == 100; ++x)
            if(p_shape[current->type][current->rotation][y][x])
                current->y = 2-y;

    type = rand()%7;
    rotation = rand()%4;

    preview = (tetris_piece*)malloc(sizeof(tetris_piece));
    preview->id = cur_id++;
    preview->type = type;
    preview->rotation = rotation;
    preview->color = tetris_get_color_for_type(type);
    preview->moved = 0;
    preview->x = 13;
    preview->y = 4;
}

uint16_t tetris_get_color_for_type(char type)
{
    switch(type)
    {
        case TETRIS_PIECE_I: return ((0x3F << 5) | 0x3F);              // cyan,   0   255 255
        case TETRIS_PIECE_J: return 0x3F;                              // blue,   0   0   255
        case TETRIS_PIECE_L: return ((0x3F << 11) | (0x29 << 5));      // orange, 255 165 0
        case TETRIS_PIECE_O: return ((0x3F << 11) | (0x3F << 11));     // yellow, 255 255 0
        case TETRIS_PIECE_S: return (0x3F << 5);                       // green,  0   255 0
        case TETRIS_PIECE_T: return ((0x20 << 10) | 0x20);             // purple, 128 0   128
        case TETRIS_PIECE_Z: return (0x3F << 11);                      // red     255 0   0
    }
    return WHITE;
}

unsigned char tetris_can_move_piece(tetris_piece *p, char dir)
{
    switch(dir)
    {
        case TETRIS_DOWN:
        {
            int8_t bottom_off = -1;
            uint8_t y,z;
            for(y = 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(bottom_off < y && p_shape[p->type][p->rotation][y][z])
                            bottom_off = y;
            if(!(p->y+(bottom_off-2) < TETRIS_H-1))
                return 0;

            for(y = p->y < 1 ? p->y : 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(p_shape[p->type][p->rotation][y][z] && pieces[p->x+(z-2)][p->y+(y-1)])
                        return 0;
            return 1;
        }
        case TETRIS_LEFT:
        {
            int8_t left_off = 100;
            uint8_t y,z;
            for(y = 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(left_off > z && p_shape[p->type][p->rotation][y][z])
                        left_off = z;

            if(!(p->x+(left_off-2) > 0))
                return 0;

            for(y = 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(p_shape[p->type][p->rotation][y][z] && pieces[p->x+(z-3)][p->y+(y-2)])
                        return 0;
            return 1;
        }
        case TETRIS_RIGHT:
        {
            int8_t right_off = -1;
            uint8_t y,z;
            for(y = 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(right_off < z && p_shape[p->type][p->rotation][y][z])
                        right_off = z;

            if(!(p->x+(right_off-2) < TETRIS_W-1))
                return 0;

            for(y = 0; y < 5; ++y)
                for(z = 0; z < 5; ++z)
                    if(p_shape[p->type][p->rotation][y][z] && pieces[p->x+(z-1)][p->y+(y-2)])
                        return 0;
            return 1;
        }
    }
    return 0;
}

void tetris_move_piece(tetris_piece *p, char dir)
{
    switch(dir)
    {
        case TETRIS_DOWN:  ++p->y; break;
        case TETRIS_LEFT:  --p->x; break;
        case TETRIS_RIGHT: ++p->x; break;
        case TETRIS_DOWN_FAST:
        {
            while(tetris_can_move_piece(p, TETRIS_DOWN))
                ++p->y;
            break;
        }
    }
    p->moved = 1;
}

void tetris_rotate_piece()
{
    ++current->rotation;
    if(current->rotation == 4)
        current->rotation = 0;

    if(current->x < 2)
    {
        int8_t left_off = 100;
        uint8_t y,x;
        for(y = 0; y < 5; ++y)
            for(x = 0; x < 5; ++x)
                if(left_off > x && p_shape[current->type][current->rotation][y][x])
                    left_off = x;
        current->x = (2-left_off);
    }
    else if(current->x > 9)
    {
        int8_t right_off = -1;
        uint8_t y,x;
        for(y = 0; y < 5; ++y)
            for(x = 0; x < 5; ++x)
                if(right_off < x && p_shape[current->type][current->rotation][y][x])
                    right_off = x;
        current->x = 11 - right_off - 2;
    }
}

void tetris_check_line()
{
    uint8_t broken;
    uint8_t lines = 0;
    uint8_t x,y,z,h;

    for(y = 0; y < TETRIS_H; ++y)
    {
        broken = 0;
        for(x = 0; x < TETRIS_W && !broken; ++x)
            if(!pieces[x][y])
                broken = 1;
        if(broken)
            continue;

        for(z = y; z > 0; --z)
        {
            for(h = 0; h < TETRIS_W; ++h)
            {
                if(y == z)
                   tetris_delete_if_nowhere(pieces[h][z]);
                pieces[h][z] = pieces[h][z-1];
            }
        }
        ++lines;
    }

    if(lines)
    {
        uint16_t coef = 0;
        switch(lines)
        {
            case 1: coef = 40;   break;
            case 2: coef = 100;  break;
            case 3: coef = 300;  break;
            case 4: coef = 1200; break;
        }
        score += level*coef + coef;
        cleared += lines;
        if(cleared >= (level+1)*LINES_LEVEL)
        {
            ++level;
            update_t -= update_t*0.2;
        }
        bootmgr_printf(10, 0, WHITE, "Score: %5u   Level: %2u", score, level);
    }
}

void tetris_delete_if_nowhere(tetris_piece *p)
{
    uint8_t found = 0;
    uint8_t x,y;
    for(x = 0; x < TETRIS_W && !found; ++x)
        for(y = 0; y < TETRIS_H && !found; ++y)
            if(pieces[x][y] == p)
                found = 1;
    if(!found)
        free(p);
}

