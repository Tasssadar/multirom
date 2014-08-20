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
#include <stdlib.h>
#include <stdio.h>

#include "../log.h"
#include "../util.h"
#include "../fstab.h"
#include "fw_mounter_defines.h"

int main(int argc, char *argv[])
{
    struct fstab *f = fstab_load(FW_MOUNTER_FSTAB, 0);
    if(!f)
    {
        ERROR("Failed to load %s\n", FW_MOUNTER_FSTAB);
        return -1;
    }

    struct fstab_part *fw_part = fstab_find_by_path(f, "/firmware");
    if(!fw_part)
    {
        ERROR("Unable to find partition /firmware in %s!\n", FW_MOUNTER_FSTAB);
        return -1;
    }

    ERROR("Mounting %s to %s\n", fw_part->device, fw_part->path);
    return mount_image(fw_part->device, fw_part->path, fw_part->type, fw_part->mountflags, fw_part->options);
}
