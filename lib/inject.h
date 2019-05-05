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

#ifndef INJECT_H
#define INJECT_H

int inject_bootimg(const char *img_path, int force);
int decompress_second_rd(const char* target, const char *second_path);
int pack_second_rd(const char *second_path, const char* unpacked_dir);
int decompress_rd(const char *path, const char* target, int* type);
int pack_rd(const char *path, const char *target, int type);

#endif
