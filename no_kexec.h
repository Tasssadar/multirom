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

#ifndef NO_KEXEC_H
#define NO_KEXEC_H

#include "multirom.h"

enum
{
    // enabled/disbaled
    NO_KEXEC_DISABLED    =  0x00,   // no-kexec workaround disabled

    NO_KEXEC_ALLOWED     =  0x01,   // "Use no-kexec only when needed"
    NO_KEXEC_CONFIRM     =  0x02,   // "..... but also ask for confirmation"
    NO_KEXEC_CHOICE      =  0x04,   // "Ask whether to kexec or use no-kexec"
    NO_KEXEC_FORCED      =  0x08,   // "Always force using no-kexec workaround"

    // options (unused)
    NO_KEXEC_PRIMARY     =  0x40,   // Allow kexec'ing into primary
    NO_KEXEC_RESTORE     =  0x80    // Always restore primary if a secondary is in primary
};

enum
{
    NO_KEXEC_BOOT_NONE      = 0,
    NO_KEXEC_BOOT_NORMAL    = 1,
    NO_KEXEC_BOOT_NOKEXEC   = 2
};

struct struct_nokexec
{
    int is_disabled;

    int is_allowed;
    int is_ask_confirm;
    int is_ask_choice;
    int is_forced;

    int selected_method;

    int is_allow_kexec_primary;
    int is_always_restore_primary;

    char * path_boot_mmcblk;
    char * path_primary_bootimg;
};


// The below version number only needs to get bumped if there is a trampoline
// related change, ie a change in "nokexec_is_second_boot()" function.
// Do NOT bump it for any other reason, because it will force re-injection
// on secondary ROMs even when not needed.
#define VERSION_NO_KEXEC '\x04'

// This text will be prepended to all INFO and ERROR logs for no_kexec functions
// it also contains a version number for informative purposes.
#define NO_KEXEC_LOG_TEXT "NO_KEXEC(4.1)"


// public functions, the rest are private so not included here on purpose
struct struct_nokexec * nokexec(void);

int nokexec_set_struct(struct multirom_status *s);
void nokexec_free_struct(void);

int nokexec_restore_primary_and_cleanup(void);
int nokexec_flash_secondary_bootimg(struct multirom_rom *secondary_rom);

int nokexec_is_second_boot(void);

#endif
