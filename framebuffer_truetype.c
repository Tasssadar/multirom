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
#include <ctype.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "log.h"
#include "framebuffer.h"
#include "util.h"
#include "containers.h"
#include "multirom.h"

#define LINE_SPACING 1.15

#if 0
#define TT_LOG(fmt, x...) INFO("TT: "fmt, ##x)
#else
#define TT_LOG(x...) ;
#endif

static const char *FONT_FILES[STYLE_COUNT] = {
    "Roboto-Regular.ttf",     // STYLE_NORMAL
    "Roboto-Italic.ttf",      // STYLE_ITALIC
    "Roboto-Bold.ttf",        // STYLE_BOLD
    "Roboto-Medium.ttf",      // STYLE_MEDIUM
    "OxygenMono-Regular.ttf", // STYLE_MONOSPACE
};

struct glyphs_entry
{
    FT_Face face;
    imap *glyphs;
    int refcnt;
};

struct strings_entry
{
    px_type *data;
    int w, h;
    int baseline;
    int refcnt;
    px_type color;
};

struct text_cache
{
    imap *glyphs[STYLE_COUNT];
    imap *strings[STYLE_COUNT];
    FT_Library ft_lib;
};

static struct text_cache cache = {
    .glyphs = { 0 },
    .strings = { 0 },
    .ft_lib = NULL
};

struct text_line
{
    char *text; // doesn't end with \0
    int len;
    int w, h, base;
    int offX, offY;
    FT_Vector *pos;
};

typedef struct 
{
    char *text;
    px_type color;
    int size;
    int justify;
    int style;
    int baseline;
    int wrap_w;
} text_extra;

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

static struct glyphs_entry *get_cache_for_size(const int style, const int size)
{
    int error;
    struct glyphs_entry *res;

    if(!cache.ft_lib)
    {
        error = FT_Init_FreeType(&cache.ft_lib);
        if(error)
        {
            ERROR("libtruetype init failed with %d\n", error);
            return NULL;
        }
    }

    if(!cache.glyphs[style])
        cache.glyphs[style] = imap_create();

    res = imap_get_val(cache.glyphs[style], size);
    if(!res)
    {
        char buff[128];
        res = mzalloc(sizeof(struct glyphs_entry));
        snprintf(buff, sizeof(buff), "%s/res/%s", multirom_dir, FONT_FILES[style]);
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
        imap_add_not_exist(cache.glyphs[style], size, res);
    }

    return res;
}

static struct strings_entry *get_cache_for_string(text_extra *ex)
{
    if(!cache.strings[ex->style])
        return NULL;

    map *c = imap_get_val(cache.strings[ex->style], ex->size);
    if(!c)
        return NULL;

    struct strings_entry *sen = map_get_val(c, ex->text);
    if(sen && sen->color == ex->color)
        return sen;
    return NULL;
}

static void add_to_strings(fb_img *img)
{
    text_extra *ex = img->extra;

    if(!cache.strings[ex->style])
        cache.strings[ex->style] = imap_create();

    map *c = imap_get_val(cache.strings[ex->style], ex->size);
    if(!c)
    {
        c = map_create();
        imap_add_not_exist(cache.strings[ex->style], ex->size, c);
    }
    else if(map_find(c, ex->text) != -1)
    {
        return;
    }

    struct strings_entry *sen = mzalloc(sizeof(struct strings_entry));
    sen->data = img->data;
    sen->refcnt = 1;
    sen->w = img->w;
    sen->h = img->h;
    sen->color = ex->color;
    sen->baseline = ex->baseline;
    map_add_not_exist(c, ex->text, sen);

    TT_LOG("CACHE: add %02d 0x%08X\n", ex->size, (uint32_t)img->data);
}

static int unlink_from_caches(text_extra *ex)
{
    struct glyphs_entry *en;
    struct strings_entry *sen;

    en = get_cache_for_size(ex->style, ex->size);
    if(en)
        --en->refcnt;

    sen = get_cache_for_string(ex);
    if(sen)
    {
        --sen->refcnt;
        TT_LOG("CACHE: drop %02d 0x%08X\n", ex->size, (uint32_t)sen->data);
        return 1;
    }
    return 0;
}

