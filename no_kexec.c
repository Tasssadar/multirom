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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/klog.h>
#include <unistd.h>

// clone libbootimg to /system/extras/ from
// https://github.com/Tasssadar/libbootimg.git
#include <libbootimg.h>

#if LIBBOOTIMG_VERSION  < 0x000200
#error "libbootimg version 0.2.0 or higher is required. Please update libbootimg."
#endif

#include "lib/fstab.h"
#include "lib/inject.h"
#include "lib/log.h"
#include "lib/util.h"

#include "multirom.h"
#include "version.h"

#include "no_kexec.h"


struct struct_nokexec nokexec_s;

struct struct_nokexec * nokexec(void)
{
    return &nokexec_s;
}

// functions used by workaround
char *nokexec_find_boot_mmcblk_path(struct multirom_status *s)
{
    struct fstab_part *boot = NULL;

    INFO(NO_KEXEC_LOG_TEXT ": locating boot partition...\n");

    if (!s)
    {
        INFO(NO_KEXEC_LOG_TEXT ": 'multirom_status' not set up yet, fstab_auto_load ourselves\n");

        struct fstab *fstab;
        fstab = fstab_auto_load();
        if (fstab)
        {
            boot = fstab_find_first_by_path(fstab, "/boot");
            fstab_destroy(fstab);
        }
    }
    else
        if (s->fstab)
            boot = fstab_find_first_by_path(s->fstab, "/boot");

    if (boot)
        INFO(NO_KEXEC_LOG_TEXT ": found boot at '%s'\n", boot->device);
    else
    {
        INFO(NO_KEXEC_LOG_TEXT ": not found in fstab, try looking at mrom.fstab...\n");

        struct fstab *mrom_fstab;
        char path_mrom_fstab[256];

        sprintf(path_mrom_fstab, "%s/%s", mrom_dir(), "mrom.fstab");
        mrom_fstab = fstab_load(path_mrom_fstab, 1);
        if (!mrom_fstab)
        {
            ERROR(NO_KEXEC_LOG_TEXT ": couldn't load mrom.fstab '%s'\n", path_mrom_fstab);
            return NULL;
        }

        boot = fstab_find_first_by_path(mrom_fstab, "/boot");
        if (boot)
            INFO(NO_KEXEC_LOG_TEXT ": found boot (using mrom.fstab) at '%s'\n", boot->device);
        else
            return NULL;
    }
    return strdup(boot->device);
}

int nokexec_set_struct(struct multirom_status *s)
{
    char path[256];
    int has_kexec;

    memset(&nokexec_s, 0, sizeof(struct struct_nokexec));

    // primary boot.img backup
    sprintf(path, "%s/%s", mrom_dir(), "primary_boot.img");
    nokexec_s.path_primary_bootimg = strdup(path);
    INFO(NO_KEXEC_LOG_TEXT ": primary_boot.img location will be=%s\n", nokexec_s.path_primary_bootimg);

    // find boot mmcblk
    nokexec_s.path_boot_mmcblk = nokexec_find_boot_mmcblk_path(s);

    // set flags
    nokexec_s.is_allowed     = s->no_kexec & NO_KEXEC_ALLOWED;
    nokexec_s.is_ask_confirm = s->no_kexec & NO_KEXEC_CONFIRM;
    nokexec_s.is_ask_choice  = s->no_kexec & NO_KEXEC_CHOICE;
    nokexec_s.is_forced      = s->no_kexec & NO_KEXEC_FORCED;

    nokexec_s.is_disabled    = !(nokexec_s.is_allowed || nokexec_s.is_ask_confirm || nokexec_s.is_ask_choice || nokexec_s.is_forced);

    has_kexec = multirom_has_kexec();

    if (!has_kexec)
        nokexec_s.selected_method = NO_KEXEC_BOOT_NOKEXEC;
    else if (nokexec_s.is_forced)
        nokexec_s.selected_method = NO_KEXEC_BOOT_NOKEXEC;
    else
        nokexec_s.selected_method = NO_KEXEC_BOOT_NORMAL;


    // not implemented
    nokexec_s.is_allow_kexec_primary = s->no_kexec & NO_KEXEC_PRIMARY;
    nokexec_s.is_always_restore_primary = s->no_kexec & NO_KEXEC_RESTORE;

    return 0;
}

