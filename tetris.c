#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cutils/memory.h>
#include <linux/input.h>
#include <stdarg.h>
#include <pthread.h>

#include "init.h"
#include "bootmgr.h"
#include "bootmgr_shared.h"
#include "tetris.h"

volatile uint8_t state;
volatile uint8_t run_thread;
pthread_mutex_t *tetris_draw_mutex;

tetris_piece *current; // Piece which is currently in air
tetris_piece *preview;
tetris_piece ***pieces;
pthread_t *t_tetris;
uint32_t score;
uint8_t level;
uint16_t cleared;
uint16_t update_t;

const uint16_t score_coef[] = { 0, 40, 100, 300, 1200 };

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

    pthread_mutex_init(tetris_draw_mutex, NULL);
    t_tetris = (pthread_t*)malloc(sizeof(pthread_t));
    pthread_create(t_tetris, NULL, tetris_thread, NULL);

    bootmgr_display->bg_img = 0;
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_print_fill(9,                      29,  TETRIS_W*BLOCK_SIZE+1, 1,                     WHITE, TETRIS_BORDER_TOP);
    bootmgr_print_fill(9,                      29,  1,                     TETRIS_H*BLOCK_SIZE+1, WHITE, TETRIS_BORDER_LEFT);
    bootmgr_print_fill(TETRIS_W*BLOCK_SIZE+10, 29,  1,                     TETRIS_H*BLOCK_SIZE+1, WHITE, TETRIS_BORDER_RIGHT);
    bootmgr_print_fill(9,                      470, TETRIS_W*BLOCK_SIZE+1, 1,                     WHITE, TETRIS_BORDER_BOTTOM);

    tetris_print_score();
    tetris_print_batt();
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

    pthread_mutex_destroy(tetris_draw_mutex);

    tetris_clear(1);

    bootmgr_display->bg_img = 1;
    bootmgr_set_lines_count(0);
    bootmgr_set_fills_count(0);
    bootmgr_phase = BOOTMGR_MAIN;
    bootmgr_draw();
}

void tetris_clear(uint8_t do_free)
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
        {
            if(state & TETRIS_PAUSED)
            {
                bootmgr_erase_text(14);
                bootmgr_erase_text(15);
                bootmgr_erase_fill(1);
                state &= ~(TETRIS_PAUSED);
                state |= TETRIS_STARTED;
            }
            else if(state & TETRIS_STARTED)
            {
                bootmgr_print_fill(11, 14*ISO_CHAR_HEIGHT, 219, 32, BLACK, 1);
                bootmgr_printf(10+((220 - 6*8)/2),  14, WHITE, "Paused");
                bootmgr_printf(10+((220 - 25*8)/2), 15, WHITE, "Press \"VolDown\" to resume");
                bootmgr_draw_fills();
                bootmgr_draw_text();
                fb_update(&fb);
                state &= ~(TETRIS_STARTED);
                state |= TETRIS_PAUSED;
            }
            break;
        }
        case KEY_VOLUMEUP:
            tetris_exit();
            bootmgr_set_time_thread(1);
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
                    bootmgr_erase_text(16);
                    bootmgr_erase_fill(1);
                    tetris_print_score();
                }
                bootmgr_erase_text(15);
                state = (TETRIS_STARTED | TETRIS_SPAWN_NEW);
            }
            else
            {
                pthread_mutex_lock(tetris_draw_mutex);
                tetris_move_piece(TETRIS_DOWN_FAST);
                tetris_draw(0);
                pthread_mutex_unlock(tetris_draw_mutex);
            }
            break;
        }
        case KEY_BACK:
        {
            if(state & TETRIS_STARTED)
            {
                pthread_mutex_lock(tetris_draw_mutex);
                tetris_rotate_piece();
                tetris_draw(0);
                pthread_mutex_unlock(tetris_draw_mutex);
            }
            break;
        }
        case KEY_MENU:
        case KEY_SEARCH:
        {
            if(state & TETRIS_STARTED)
            {
                pthread_mutex_lock(tetris_draw_mutex);
                if(tetris_can_move_piece(key == KEY_MENU ? TETRIS_LEFT : TETRIS_RIGHT))
                    tetris_move_piece(key == KEY_MENU ? TETRIS_LEFT : TETRIS_RIGHT);
                tetris_draw(0);
                pthread_mutex_unlock(tetris_draw_mutex);
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

void tetris_draw(uint8_t move)
{
    if(move && current)
    {
        pthread_mutex_lock(tetris_draw_mutex);
        if(!tetris_can_move_piece(TETRIS_DOWN))
        {
            if(!current->moved)
            {
                uint8_t lines = 2;
                if(settings.tetris_max_score < score)
                {
                    ++lines;
                    settings.tetris_max_score = score;
                    bootmgr_save_settings();
                    bootmgr_printf(10+((220 - 15*8)/2), 16, WHITE, "New high score!");
                }
                bootmgr_print_fill(11, 14*ISO_CHAR_HEIGHT, 219, lines*ISO_CHAR_HEIGHT, BLACK, 1);
                bootmgr_printf(10+((220 - 9*8)/2),  14, WHITE, "Game over");
                bootmgr_printf(10+((220 - 23*8)/2), 15, WHITE, "Press \"Home\" to restart");
                bootmgr_draw_fills();
                bootmgr_draw_text();
                fb_update(&fb);
                state = TETRIS_FINISHED;
                pthread_mutex_unlock(tetris_draw_mutex);
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
            tetris_move_piece(TETRIS_DOWN);
        pthread_mutex_unlock(tetris_draw_mutex);
    }

    android_memset16(fb.bits, BLACK, BOOTMGR_DIS_W*BOOTMGR_DIS_H*2);

    tetris_print_batt();
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
                android_memset16(bits, tetris_get_color_for_type(itr->type), len*BLOCK_SIZE*2);
                bits += BOOTMGR_DIS_W;
            }
        }
        itr = preview;
        ++i;
    } while(i < 2);

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
                android_memset16(bits, tetris_get_color_for_type(pieces[x][y]->type), BLOCK_SIZE*2);
                bits += BOOTMGR_DIS_W;
            }
        }
    }

    fb_update(&fb);
}

