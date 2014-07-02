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
#include "multirom.h"

#define FONT_FILE "Roboto-Regular.ttf"
#define LINE_SPACING 1.1

#if 0
#define TT_LOG(fmt, x...) INFO("TT: "fmt, ##x)
#else
#define TT_LOG(x...) ;
#endif

struct glyph_cache_entry
{
    FT_Face face;
    imap *glyphs;
    int refcnt;
};

struct string_cache_entry
{
    px_type *data;
    int w, h;
    int baseline;
    int refcnt;
    px_type color;
};

struct text_cache
{
    imap *glyph_cache;
    FT_Library ft_lib;
    imap *string_cache;
};

static struct text_cache cache = { NULL, NULL, NULL };

struct text_line
{
    const char *text; // doesn't end with \0
    int len;
    int w, h, base;
    int offX, offY;
    FT_Vector *pos;
};

static int convert_ft_bitmap(FT_BitmapGlyph bit, px_type color, px_type *res_data, int stride, struct text_line *line, FT_Vector *pos)
{
    int x, y;
    uint8_t *buff;
    px_type *res_itr;

    if(bit->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
        ERROR("Unsupported pixel mode in FT_BitmapGlyph %d\n", bit->bitmap.pixel_mode);
        return -1;
    }

    //INFO("Bitmap w %d baseline %d pos [%d; %d] left %d top %d rows %d cols %d pitch %d\n", line->w, line->base, pos->x, pos->y, bit->left, bit->top, bit->bitmap.rows, bit->bitmap.width, bit->bitmap.pitch);

    buff = (uint8_t*)bit->bitmap.buffer;
    res_itr = (px_type*)(((uint32_t*)res_data) + (line->offY + line->base - bit->top)*stride + (line->offX + pos->x + bit->left));
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
        res_itr = (px_type*)(((uint32_t*)res_itr) + stride - bit->bitmap.width);
    }
    return 0;
}

static struct glyph_cache_entry *get_cache_for_size(const int size)
{
    int error;
    struct glyph_cache_entry *res;

    if(!cache.ft_lib)
    {
        error = FT_Init_FreeType(&cache.ft_lib);
        if(error)
        {
            ERROR("libtruetype init failed with %d\n", error);
            return NULL;
        }
    }

    if(!cache.glyph_cache)
        cache.glyph_cache = imap_create();

    res = imap_get_val(cache.glyph_cache, size);
    if(!res)
    {
        char buff[128];
        res = mzalloc(sizeof(struct glyph_cache_entry));
        snprintf(buff, sizeof(buff), "%s/"FONT_FILE, multirom_dir);
        error = FT_New_Face(cache.ft_lib, buff, 0, &res->face);
        if(error)
        {
            ERROR("font load failed with %d\n", error);
            free(res);
            return NULL;
        }

        error = FT_Set_Char_Size(res->face, 0, size*64, MR_DPI_FONT, MR_DPI_FONT);
        if(error)
        {
            ERROR("failed to set font size with %d\n", error);
            FT_Done_Face(res->face);
            free(res);
            return NULL;
        }

        res->glyphs = imap_create();
        imap_add_not_exist(cache.glyph_cache, size, res);
    }

    return res;
}

static struct string_cache_entry *get_cache_for_string(px_type color, int size, const char *text)
{
    if(!cache.string_cache)
        return NULL;

    map *c = imap_get_val(cache.string_cache, size);
    if(!c)
        return NULL;

    struct string_cache_entry *sen = map_get_val(c, text);
    if(sen && sen->color == color)
        return sen;
    return NULL;
}

static void add_to_string_cache(px_type color, int baseline, int size, const char *text, int w, int h, px_type *data)
{
    if(!cache.string_cache)
        cache.string_cache = imap_create();

    map *c = imap_get_val(cache.string_cache, size);
    if(!c)
    {
        c = map_create();
        imap_add_not_exist(cache.string_cache, size, c);
    }
    else if(map_find(c, text) != -1)
    {
        return;
    }

    struct string_cache_entry *sen = mzalloc(sizeof(struct string_cache_entry));
    sen->data = data;
    sen->refcnt = 1;
    sen->w = w;
    sen->h = h;
    sen->color = color;
    sen->baseline = baseline;
    map_add_not_exist(c, text, sen);

    TT_LOG("CACHE: add %02d 0x%08X\n", size, (uint32_t)data);
}

