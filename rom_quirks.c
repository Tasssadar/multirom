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

#include <unistd.h>
#include <stdio.h>

#include "multirom.h"
#include "rom_quirks.h"
#include "log.h"

void rom_quirks_on_android_mounted_fs(struct multirom_rom *rom)
{
    // CyanogenMod has init script 50selinuxrelabel which calls
    // restorecon on /data. On secondary ROMs, /system is placed
    // inside /data/media/ and mount-binded to /system, so restorecon
    // sets contexts to files in /system as if they were in /data.
    // This behaviour is there mainly because of old recoveries which
    // didn't set contexts properly, so it should be safe to remove
    // that file entirely.
    if(rom->type != ROM_ANDROID_USB_IMG && access("/system/etc/init.d/50selinuxrelabel", F_OK) >= 0)
    {
        // run-parts won't run files which don't have +x
        INFO("Removing /system/etc/init.d/50selinuxrelabel.\n");
        remove("/system/etc/init.d/50selinuxrelabel");
    }
}