void nokexec_free_struct(void)
{
    if (nokexec_s.path_primary_bootimg) free(nokexec_s.path_primary_bootimg);
    if (nokexec_s.path_boot_mmcblk) free(nokexec_s.path_boot_mmcblk);
}

int nokexec_cleanup(void)
{
    int res = 0;

    if (access(nokexec_s.path_primary_bootimg, R_OK) == 0)
        INFO(NO_KEXEC_LOG_TEXT ": deleting primary boot.img, the backup is no longer needed; res=%d\n", res |= remove(nokexec_s.path_primary_bootimg));

    return res;
}

int nokexec_set_secondary_flag(void)
{
    // echo -ne "\x71" | dd of=/dev/nk bs=1 seek=63 count=1 conv=notrunc
    int res = -1;
    struct bootimg img;

    // make note that the primary slot now contains a secondary boot.img
    // by tagging the BOOT_NAME at the very end, even after a null terminated "tr_verNN" string
    INFO(NO_KEXEC_LOG_TEXT ": Going to tag the bootimg in primary slot as a secondary\n");

    if (libbootimg_init_load(&img, nokexec_s.path_boot_mmcblk, LIBBOOTIMG_LOAD_ALL) < 0)
    {
        ERROR(NO_KEXEC_LOG_TEXT ": Could not open boot image (%s)!\n", nokexec_s.path_boot_mmcblk);
        return -1;
    }

    // Update the boot.img
    img.hdr.name[BOOT_NAME_SIZE-1] = 0x71;

    INFO(NO_KEXEC_LOG_TEXT ": Writing boot.img updated with secondary flag set\n");
    if (libbootimg_write_img(&img, nokexec_s.path_boot_mmcblk) < 0)
    {
        ERROR("Failed to libbootimg_write_img!\n");
    }
    else
        res = 0;

    libbootimg_destroy(&img);
    return res;
}

int nokexec_backup_primary(void)
{
    int res;

    INFO(NO_KEXEC_LOG_TEXT ": backing up primary boot.img; res=%d\n", res = copy_file(nokexec_s.path_boot_mmcblk, nokexec_s.path_primary_bootimg));

    return res;
}

int nokexec_flash_to_primary(const char * source)
{
    int res = 0;

    // check trampoline no_kexec version and update if needed
    struct boot_img_hdr hdr;

    if (libbootimg_load_header(&hdr, source) < 0)
    {
        ERROR(NO_KEXEC_LOG_TEXT ": Could not open boot image (%s)!\n", nokexec_s.path_boot_mmcblk);
        res = -1;
    }
    else
    {
        int trampoline_ver = 0;
        if(strncmp((char*)hdr.name, "tr_ver", 6) == 0)
            trampoline_ver = atoi((char*)hdr.name + 6);

        if ((trampoline_ver != VERSION_TRAMPOLINE) || (hdr.name[BOOT_NAME_SIZE-2] != VERSION_NO_KEXEC))
        {
            // Trampolines in ROM boot images may get out of sync, so we need to check it and
            // update if needed. I can't do that during ZIP installation because of USB drives.
            if(inject_bootimg(source, 1) < 0)
            {
                ERROR(NO_KEXEC_LOG_TEXT ": Failed to inject bootimg!\n");
                res = -1;
            }
            else
                INFO(NO_KEXEC_LOG_TEXT ": Injected bootimg\n");
        }
    }

    if (res == 0)
        INFO(NO_KEXEC_LOG_TEXT ": flashing '%s' to boot partition; res=%d\n", source, res = copy_file(source, nokexec_s.path_boot_mmcblk));

    return res;
}