static void measure_line(struct text_line *line, struct glyph_cache_entry *en, int size)
{
    int i, penX, penY, idx, prev_idx, error;
    FT_Vector delta;
    FT_Glyph glyph;
    const int use_kerning = FT_HAS_KERNING(en->face);
    FT_BBox bbox, glyph_bbox;
    bbox.yMin = LONG_MAX;
    bbox.yMax = LONG_MIN;

    penX = penY = prev_idx = 0;

    // Load glyphs and their positions
    for(i = 0; i < line->len; ++i)
    {
        idx = FT_Get_Char_Index(en->face, line->text[i]);

        if(use_kerning && prev_idx && idx)
        {
            FT_Get_Kerning(en->face, prev_idx, idx, FT_KERNING_DEFAULT, &delta);
            penX += delta.x >> 6;
        }

        glyph = imap_get_val(en->glyphs, (int)line->text[i]);
        if(!glyph)
        {
            error = FT_Load_Glyph(en->face, idx, FT_LOAD_DEFAULT);
            if(error)
                continue;

            error = FT_Get_Glyph(en->face->glyph, &glyph);
            if(error)
                continue;

            imap_add_not_exist(en->glyphs, (int)line->text[i], glyph);
        }

        FT_Glyph_Get_CBox(glyph, ft_glyph_bbox_pixels, &glyph_bbox);
        bbox.yMin = imin(bbox.yMin, glyph_bbox.yMin);
        bbox.yMax = imax(bbox.yMax, glyph_bbox.yMax);

        line->pos[i].x = penX;
        line->pos[i].y = penY;

        penX += glyph->advance.x >> 16;
        prev_idx = idx;
    }

    if(bbox.yMin > bbox.yMax)
        bbox.yMin = bbox.yMax = 0;

    line->w = penX;
    line->h = bbox.yMax - bbox.yMin;
    line->base = bbox.yMax;
}

static void render_line(struct text_line *line, struct glyph_cache_entry *en, px_type *res_data, int stride, px_type converted_color)
{
    int i, error;
    FT_Glyph *image;
    FT_BitmapGlyph bit;

    for(i = 0; i < line->len; ++i)
    {
        image = imap_get_ref(en->glyphs, (int)line->text[i]); // pre-cached from measure_line()
        error = FT_Glyph_To_Bitmap(image, FT_RENDER_MODE_NORMAL, NULL, 1);
        if(error == 0)
        {
            bit = (FT_BitmapGlyph)(*image);
            convert_ft_bitmap(bit, converted_color, res_data, stride, line, &line->pos[i]);
        }
    }
}

static void destroy_line(struct text_line *line)
{
    free(line->pos);
    free(line);
}

typedef struct 
{
    char *text;
    px_type color;
    int size;
    int justify;
    int baseline;
} text_extra;

fb_img *fb_text_create_item(int x, int y, uint32_t color, int size, int justify, const char *text)
{
    fb_img *result = mzalloc(sizeof(fb_img));
    result->id = fb_generate_item_id();
    result->parent = &DEFAULT_FB_PARENT;
    result->type = FB_IT_IMG;
    result->level = LEVEL_TEXT;
    result->x = x;
    result->y = y;
    result->img_type = FB_IMG_TYPE_TEXT;
    result->data = NULL;
    result->extra = mzalloc(sizeof(text_extra));

    text_extra *extras = result->extra;
    // set color's alpha to 0, because data from the bitmap will act as alpha
    extras->color = fb_convert_color(color & ~(0xFF << 24));
    extras->size = size;
    extras->justify = justify;

    fb_text_set_content(result, text);

    if(!result->data)
    {
        free(extras);
        free(result);
        result = NULL;
    }

    return result;
}

