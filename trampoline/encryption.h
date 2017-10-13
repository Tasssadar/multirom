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

#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#define ENC_RES_ERR -1
#define ENC_RES_OK 0
#define ENC_RES_BOOT_INTERNAL 1
#define ENC_RES_BOOT_RECOVERY 2

#ifdef MR_ENCRYPTION
int encryption_before_mount(struct fstab *fstab);
void encryption_destroy(void);
int encryption_cleanup(void);
#else
int encryption_before_mount(struct fstab *fstab) { return ENC_RES_OK; }
void encryption_destroy(void) { }
int encryption_cleanup(void) { return 0; }
#endif

#endif
