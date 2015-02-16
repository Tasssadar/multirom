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

#include <string.h>
#include <stdio.h>

#include "mrom_data.h"

static char multirom_dir[128] = { 0 };
static char log_tag[64] = { 0 };

void mrom_set_dir(const char *mrom_dir)
{
    snprintf(multirom_dir, sizeof(multirom_dir), "%s", mrom_dir);
}

void mrom_set_log_tag(const char *tag)
{
    snprintf(log_tag, sizeof(log_tag), "%s", tag);
}

const char *mrom_log_tag(void)
{
    return log_tag;
}

const char *mrom_dir(void)
{
    return multirom_dir;
}
