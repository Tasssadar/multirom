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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kexec.h"
#include "containers.h"
#include "log.h"
#include "util.h"

// kexec --load-hardboot ./zImage --command-line="$(cat /proc/cmdline)" --mem-min=0xA0000000 --initrd=./rd.img
// --mem-min should be somewhere in System RAM (see /proc/iomem). Location just above kernel seems to work fine.
// It must not conflict with vmalloc ram. Vmalloc area seems to be allocated from top of System RAM.

void kexec_init(struct kexec *k, const char *path)
{
    k->args = NULL;
    kexec_add_arg(k, path);
}

void kexec_destroy(struct kexec *k)
{
    list_clear(&k->args, &free);
}

int kexec_load_exec(struct kexec *k)
{
    int i, len;

    INFO("Loading kexec:\n");
    for(i = 0; k->args && k->args[i]; ++i)
    {
        len = strlen(k->args[i]);

        if(len < 480)
            INFO("    %s\n", k->args[i]);
        else
        {
            char buff[481];
            char *itr;
            const char *end = k->args[i]+len;
            int chunk = 0;

            for(itr = k->args[i]; itr < end; itr += chunk)
            {
                chunk = imin(480, end - itr);

                memcpy(buff, itr, chunk);
                buff[chunk] = 0;

                INFO("    %s\n", buff);
            }
        }
    }

    if(run_cmd(k->args) == 0)
        return 0;
    else
    {
        ERROR("kexec call failed, re-running it to get info:\n");
        char *r = run_get_stdout(k->args);
        if(!r)
            ERROR("run_get_stdout returned NULL!\n");

        char *p = strtok(r, "\n\r");
        while(p)
        {
            ERROR("  %s\n", p);
            p = strtok(NULL, "\n\r");
        }
        free(r);

        return -1;
    }
}

void kexec_add_arg(struct kexec *k, const char *arg)
{
    list_add(&k->args, strdup(arg));
}

void kexec_add_arg_prefix(struct kexec *k, const char *prefix, const char *value)
{
    int len = strlen(prefix) + strlen(value) + 1;
    char *arg = malloc(len);
    snprintf(arg, len, "%s%s", prefix, value);

    list_add(&k->args, arg);
}

void kexec_add_kernel(struct kexec *k, const char *path, int hardboot)
{
    if(hardboot)
        kexec_add_arg(k, "--load-hardboot");
    else
        kexec_add_arg(k, "-l");
    kexec_add_arg(k, path);
}
