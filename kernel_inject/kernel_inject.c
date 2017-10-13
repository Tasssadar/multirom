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
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "../lib/log.h"
#include "../lib/util.h"

#include <libbootimg.h>

int kernel_inject(const char *img_path, const char *kernel_path)
{
    int res = -1;
    struct bootimg img;

    if (libbootimg_init_load(&img, img_path, LIBBOOTIMG_LOAD_ALL) < 0)
    {
        ERROR("Could not open boot image (%s)!\n", img_path);
        return -1;
    }

    if (libbootimg_load_kernel(&img, kernel_path) < 0)
    {
        ERROR("Failed to load kernel from %s!\n", kernel_path);
        goto exit;
    }

    char tmp[256];
    strcpy(tmp, img_path);
    strcat(tmp, ".new");
    if (libbootimg_write_img(&img, tmp) >= 0)
    {
        INFO("Writing boot.img updated with kernel\n");
        if (copy_file(tmp, img_path) < 0)
            ERROR("Failed to copy %s to %s!\n", tmp, img_path);
        else
            res = 0;
        remove(tmp);
    }
    else
    {
        ERROR("Failed to libbootimg_write_img!\n");
    }

exit:
    libbootimg_destroy(&img);
    return res;
}

int main(int argc, char *argv[])
{
    int i, res;
    static char *const cmd[] = { "/init", NULL };
    char *inject_path = NULL;
    char *kernel = NULL;

    for (i = 1; i < argc; ++i)
    {
        if (strstartswith(argv[i], "--inject="))
        {
            inject_path = argv[i] + strlen("--inject=");
        }
        else if (strstartswith(argv[i], "--kernel="))
        {
            kernel = argv[i] + strlen("--kernel=");
        }
    }

    if (!inject_path || !kernel)
    {
        printf("--inject=[path to bootimage to patch] --kernel=[path to the new kernel] needs to be specified!\n");
        fflush(stdout);
        return 1;
    }

    mrom_set_log_tag("kernel_inject");
    return kernel_inject(inject_path, kernel);
}