void tetris_spawn_new()
{
    uint8_t type;
    uint8_t rotation;

    if(preview)
        current = preview;
    else
    {
        type = rand()%TETRIS_PIECE_MAX;
        rotation = rand()%4;
        current = (tetris_piece*)malloc(sizeof(tetris_piece));
        current->type = type;
        current->rotation = rotation;
        current->moved = 0;
    }

    uint8_t x,y;
    current->y = 100;
    current->x = 5;
    for(y = 0; y < 5 && current->y == 100; ++y)
        for(x = 0; x < 5 && current->y == 100; ++x)
            if(p_shape[current->type][current->rotation][y][x])
                current->y = 2-y;

    type = rand()%TETRIS_PIECE_MAX;
    rotation = rand()%4;

    preview = (tetris_piece*)malloc(sizeof(tetris_piece));
    preview->type = type;
    preview->rotation = rotation;
    preview->moved = 0;
    preview->x = 13;
    preview->y = 4;
}

uint16_t tetris_get_color_for_type(uint8_t type)
{
    static const uint16_t colors[] =
    {
        ((0x3F << 5) | 0x3F),
        (0x3F),
        ((0x3F << 11) | (0x29 << 5)),
        ((0x3F << 11) | (0x3F << 5)),
        (0x3F << 5),
        ((0x20 << 10) | 0x20),
        (0x3F << 11)
    };
    return colors[type];
}

