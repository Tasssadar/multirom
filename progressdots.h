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

#ifndef PROGRESSDOTS_H
#define PROGRESSDOTS_H

#include <pthread.h>
#include "framebuffer.h"

#define PROGDOTS_W (400*DPI_MUL)
#define PROGDOTS_H (10*DPI_MUL)
#define PROGDOTS_CNT 8

typedef struct
{
	FB_ITEM_POS

    fb_rect *rect;
} progdots;

progdots *progdots_create(int x, int y);
void progdots_destroy(progdots *p);

#endif
