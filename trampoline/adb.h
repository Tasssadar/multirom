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

#ifndef ADB_H
#define ADB_H

void adb_init(char *mrom_path);
void adb_quit(void);
void adb_init_usb(void);
int adb_init_busybox(void);
void adb_init_fs(void);
void adb_cleanup(void);
int adb_get_serial(char *serial, int maxlen);
int adb_is_enabled(char *mrom_path);

#endif
