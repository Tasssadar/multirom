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

#ifndef H_FRAMEBUFFER
#define H_FRAMEBUFFER

#include <linux/fb.h>
#include <stdarg.h>

#if defined(RECOVERY_BGRA) || defined(RECOVERY_RGBX)
#define PIXEL_SIZE 4
typedef uint32_t px_type;
#else
#define PIXEL_SIZE 2
#ifndef RECOVERY_RGB_565
  #define RECOVERY_RGB_565
#endif
typedef uint16_t px_type;
#endif

struct FB {
    px_type *bits;
    px_type *mapped;
    uint32_t size;
    int stride;
    int fd;
    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;
};

extern struct FB *fb;

#define ISO_CHAR_HEIGHT 16
#define ISO_CHAR_WIDTH 8

// Colors, 0xAABBGGRR
#define BLACK     0xFF000000
#define WHITE     0xFFFFFFFF
#define LBLUE     0xFFCC9900
#define LBLUE2    0xFFF4DFA8
#define GRAYISH   0xFFBEBEBE
#define GRAY      0xFF7F7F7F
#define DRED      0xFF0000CC

#if defined(MR_XHDPI)
enum
{
    SIZE_SMALL     = 2,
    SIZE_NORMAL    = 3,
    SIZE_BIG       = 4,
    SIZE_EXTRA     = 6,
};
#elif defined(MR_HDPI)
enum
{
    SIZE_SMALL     = 1,
    SIZE_NORMAL    = 2,
    SIZE_BIG       = 3,
    SIZE_EXTRA     = 4,
};
#endif

extern uint32_t fb_width;
extern uint32_t fb_height;
extern int fb_rotation;

int fb_open(int rotation);
void fb_close(void);
void fb_update(void);
void fb_switch(int n_sig);
inline struct FB *get_active_fb();
void fb_set_active_framebuffer(unsigned n);
void fb_dump_info(void);

enum 
{
    FB_TEXT = 0,
    FB_RECT = 1,
    FB_BOX  = 2,
};

typedef struct
{
    int id;
    int type;
    int x;
    int y;
} fb_item_header;

typedef struct
{
    fb_item_header head;

    uint32_t color;
    int8_t size;
    char *text;
} fb_text;

typedef struct
{
    fb_item_header head;

    int w;
    int h;
    uint32_t color;
} fb_rect;

typedef struct
{
    fb_item_header head;
    int w, h;

    fb_text **texts;
    fb_rect *background[3];
} fb_msgbox;

typedef struct
{
    fb_text **texts;
    fb_rect **rects;
    fb_msgbox *msgbox;
} fb_items_t;

void fb_remove_item(void *item);
int fb_generate_item_id();
fb_text *fb_add_text(int x, int y, uint32_t color, int size, const char *fmt, ...);
fb_text *fb_add_text_long(int x, int y, uint32_t color, int size, char *text);
fb_rect *fb_add_rect(int x, int y, int w, int h, uint32_t color);
void fb_add_rect_notfilled(int x, int y, int w, int h, uint32_t color, int thickness, fb_rect ***list);
fb_msgbox *fb_create_msgbox(int w, int h, int bgcolor);
fb_text *fb_msgbox_add_text(int x, int y, int size, char *txt, ...);
void fb_msgbox_rm_text(fb_text *text);
void fb_destroy_msgbox(void);
void fb_rm_text(fb_text *t);
void fb_rm_rect(fb_rect *r);
px_type fb_convert_color(uint32_t c);

void fb_draw_text(fb_text *t);
void fb_draw_char(int x, int y, char c, px_type color, int size);
void fb_draw_square(int x, int y, px_type color, int size);
void fb_draw_overlay(void);
void fb_draw_rect(fb_rect *r);
void fb_fill(uint32_t color);
void fb_draw(void);
void fb_request_draw(void);
void fb_clear(void);
void fb_freeze(int freeze);
int fb_clone(char **buff);

void fb_push_context(void);
void fb_pop_context(void);

inline int center_x(int x, int width, int size, const char *text);
inline int center_y(int y, int height, int size);

int vt_set_mode(int graphics);

#endif
