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

#ifndef APPLICATIONINFO_H
#define APPLICATIONINFO_H

enum {
    FLAG_SYSTEM                      = 0x00000001,
    FLAG_DEBUGGABLE                  = 0x00000002,
    FLAG_HAS_CODE                    = 0x00000004,
    FLAG_PERSISTENT                  = 0x00000008,
    FLAG_FACTORY_TEST                = 0x00000010,
    FLAG_ALLOW_TASK_REPARENTING      = 0x00000020,
    FLAG_ALLOW_CLEAR_USER_DATA       = 0x00000040,
    FLAG_UPDATED_SYSTEM_APP          = 0x00000080,
    FLAG_TEST_ONLY                   = 0x00000100,
    FLAG_SUPPORTS_SMALL_SCREENS      = 0x00000200,
    FLAG_SUPPORTS_NORMAL_SCREENS     = 0x00000400,
    FLAG_SUPPORTS_LARGE_SCREENS      = 0x00000800,
    FLAG_RESIZEABLE_FOR_SCREENS      = 0x00001000,
    FLAG_SUPPORTS_SCREEN_DENSITIES   = 0x00002000,
    FLAG_VM_SAFE_MODE                = 0x00004000,
    FLAG_ALLOW_BACKUP                = 0x00008000,
    FLAG_KILL_AFTER_RESTORE          = 0x00010000,
    FLAG_RESTORE_ANY_VERSION         = 0x00020000,
    FLAG_EXTERNAL_STORAGE            = 0x00040000,
    FLAG_SUPPORTS_XLARGE_SCREENS     = 0x00080000,
    FLAG_LARGE_HEAP                  = 0x00100000,
    FLAG_STOPPED                     = 0x00200000,
    FLAG_SUPPORTS_RTL                = 0x00400000,
    FLAG_INSTALLED                   = 0x00800000,
    FLAG_IS_DATA_ONLY                = 0x01000000,
    FLAG_BLOCKED                     = 0x08000000,
    FLAG_CANT_SAVE_STATE             = 0x10000000,
    FLAG_FORWARD_LOCK                = 0x20000000,
    FLAG_PRIVILEGED                  = 0x40000000
};

#endif
