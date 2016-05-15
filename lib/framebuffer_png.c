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
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <png.h>

#include "log.h"
#include "framebuffer.h"
#include "util.h"
#include "containers.h"

#if 0
#define PNG_LOG(x...) INFO(x)
#else
#define PNG_LOG(x...) ;
#endif

struct png_cache_entry
{
    char *path;
    px_type *data;
    int width;
    int height;
    int refcnt;
};

static struct png_cache_entry **png_cache = NULL;

// http://willperone.net/Code/codescaling.php
static px_type *scale_png_img(px_type *fi_data, int orig_w, int orig_h, int new_w, int new_h)
{
    if(orig_w == new_w && orig_h == new_h)
        return fi_data;

    uint32_t *in = (uint32_t*)fi_data;
#if PIXEL_SIZE == 2
    // need another byte for alpha. Make it 4 to make it simpler
    uint32_t *out = malloc(4 * new_w * new_h);
#else
    uint32_t *out = malloc(PIXEL_SIZE * new_w * new_h);
#endif

    const int YD = (orig_h / new_h) * orig_w - orig_w;
    const int YR = orig_h % new_h;
    const int XD = orig_w / new_w;
    const int XR = orig_w % new_w;
    int in_off = 0, out_off = 0;
    int x, y, YE, XE;

    for(y = new_h, YE = 0; y > 0; --y)
    {
        for(x = new_w, XE = 0; x > 0; --x)
        {
            out[out_off++] = in[in_off];
            in_off += XD;
            XE += XR;
            if(XE >= new_w)
            {
                XE -= new_w;
                ++in_off;
            }
        }
        in_off += YD;
        YE += YR;
        if(YE >= new_h)
        {
            YE -= new_h;
            in_off += orig_w;
        }
    }

    free(fi_data);
    return (px_type*)out;
}

static px_type *load_png(const char *path, int destW, int destH)
{
    FILE *fp;
    unsigned char header[8];
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    uint32_t bytes_per_row;
    px_type *data_dest = NULL, *data_itr;
    size_t i, y;
    int si, alpha;
    int px_per_row;
    uint32_t src_pix;
    png_bytep *rows = NULL;

    fp = fopen(path, "rbe");
    if(!fp)
        return NULL;

    size_t bytesRead = fread(header, 1, sizeof(header), fp);
    if (bytesRead != sizeof(header)) {
        goto exit;
    }

    if (png_sig_cmp(header, 0, sizeof(header))) {
        goto exit;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        goto exit;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        goto exit;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        goto exit;
    }

    png_set_packing(png_ptr);

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(header));
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    size_t stride, pixelSize;
    int color_type, bit_depth, channels;

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
            NULL, NULL, NULL);

    channels = png_get_channels(png_ptr, info_ptr);
    stride = 4 * width;
    pixelSize = stride * height;

    if (!(bit_depth == 8 &&
          ((channels == 3 && color_type == PNG_COLOR_TYPE_RGB) ||
           (channels == 4 && color_type == PNG_COLOR_TYPE_RGBA)))) {
        goto exit;
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

#if PIXEL_SIZE == 2
    // need another byte for alpha. Make it 4 to make it simpler
    data_dest = malloc(4 * width * height);
#else
    data_dest = malloc(PIXEL_SIZE * width * height);
#endif
    data_itr = data_dest;

    bytes_per_row = png_get_rowbytes(png_ptr, info_ptr);
    rows = malloc(sizeof(png_bytep)*height);
    for(y = 0; y < height; ++y)
        rows[y] = malloc(bytes_per_row);
    png_read_image(png_ptr, rows);

    for(y = 0; y < height; ++y)
    {
        for(i = 0, si = 0; i < width; ++i)
        {
            if(channels == 4)
            {
                src_pix = ((uint32_t*)rows[y])[i];
                src_pix = (src_pix & 0xFF00FF00) | ((src_pix & 0xFF0000) >> 16) | ((src_pix & 0xFF) << 16);
            }
            else //if(channels == 3) - no other option
            {
                src_pix = (rows[y][si++] << 16);  // R
                src_pix |= (rows[y][si++] << 8);  // G
                src_pix |= (rows[y][si++]);       // B
                src_pix |= 0xFF000000;             // A
            }

            *data_itr = (px_type)fb_convert_color(src_pix);
            ++data_itr;
#if PIXEL_SIZE == 2
            // Store alpha value for 5 and 6 bit values in next two bytes
            alpha = ((src_pix & 0xFF000000) >> 24);
            ((uint8_t*)data_itr)[0] = ((((alpha*100)/0xFF)*31)/100);
            ((uint8_t*)data_itr)[1] = ((((alpha*100)/0xFF)*63)/100);
            ++data_itr;
#endif
        }
        free(rows[y]);
    }
    free(rows);

    data_dest = scale_png_img(data_dest, width, height, destW, destH);
exit:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    return data_dest;
}