int nokexec_is_secondary_in_primary(const char *path_boot_mmcblk)
{
    int res = 0;
    struct boot_img_hdr hdr;

    if (libbootimg_load_header(&hdr, path_boot_mmcblk) < 0)
    {
        ERROR(NO_KEXEC_LOG_TEXT ": Could not open boot image (%s)!\n", path_boot_mmcblk);
        res = -1;
    }
    else
    {
        if (hdr.name[BOOT_NAME_SIZE-1] == 0x71)
            res = 1;
    }
    INFO(NO_KEXEC_LOG_TEXT ": Checking the primary slot bootimg for the secondary tag; res=%d\n", res);

    return res;
}

int nokexec_is_new_primary(void)
{
    int is_new = 0;

    // check if it even exists, otherwise it's "new" (1)
    if (access(nokexec_s.path_primary_bootimg, R_OK) != 0)
        is_new = 1;

    // if the current bootimg is not tagged, then it's new (2)
    else if (!nokexec_is_secondary_in_primary(nokexec_s.path_boot_mmcblk))
        is_new = 2;

    INFO(NO_KEXEC_LOG_TEXT ": Checking if primary is new; is_new=%d\n", is_new);

    return is_new;
}


// this has to be a self dependant function because it get's called before multirom_load_status
// so neither 'multirom_status' nor 'nokexec_s' are set up yet
// if primary slot contains a secondary boot.img then we're in second_boot
int nokexec_is_second_boot(void)
{
    static int is_second_boot = -1;
    if(is_second_boot != -1) {
        INFO(NO_KEXEC_LOG_TEXT ": returning cached info is_second_boot=%d\n", is_second_boot);
        return is_second_boot;
    }

    int res;
    INFO(NO_KEXEC_LOG_TEXT ": Checking primary slot...\n");
    char * path_boot_mmcblk = nokexec_find_boot_mmcblk_path(NULL);
    if (!path_boot_mmcblk)
        return -1;

    res = nokexec_is_secondary_in_primary(path_boot_mmcblk);
    free(path_boot_mmcblk);

    if (res < 0)
        return -1;
    else if (res == 1)
    {
        // it's a secondary boot.img so set second_boot=1
        INFO(NO_KEXEC_LOG_TEXT ":    secondary bootimg, so second_boot=1\n");
        is_second_boot = 1;
    }
    else
    {
        INFO(NO_KEXEC_LOG_TEXT ":    primary bootimg, so second_boot=0\n");
        is_second_boot = 0;
    }

    return is_second_boot;
}


int nokexec_flash_secondary_bootimg(struct multirom_rom *secondary_rom)
{
    // make sure all the paths are set up, otherwise abort
    if (!nokexec_s.path_boot_mmcblk || !nokexec_s.path_primary_bootimg)
        return -1;

    if (nokexec_is_new_primary())
        if (nokexec_backup_primary())
            return -2;

    // now flash the secondary boot.img to primary slot
    char path_bootimg[256];
    sprintf(path_bootimg, "%s/%s", secondary_rom->base_path, "boot.img");
    if (nokexec_flash_to_primary(path_bootimg))
        return -3;

    // make note that the primary slot now contains a secondary boot.img
    if (nokexec_set_secondary_flag())
        return -4;

    return 0;
}

int nokexec_restore_primary_and_cleanup(void)
{
    // make sure all the paths are set up, otherwise abort
    if (!nokexec_s.path_boot_mmcblk || !nokexec_s.path_primary_bootimg)
        return -1;

    // only restore if a secondary is in primary slot
    if (nokexec_is_secondary_in_primary(nokexec_s.path_boot_mmcblk) != 0)
    {
        // check if primary_boot.img exits , if it does then restore it
        if (access(nokexec_s.path_primary_bootimg, R_OK) != 0)
        {
            ERROR(NO_KEXEC_LOG_TEXT ": no primary boot.img was found, so cannot restore it\n");
            // NOTE: theoretically we could try booting into primary, even with a secondary
            //       boot.img installed, if it's compatible it will boot, but we're not going to
            return -2;
        }
        else
        {
            if (nokexec_flash_to_primary(nokexec_s.path_primary_bootimg))
                return -3;
        }
    }

    // remove no longer needed files, in case of error, just log it
    if(nokexec_cleanup())
        ERROR(NO_KEXEC_LOG_TEXT ": WARNING: error in no_kexec_cleanup\n");

    return 0;
}
