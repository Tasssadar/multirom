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

#ifndef RCADDITIONS_H
#define RCADDITIONS_H

#include "lib/containers.h"

struct rcadditions
{
    map *triggers;
    char *eof_append;
    char *file_contexts_append;
};

void rcadditions_append_trigger(struct rcadditions *r, const char *trigger, const char *what);
void rcadditions_append_file(struct rcadditions *r, const char *what);
void rcadditions_append_contexts(struct rcadditions *r, const char *what);
void rcadditions_free(struct rcadditions *r);
void rcadditions_write_to_files(struct rcadditions *r);

#endif