uint8_t tetris_can_move_piece(uint8_t dir)
{
    if(tetris_check_border(dir) != -1)
        return 0;

    uint8_t y,z;
    switch(dir)
    {
        case TETRIS_DOWN:
        {
            for(y = current->y < 1 ? current->y : 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(p_shape[current->type][current->rotation][y][z] && pieces[current->x+(z-2)][current->y+(y-1)])
                        return 0;
            break;
        }
        case TETRIS_LEFT:
        {
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(p_shape[current->type][current->rotation][y][z] && pieces[current->x+(z-3)][current->y+(y-2)])
                        return 0;
            break;
        }
        case TETRIS_RIGHT:
        {
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(p_shape[current->type][current->rotation][y][z] && pieces[current->x+(z-1)][current->y+(y-2)])
                        return 0;
            break;
        }
    }
    return 1;
}

// Check if there is border in way. returns -1 if not, pos(0-4) of *dir*-most block if yes
int8_t tetris_check_border(uint8_t dir)
{
    switch(dir)
    {
        case TETRIS_DOWN:
        {
            int8_t bottom_off = -1;
            uint8_t y,z;
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(bottom_off < y && p_shape[current->type][current->rotation][y][z])
                            bottom_off = y;
            if(!(current->y+(bottom_off-2) < TETRIS_H-1))
                return bottom_off;
            break;
        }
        case TETRIS_LEFT:
        {
            int8_t left_off = 100;
            uint8_t y,z;
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(left_off > z && p_shape[current->type][current->rotation][y][z])
                        left_off = z;

            if(!(current->x+(left_off-2) > 0))
                return left_off;
            break;
        }
        case TETRIS_RIGHT:
        {
            int8_t right_off = -1;
            uint8_t y,z;
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(right_off < z && p_shape[current->type][current->rotation][y][z])
                        right_off = z;

            if(!(current->x+(right_off-2) < TETRIS_W-1))
                return right_off;
            break;
        }
        case TETRIS_UP:
        {
            int8_t up_off = 100;
            uint8_t y,z;
            for(y = 0; y < PIECE_BLOCKS; ++y)
                for(z = 0; z < PIECE_BLOCKS; ++z)
                    if(y < up_off && p_shape[current->type][current->rotation][y][z])
                        up_off = y;

            if(current->y+(up_off-2) < 0)
                return up_off;
            break;
        }
    }
    return -1;
}

void tetris_move_piece( uint8_t dir)
{
    switch(dir)
    {
        case TETRIS_DOWN:  ++current->y; break;
        case TETRIS_LEFT:  --current->x; break;
        case TETRIS_RIGHT: ++current->x; break;
        case TETRIS_DOWN_FAST:
        {
            while(tetris_can_move_piece(TETRIS_DOWN))
                ++current->y;
            break;
        }
    }
    current->moved = 1;
}

void tetris_rotate_piece()
{
    uint8_t rot = current->rotation;
    uint8_t x_o = current->x;
    uint8_t y_o = current->y;
    ++current->rotation;
    if(current->rotation == 4)
        current->rotation = 0;

    // Check for border
    if(current->x < 2)
    {
        int8_t res = tetris_check_border(TETRIS_LEFT);
        if(res != -1)
            current->x = (2-res);
    }
    else if(current->x >= TETRIS_W-2)
    {
        int8_t res = tetris_check_border(TETRIS_RIGHT);
        if(res != -1)
            current->x = TETRIS_W - (res-2) - 1;
    }

    if(current->y < 2)
    {
        int8_t res = tetris_check_border(TETRIS_UP);
        if(res != -1)
            current->y = 2 - res;
    }
    else if(current->y > TETRIS_H-2)
    {
        int8_t res = tetris_check_border(TETRIS_DOWN);
        if(res != -1)
            current->y = TETRIS_H - (res - 2);
    }

    // Check for other pieces
    uint8_t x,y;
    for(x = 0; x < PIECE_BLOCKS; ++x)
    {
        for(y = 0; y < PIECE_BLOCKS; ++y)
        {
            if(p_shape[current->type][current->rotation][y][x] && pieces[current->x+(x-2)][current->y+(y-2)])
            {
                current->rotation = rot;
                current->x = x_o;
                current->y = y_o;
                return;
            }
        }
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
        score += level*score_coef[lines] + score_coef[lines];
        cleared += lines;
        if(cleared >= (level+1)*LINES_LEVEL)
        {
            ++level;
            update_t -= update_t*0.2;
        }
        tetris_print_score();
    }
}

void tetris_print_score()
{
    bootmgr_printf(10, 0, WHITE, "Score: %5u   Level: %2u Max: %u", score, level, settings.tetris_max_score);
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

void tetris_print_batt()
{
    char pct[5];
    bootmgr_get_file(battery_pct, &pct, 4);
    char *n = strchr(&pct, '\n');
    *n = NULL;
    bootmgr_printf(234, 28, WHITE, "Batt: %s%%", &pct);
}