void fb_text_set_content(fb_img *img, const char *text)
{
    int maxW, maxH, totalH, i, lineH, lines_cnt;
    struct glyph_cache_entry *en;
    struct string_cache_entry *sen, *old_sen = NULL;
    struct text_line **lines = NULL;
    const char *start = text, *end;
    px_type *res_data;
    text_extra *ex = img->extra;

    en = get_cache_for_size(ex->size);
    if(!en)
        return;

    if(ex->text)
    {
        old_sen = get_cache_for_string(ex->color, ex->size, ex->text);
    }
    else
    {
        // if ex->text is empty, this is first call of fb_set_text for this fb_img
        // increase refcount on this size's glyph cache entry
        ++en->refcnt;
    }

    sen = get_cache_for_string(ex->color, ex->size, text);
    if(sen)
    {
        if(old_sen)
        {
            TT_LOG("CACHE: drop %02d 0x%08X\n", ex->size, (uint32_t)old_sen->data);
            --old_sen->refcnt;
        }

        img->w = sen->w;
        img->h = sen->h;
        img->data = sen->data;
        ex->baseline = sen->baseline;
        if(ex->text != text)
        {
            ex->text = realloc(ex->text, strlen(text)+1);
            strcpy(ex->text, text);
        }
        ++sen->refcnt;

        TT_LOG("CACHE: use %02d 0x%08X\n", ex->size, (uint32_t)sen->data);
        TT_LOG("Getting string %dx%d %s from cache\n", img->w, img->h, ex->text);
        return;
    }

    TT_LOG("Rendering string %s\n", text);

    maxW = maxH = lines_cnt = 0;
    while(start && *start)
    {
        struct text_line *line = mzalloc(sizeof(struct text_line));
        line->text = start;

        end = strchr(start, '\n');
        if(end == NULL)
        {
            line->len = strlen(start);
            start = NULL;
        }
        else
        {
            line->len = end - start;
            start = ++end;
        }

        line->pos = mzalloc(sizeof(FT_Vector)*line->len);

        measure_line(line, en, ex->size);

        maxW = imax(maxW, line->w);
        maxH = imax(maxH, line->h);

        list_add(line, &lines);
        ++lines_cnt;
    }

    lineH = maxH * LINE_SPACING;
    totalH = 0;
    for(i = 0; i < lines_cnt; ++i)
    {
        switch(ex->justify)
        {
            case JUSTIFY_LEFT:
                break;
            case JUSTIFY_CENTER:
                lines[i]->offX = maxW/2 - lines[i]->w/2;
                break;
            case JUSTIFY_RIGHT:
                lines[i]->offX = maxW - lines[i]->w;
                break;
        }
        lines[i]->offY = totalH;
        totalH += lineH;
        ex->baseline = lines[i]->offY + lines[i]->base;
    }

    if(lines_cnt > 1)
        ex->baseline /= 2;

    // always 4 bytes per pixel cause of fb_img data structure
    if(old_sen)
    {
        img->data = malloc(maxW*totalH*4);
        --old_sen->refcnt;
        TT_LOG("CACHE: drop %02d 0x%08X\n", ex->size, (uint32_t)old_sen->data);
    }
    else
    {
        if(img->data != NULL)
        {
            TT_LOG("CACHE: realloc %02d 0x%08X old_sen 0x%08X %d\n", ex->size, (uint32_t)img->data, (uint32_t)old_sen, old_sen ? old_sen->refcnt : 0);
        }

        img->data = realloc(img->data, maxW*totalH*4);
    }

    memset(img->data, 0, maxW*totalH*4);
    img->w = maxW;
    img->h = totalH;

    for(i = 0; i < lines_cnt; ++i)
        render_line(lines[i], en, img->data, img->w, ex->color);

    add_to_string_cache(ex->color, ex->baseline, ex->size, text, img->w, img->h, img->data);

    list_clear(&lines, &destroy_line);

    if(ex->text != text)
    {
        ex->text = realloc(ex->text, strlen(text)+1);
        strcpy(ex->text, text);
    }
}

inline void center_text(fb_img *text, int targetX, int targetY, int targetW, int targetH)
{
    text_extra *ex = text->extra;

    if(targetX != -1)
        text->x = targetX + (targetW/2 - text->w/2);

    if(targetY != -1)
        text->y = targetY + (targetH/2 - ex->baseline/2);
}