static int measure_line(struct text_line *line, struct glyphs_entry *en, text_extra *ex)
{
    int i, penX, penY, idx, prev_idx, error, last_space, wrapped;
    FT_Vector delta;
    FT_Glyph glyph;
    const int use_kerning = FT_HAS_KERNING(en->face);
    FT_BBox bbox, glyph_bbox;
    bbox.yMin = LONG_MAX;
    bbox.yMax = LONG_MIN;

    penX = penY = prev_idx = last_space = wrapped = 0;

    // Load glyphs and their positions
    for(i = 0; i < line->len; ++i)
    {
        idx = FT_Get_Char_Index(en->face, line->text[i]);

        if(use_kerning && prev_idx && idx)
        {
            FT_Get_Kerning(en->face, prev_idx, idx, FT_KERNING_DEFAULT, &delta);
            penX += delta.x >> 6;
        }

        if(ex->wrap_w && penX >= ex->wrap_w)
        {
            if(last_space == 0)
                last_space = i-1;
            line->len = last_space + 1;
            if(i-1 != last_space)
                penX = line->pos[last_space+1].x;
            wrapped = 1;
            break;
        }

        if(isspace(line->text[i]))
            last_space = i;

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
    return wrapped;
}

static void render_line(struct text_line *line, struct glyphs_entry *en, px_type *res_data, int stride, px_type converted_color)
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

static void fb_text_render(fb_img *img)
{
    int maxW, maxH, totalH, i, lineH, lines_cnt;
    struct glyphs_entry *en;
    struct strings_entry *sen;
    struct text_line **lines = NULL;
    char *start, *end;
    px_type *res_data;
    text_extra *ex = img->extra;

    en = get_cache_for_size(ex->style, ex->size);
    if(!en)
        return;

    ++en->refcnt;

    sen = get_cache_for_string(ex);
    if(sen)
    {
        img->w = sen->w;
        img->h = sen->h;
        img->data = sen->data;
        ex->baseline = sen->baseline;
        ++sen->refcnt;

        TT_LOG("CACHE: use %02d 0x%08X\n", ex->size, (uint32_t)sen->data);
        TT_LOG("Getting string %dx%d %s from cache\n", img->w, img->h, ex->text);
        return;
    }

    TT_LOG("Rendering string %s\n", ex->text);

    maxW = maxH = lines_cnt = 0;
    start = ex->text;
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

        if(measure_line(line, en, ex))
            start = line->text + line->len;

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

    img->w = img->h = 0;

    // always 4 bytes per pixel cause of fb_img data structure
    img->data = mzalloc(maxW*totalH*4);

    for(i = 0; i < lines_cnt; ++i)
        render_line(lines[i], en, img->data, maxW, ex->color);

    img->w = maxW;
    img->h = totalH;

    add_to_strings(img);

    list_clear(&lines, &destroy_line);
}

fb_img *fb_add_text(int x, int y, uint32_t color, int size, const char *fmt, ...)
{
    int ret;
    fb_img *res;
    fb_text_proto *p;
    char txt[512] = { 0 };
    char *buff = txt;
    va_list ap;

    txt[0] = 0;

    va_start(ap, fmt);
    ret = vsnprintf(txt, sizeof(txt), fmt, ap);
    if(ret >= (int)sizeof(txt))
    {
        buff = malloc(ret+1);
        vsnprintf(buff, ret+1, fmt, ap);
    }
    va_end(ap);

    p = fb_text_create(x, y, color, size, buff);
    res = fb_text_finalize(p);

    if(ret >= (int)sizeof(txt))
        free(buff);
    return res;
}

fb_text_proto *fb_text_create(int x, int y, uint32_t color, int size, const char *text)
{
    fb_text_proto *p = mzalloc(sizeof(fb_text_proto));
    p->x = x;
    p->y = y;
    p->level = LEVEL_TEXT;
    p->parent = &DEFAULT_FB_PARENT;
    p->color = color;
    p->size = size;
    p->style = STYLE_NORMAL;
    if(text)
        p->text = strdup(text);
    return p;
}

fb_img *fb_text_finalize(fb_text_proto *p)
{
    fb_img *result = mzalloc(sizeof(fb_img));
    result->id = fb_generate_item_id();
    result->type = FB_IT_IMG;
    result->parent = p->parent;
    result->level = p->level;
    result->x = p->x;
    result->y = p->y;
    result->img_type = FB_IMG_TYPE_TEXT;
    result->data = NULL;
    result->extra = mzalloc(sizeof(text_extra));

    text_extra *extras = result->extra;
    // set color's alpha to 0 because data from the font will act as alpha
    extras->color = fb_convert_color(p->color & ~(0xFF << 24));
    extras->size = p->size;
    extras->justify = p->justify;
    extras->style = p->style;
    extras->text = p->text;
    extras->wrap_w = p->wrap_w;

    free(p);

    fb_text_render(result);
    fb_ctx_add_item(result);

    return result;
}

void fb_text_set_color(fb_img *img, uint32_t color)
{
    text_extra *extras = img->extra;
    int copy = 0;
    const px_type converted_color = fb_convert_color(color & ~(0xFF << 24));

    if(extras->color == converted_color)
        return;

    struct strings_entry *sen = get_cache_for_string(extras);
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

    fb_items_lock();

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

    fb_items_unlock();
}

void fb_text_set_size(fb_img *img, int size)
{
    text_extra *ex = img->extra;

    if(size == ex->size)
        return;

    fb_items_lock();
    if(unlink_from_caches(ex) == 0)
    {
        img->w = img->h = 0;
        free(img->data);
        img->data = NULL;
    }

    ex->size = size;
    fb_text_render(img);
    fb_items_unlock();
}

void fb_text_set_content(fb_img *img, const char *text)
{
    text_extra *ex = img->extra;

    if(text == ex->text)
        return;

    fb_items_lock();
    if(unlink_from_caches(ex) == 0)
    {
        img->w = img->h = 0;
        free(img->data);
        img->data = NULL;
    }

    ex->text = realloc(ex->text, strlen(text)+1);
    strcpy(ex->text, text);
    fb_text_render(img);
    fb_items_unlock();
}

inline void center_text(fb_img *text, int targetX, int targetY, int targetW, int targetH)
{
    text_extra *ex = text->extra;

    if(targetX != -1)
        text->x = targetX + (targetW/2 - text->w/2);

    if(targetY != -1)
        text->y = targetY + (targetH/2 - ex->baseline/2);
}

void fb_text_destroy(fb_img *i)
{
    text_extra *ex = i->extra;

    struct strings_entry *sen = get_cache_for_string(ex);
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

    struct glyphs_entry *en = get_cache_for_size(ex->style, ex->size);
    if(en)
    {
        --en->refcnt;
        TT_LOG("Decreasing glyph cache %d counter to %d\n", ex->size, en->refcnt);
    }

    free(ex->text);
    free(ex);
    // fb_img is freed in fb_destroy_item
}

static int drop_glyphs_cache(imap *g_cache)
{
    size_t i;
    for(i = 0; i < g_cache->size;)
    {
        const int key = g_cache->keys[i];
        struct glyphs_entry *en = g_cache->values[i];
        TT_LOG("glyphs_entry size %d has refcnt %d\n", key, en->refcnt);
        if(en->refcnt == 0)
        {
            imap_destroy(en->glyphs, (void*)&FT_Done_Glyph);
            FT_Done_Face(en->face);
            imap_rm(g_cache, key, &free);
        }
        else
            ++i;
    }
    return g_cache->size == 0;
}

static int drop_strings_cache(imap *s_cache)
{
    size_t i, x;
    for(i = 0; i < s_cache->size; )
    {
        const int key = s_cache->keys[i];
        map *size_c = s_cache->values[i];
        TT_LOG("Dropping unused string caches for size %d\n", key);

        for(x = 0; x < size_c->size; )
        {
            char *s_key = size_c->keys[x];
            struct strings_entry *sen = size_c->values[x];

            TT_LOG("strings_entry size %d str \"%s\" has refcnt %d\n", key, s_key, sen->refcnt);
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
            imap_rm(s_cache, key, NULL);
        }
        else
            ++i;
    }
    return s_cache->size == 0;
}

void fb_text_drop_cache_unused(void)
{
    size_t s;
    int free_ft_lib = 1;

    for(s = 0; s < STYLE_COUNT; ++s)
    {
        if(cache.glyphs[s])
        {
            if(drop_glyphs_cache(cache.glyphs[s]))
            {
                TT_LOG("Whole glyph cache was freed.\n");
                imap_destroy(cache.glyphs[s], NULL);
                cache.glyphs[s] = NULL;
            }
            else
                free_ft_lib = 0;
        }

        if(cache.strings[s])
        {
            if(drop_strings_cache(cache.strings[s]))
            {
                TT_LOG("Whole string cache was freed.\n");
                imap_destroy(cache.strings[s], NULL);
                cache.strings[s] = NULL;
            }
        }
    }

    if(free_ft_lib && cache.ft_lib)
    {
        TT_LOG("Freeing libfreetype\n");
        FT_Done_FreeType(cache.ft_lib);
        cache.ft_lib = NULL;
    }
}
