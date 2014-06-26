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
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "log.h"
#include "framebuffer.h"
#include "util.h"
#include "containers.h"

struct cache_entry
{
    FT_Face *face;
    imap *glyphs;
    int refcnt;
};

struct glyph_cache
{
    imap *cache;
    FT_Library *library;
};

static int convert_ft_bitmap(FT_BitmapGlyph bit, px_type color, px_type *res_data, int w, int baseline_y, FT_Vector *pos)
{
    int x, y;
    uint8_t *buff;
    px_type *res_itr;

    if(bit->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
        ERROR("Unsupported pixel mode in FT_BitmapGlyph %d\n", bit->bitmap.pixel_mode);
        return -1;
    }

    INFO("Bitmap w %d baseline %d pos [%d; %d] left %d top %d rows %d cols %d pitch %d\n", w, baseline_y, pos->x, pos->y, bit->left, bit->top, bit->bitmap.rows, bit->bitmap.width, bit->bitmap.pitch);

    buff = (uint8_t*)bit->bitmap.buffer;

    res_itr = (px_type*)(((uint32_t*)res_data) + (baseline_y - bit->top)*w + (pos->x + bit->left));
    for(y = 0; y < bit->bitmap.rows; ++y)
    {
        for(x = 0; x < bit->bitmap.width; ++x)
        {
#if PIXEL_SIZE == 4
            *res_itr++ = color | (buff[x] << ((PX_IDX_A*8)));
#else
            *res_itr++ = color;
            ((uint8_t*)res_itr)[0] = ((((buff[x]*100)/0xFF)*31)/100);
            ((uint8_t*)res_itr)[1] = ((((buff[x]*100)/0xFF)*63)/100);
            ++res_itr;
#endif
        }
        buff += bit->bitmap.pitch;
        res_itr = (px_type*)(((uint32_t*)res_itr) + w - bit->bitmap.width);
    }
    return 0;
}

fb_img *fb_add_text_long(int x, int y, uint32_t color, int size, const char *text)
{
    const int text_len = strlen(text);





    int error, penX, penY, use_kerning, i, w, h, len;
    FT_UInt prev_idx, idx;
    FT_Library library;
    FT_Face face;
    FT_GlyphSlot slot;
    FT_Glyph *glyphs;
    FT_Vector *pos;
    FT_Glyph image;
    FT_BitmapGlyph bit;
    FT_Vector pen, delta;
    px_type *res_data;
    px_type converted_color;

    error = FT_Init_FreeType( &library );
    if(error)
    {
        ERROR("libtruetype init failed with %d\n", error);
        return NULL;
    }

    error = FT_New_Face(library, "/realdata/media/0/multirom/Roboto-Regular.ttf", 0, &face);
    if(error)
    {
        ERROR("font load failed with %d\n", error);
        return NULL;
    }

    error = FT_Set_Pixel_Sizes(face, 0, size);
    if(error)
    {
        ERROR("failed to set font size with %d\n", error);
        return NULL;
    }

    slot = face->glyph;
    penX = 0;
    penY = 0;
    use_kerning = FT_HAS_KERNING(face);
    prev_idx = 0;
    w = h = 0;

    len = strlen(text);
    glyphs = malloc(sizeof(FT_Glyph)*len);
    pos = malloc(sizeof(FT_Vector)*len);

    // Load glyphs and their positions
    for(i = 0; i < len; ++i)
    {
        idx = FT_Get_Char_Index(face, text[i]);

        if(use_kerning && prev_idx && idx)
        {
            error = FT_Get_Kerning(face, prev_idx, idx, FT_KERNING_DEFAULT, &delta);
            penX += delta.x >> 6;
        }

        error = FT_Load_Glyph(face, idx, FT_LOAD_RENDER);
        if(error)
            continue;

        pos[i].x = penX;
        pos[i].y = penY;

        error = FT_Get_Glyph(face->glyph, &glyphs[i]);
        if(error)
            continue;

        penX += slot->advance.x >> 6;
        prev_idx = idx;
    }

    // do the last kerning
    if(use_kerning && prev_idx && idx)
    {
        FT_Vector delta;
        FT_Get_Kerning(face, prev_idx, idx, FT_KERNING_DEFAULT, &delta);
        penX += delta.x >> 6;
    }

    // calculate bounding box
    FT_BBox bbox, glyph_bbox;
    bbox.xMin = bbox.yMin = 32000;
    bbox.xMax = bbox.yMax = -32000;

    for(i = 0; i < len; ++i)
    {
        FT_Glyph_Get_CBox(glyphs[i], ft_glyph_bbox_pixels, &glyph_bbox);

        bbox.xMin = imin(bbox.xMin, glyph_bbox.xMin + pos[i].x);
        bbox.yMin = imin(bbox.yMin, glyph_bbox.yMin + pos[i].y);
        bbox.xMax = imax(bbox.xMax, glyph_bbox.xMax + pos[i].x);
        bbox.yMax = imax(bbox.yMax, glyph_bbox.yMax + pos[i].y);
    }

    if(bbox.xMin > bbox.xMax)
        bbox.xMin = bbox.yMin = bbox.xMax = bbox.yMax = 0;

    // Render text
    INFO("BBOX: X %d %d Y %d %d\n", bbox.xMin, bbox.xMax, bbox.yMin, bbox.yMax);
    w = penX;
    h = bbox.yMax - bbox.yMin;
    res_data = mzalloc(w*h*4); // always 4 bytes per pixel cause of fb_img data structure

    // set color's alpha to 0, because data from the bitmap will act as alpha
    converted_color = fb_convert_color(color & ~(0xFF << 24));
    for(i = 0; i < len; ++i)
    {
        image = glyphs[i];
        error = FT_Glyph_To_Bitmap(&image, FT_RENDER_MODE_NORMAL, &pos[i], 0);
        if(!error)
        {
            bit = (FT_BitmapGlyph)image;

            convert_ft_bitmap(bit, converted_color, res_data, w, bbox.yMax, &pos[i]);

            FT_Done_Glyph(image);
        }
    }

    free(glyphs);
    free(pos);

    FT_Done_FreeType(library);

    return fb_add_img(x, y, w, h, FB_IMG_TYPE_GENERIC, res_data);
}