void fb_text_set_color(fb_img *img, uint32_t color)
{
    text_extra *extras = img->extra;
    int copy = 0;
    const px_type converted_color = fb_convert_color(color & ~(0xFF << 24));

    if(extras->color == converted_color)
        return;

    struct string_cache_entry *sen = get_cache_for_string(extras->color, extras->size, extras->text);
    if(sen)
    {
        if(sen->refcnt == 1)
            sen->color = converted_color;
        else
        {
            copy = 1;
            --sen->refcnt;
        }
    }

    extras->color = converted_color;

    px_type *itr = img->data;
    if(copy)
    {
        img->data = malloc(img->w*img->h*4);
        memcpy(img->data, itr, img->w*img->h*4);
        itr = img->data;
    }

    const px_type *end = (px_type*)(((uint32_t*)itr) + img->w * img->h);
    int alpha;

    while(itr != end)
    {
#if PIXEL_SIZE == 4
        alpha = *itr & (0xFF << PX_IDX_A*8);
        if(alpha != 0)
            *itr = converted_color | alpha;
        ++itr;
#else
        if(itr[1] != 0)
            *itr = converted_color;
        itr += 2;
#endif
    }
}

void fb_text_set_size(fb_img *img, int size)
{
    text_extra *ex = img->extra;
    if(ex->size == size)
        return;
    ex->size = size;
    fb_text_set_content(img, ex->text);
}

void fb_text_destroy(fb_img *i)
{
    text_extra *ex = i->extra;

    struct string_cache_entry *sen = get_cache_for_string(ex->color, ex->size, ex->text);
    if(sen)
    {
        TT_LOG("CACHE: drop %02d 0x%08X\n", ex->size, (uint32_t)sen->data);
        --sen->refcnt;
    }
    else
    {
        TT_LOG("CACHE: free %02d 0x%08X\n", ex->size, (uint32_t)i->data);
        free(i->data);
    }

    struct glyph_cache_entry *en = get_cache_for_size(ex->size);
    if(en)
    {
        --en->refcnt;
        TT_LOG("Decreasing glyph cache %d counter to %d\n", ex->size, en->refcnt);
    }

    free(ex->text);
    free(ex);
    // fb_img is freed in fb_destroy_item
}

void fb_text_drop_cache_unused(void)
{
    size_t i, x;

    if(cache.glyph_cache)
    {
        TT_LOG("Dropping unused glyph caches\n");
        for(i = 0; i < cache.glyph_cache->size;)
        {
            const int key = cache.glyph_cache->keys[i];
            struct glyph_cache_entry *en = cache.glyph_cache->values[i];
            TT_LOG("glyph_cache_entry size %d has refcnt %d\n", key, en->refcnt);
            if(en->refcnt == 0)
            {
                imap_destroy(en->glyphs, (void*)&FT_Done_Glyph);
                FT_Done_Face(en->face);
                imap_rm(cache.glyph_cache, key, &free);
            }
            else
                ++i;
        }

        if(cache.glyph_cache->size == 0)
        {
            TT_LOG("Whole glyph cache was freed.\n");
            imap_destroy(cache.glyph_cache, NULL);
            cache.glyph_cache = NULL;
        }
    }

    if(!cache.glyph_cache && cache.ft_lib)
    {
        TT_LOG("Freeing libfreetype\n");
        FT_Done_FreeType(cache.ft_lib);
        cache.ft_lib = NULL;
    }

    if(cache.string_cache)
    {
        TT_LOG("Dropping unused string caches\n");
        for(i = 0; i < cache.string_cache->size; )
        {
            const int key = cache.string_cache->keys[i];
            map *size_c = cache.string_cache->values[i];
            TT_LOG("Dropping unused string caches for size %d\n", key);

            for(x = 0; x < size_c->size; )
            {
                char *s_key = size_c->keys[x];
                struct string_cache_entry *sen = size_c->values[x];

                TT_LOG("string_cache_entry size %d str \"%s\" has refcnt %d\n", key, s_key, sen->refcnt);
                if(sen->refcnt == 0)
                {
                    free(sen->data);
                    map_rm(size_c, s_key, &free);
                }
                else
                    ++x;
            }

            if(size_c->size == 0)
            {
                TT_LOG("Removing string cache for size %d\n", key);
                map_destroy(size_c, &free);
                imap_rm(cache.string_cache, key, NULL);
            }
            else
                ++i;
        }

        if(cache.string_cache->size == 0)
        {
            TT_LOG("Whole string cache was freed.\n");
            imap_destroy(cache.string_cache, NULL);
            cache.string_cache = NULL;
        }
    }
}