static void destroy_png_cache_entry(void *entry)
{
    struct png_cache_entry *e = (struct png_cache_entry*)entry;
    free(e->path);
    free(e->data);
    free(e);
}

px_type *fb_png_get(const char *path, int w, int h)
{
    // Try to find it in cache
    struct png_cache_entry **itr;
    for(itr = png_cache; itr && *itr; ++itr)
    {
        if((*itr)->width == w && (*itr)->height == h && strcmp(path, (*itr)->path) == 0)
        {
            ++(*itr)->refcnt;
            PNG_LOG("PNG %s (%dx%d) %p found in cache, refcnt increased to %d\n", path, w, h, (*itr)->data, (*itr)->refcnt);
            return (*itr)->data;
        }
    }

    // not in cache yet, load and create cache entry
    px_type *data = load_png(path, w, h);
    if(!data)
    {
        PNG_LOG("PNG %s (%dx%d) failed to load\n", path, w, h);
        return NULL;
    }
    PNG_LOG("PNG %s (%dx%d) loaded\n", path, w, h);

    struct png_cache_entry *e = mzalloc(sizeof(struct png_cache_entry));
    e->path = strdup(path);
    e->data = data;
    e->width = w;
    e->height = h;
    e->refcnt = 1;

    list_add(&png_cache, e);
    PNG_LOG("PNG %s (%dx%d) %p added into cache\n", path, w, h, data);
    return data;
}

void fb_png_release(px_type *data)
{
    struct png_cache_entry **itr;
    for(itr = png_cache; itr && *itr; ++itr)
    {
        if((*itr)->data == data)
        {
            --(*itr)->refcnt;
            PNG_LOG("PNG %s (%dx%d) %p released, refcnt is %d\n", (*itr)->path, (*itr)->width, (*itr)->height, data, (*itr)->refcnt);
            return;
        }
    }
    PNG_LOG("PNG %p not found in cache!\n", data);
}

void fb_png_drop_unused(void)
{
    struct png_cache_entry **itr;
    for(itr = png_cache; itr && *itr;)
    {
        if((*itr)->refcnt <= 0)
        {
            PNG_LOG("PNG %s (%dx%d) %p removed from cache\n", (*itr)->path, (*itr)->width, (*itr)->height, data);
            list_rm(&png_cache, *itr, &destroy_png_cache_entry);
            itr = png_cache;
        }
        else
        {
            ++itr;
        }
    }
}

static inline void convert_fb_px_to_rgb888(px_type src, uint8_t *dest)
{
    dest[0] = PX_GET_R(src);
    dest[1] = PX_GET_G(src);
    dest[2] = PX_GET_B(src);
}

int fb_png_save_img(const char *path, int w, int h, int stride, px_type *data)
{
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    int res = -1;
    int y, x;
    uint8_t *row = NULL;
    const int stride_leftover = stride - w;
    const int w_png_bytes = w * 3;
    px_type * volatile itr = data;

    fp = fopen(path, "we");
    if(!fp)
    {
        ERROR("Failed to open %s for writing\n", path);
        return -1;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        goto exit;

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
        goto exit;

    if (setjmp(png_jmpbuf(png_ptr)))
        goto exit;

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, w, h,
         8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
         PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);

    row = malloc(w*3);
    for(y = 0; y < h; ++y)
    {
        for(x = 0; x < w_png_bytes; x += 3)
        {
            convert_fb_px_to_rgb888(*itr, row + x);
            ++itr;
        }
        itr += stride_leftover;

        png_write_row(png_ptr, row);
    }

    png_write_end(png_ptr, NULL);
    res = 0;
exit:
    if(info_ptr)
        png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if(png_ptr)
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if(fp)
        fclose(fp);
    if(row)
        free(row);
    return res;
}
