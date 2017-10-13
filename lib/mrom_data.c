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

#ifdef MR_NO_KEXEC
#include "../no_kexec.h"
#endif

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

int mrom_is_second_boot(void)
{
    int i;
    int res = 0;
    FILE *f = NULL;
    char buff[2048];

    static const char *kmsg_paths[] = {
        "/proc/last_kmsg",
        "/sys/fs/pstore/console-ramoops",
        NULL,
    };

    f = fopen("/proc/cmdline", "re");
    if(f)
    {
        if(fgets(buff, sizeof(buff), f) && strstr(buff, "mrom_kexecd=1"))
        {
            res = 1;
            goto exit;
        }

        fclose(f);
        f = NULL;
    }

    for(i = 0; !f && kmsg_paths[i]; ++i)
        f = fopen(kmsg_paths[i], "re");

    if(!f)
#ifndef MR_NO_KEXEC
        return 0;
#else
        goto check_no_kexec;
#endif

    while(fgets(buff, sizeof(buff), f))
    {
        if(strstr(buff, SECOND_BOOT_KMESG))
        {
            res = 1;
            goto exit;
        }
    }

exit:
    fclose(f);

#ifdef MR_NO_KEXEC
check_no_kexec:
    if (res == 0) {
        res = nokexec_is_second_boot();
        if (res < 0)
            res = 0;
    }
#endif
    return res;
}
