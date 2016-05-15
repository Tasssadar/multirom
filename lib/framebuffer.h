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
#include <pthread.h>

#if defined(RECOVERY_BGRA) || defined(RECOVERY_RGBX) || defined(RECOVERY_ABGR)
#define PIXEL_SIZE 4
typedef uint32_t px_type;
#else
#define PIXEL_SIZE 2
#ifndef RECOVERY_RGB_565
  #define RECOVERY_RGB_565
#endif
typedef uint16_t px_type;
#endif

#ifdef RECOVERY_BGRA
#define PX_IDX_A 3
#define PX_IDX_R 2
#define PX_IDX_G 1
#define PX_IDX_B 0
#define PX_GET_R(px) ((px & 0xFF0000) >> 16)
#define PX_GET_G(px) ((px & 0xFF00) >> 8)
#define PX_GET_B(px) ((px & 0xFF))
#define PX_GET_A(px) ((px & 0xFF000000) >> 24)
#elif defined(RECOVERY_RGBX)
#define PX_IDX_A 3
#define PX_IDX_R 0
#define PX_IDX_G 1
#define PX_IDX_B 2
#define PX_GET_R(px) (px & 0xFF)
#define PX_GET_G(px) ((px & 0xFF00) >> 8)
#define PX_GET_B(px) ((px & 0xFF0000) >> 16)
#define PX_GET_A(px) ((px & 0xFF000000) >> 24)
#elif defined(RECOVERY_RGB_565)
#define PX_GET_R(px) ((((((px & 0xF800) >> 11)*100)/31)*0xFF)/100)
#define PX_GET_G(px) ((((((px & 0x7E0) >> 5)*100)/63)*0xFF)/100)
#define PX_GET_B(px) (((((px & 0x1F)*100)/31)*0xFF)/100)
#define PX_GET_A(px) (0xFF)
#elif defined(RECOVERY_ABGR)
#define PX_IDX_A 3
#define PX_IDX_R 0
#define PX_IDX_G 1
#define PX_IDX_B 2
#define PX_GET_R(px) ((px & 0xFF))
#define PX_GET_G(px) ((px & 0xFF00) >> 8)
#define PX_GET_B(px) ((px & 0xFF0000) >> 16)
#define PX_GET_A(px) ((px & 0xFF000000) >> 24)
#endif

struct framebuffer {
    px_type *buffer;
    uint32_t size;
    uint32_t stride;
    int fd;
    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;
    struct fb_impl *impl;
    void *impl_data;
};

struct fb_impl {
    const char *name;
    const int impl_id;

    int (*open)(struct framebuffer *fb);
    void (*close)(struct framebuffer *fb);
    int (*update)(struct framebuffer *fb);
    void *(*get_frame_dest)(struct framebuffer *fb);
};

enum
{
#ifdef MR_USE_QCOM_OVERLAY
    FB_IMPL_QCOM_OVERLAY,
#endif

    FB_IMPL_GENERIC, // must be last

    FB_IMPL_CNT
};

// Colors, 0xAARRGGBB
#define BLACK     0xFF000000
#define WHITE     0xFFFFFFFF
#define LBLUE     0xFF0099CC
#define LBLUE2    0xFFA8DFF4
#define GRAYISH   0xFFBEBEBE
#define GRAY      0xFF7F7F7F
#define DRED      0xFFCC0000

// Font sizes in 1/4 of a point
enum
{
    SIZE_SMALL     = (7*4),
    SIZE_NORMAL    = (10*4),
    SIZE_BIG       = (13*4),
    SIZE_EXTRA     = (15*4),
};

extern uint32_t fb_width;
extern uint32_t fb_height;
extern int fb_rotation;

int fb_open(int rotation);
int fb_open_impl(void);
void fb_close(void);
void fb_update(void);
void fb_dump_info(void);
int fb_get_vi_xres(void);
int fb_get_vi_yres(void);
void fb_force_generic_impl(int force);

enum
{
    FB_IT_RECT,
    FB_IT_IMG,
    FB_IT_LISTVIEW,
    FB_IT_LINE,
};

enum
{
    FB_IMG_TYPE_GENERIC,
    FB_IMG_TYPE_PNG,
    FB_IMG_TYPE_TEXT,
};

enum
{
    JUSTIFY_LEFT,
    JUSTIFY_CENTER,
    JUSTIFY_RIGHT,
};

enum
{
    STYLE_NORMAL,
    STYLE_ITALIC,
    STYLE_BOLD,
    STYLE_BOLD_ITALIC,
    STYLE_MEDIUM,
    STYLE_CONDENSED,
    STYLE_MONOSPACE,

    STYLE_COUNT
};

enum
{
    LEVEL_LISTVIEW = 0,
    LEVEL_RECT = 1,
    LEVEL_CIRCLE = 1,
    LEVEL_PNG  = 2,
    LEVEL_LINE = 2,
    LEVEL_TEXT = 3,
};

struct fb_item_header;

#define FB_ITEM_POS \
    int x, y; \
    int w, h;

typedef struct
{
    FB_ITEM_POS
} fb_item_pos;

extern fb_item_pos DEFAULT_FB_PARENT;

#define FB_ITEM_HEAD \
    FB_ITEM_POS \
    int id; \
    int type; \
    int level; \
    fb_item_pos *parent; \
    struct fb_item_header *prev; \
    struct fb_item_header *next;

struct fb_item_header
{
    FB_ITEM_HEAD
};
typedef struct fb_item_header fb_item_header;

typedef struct
{
    FB_ITEM_HEAD

    uint32_t color;
} fb_rect;

/*
 * fb_img element draws pre-rendered image data, which can come for
 * example from a PNG file.
 * For RECOVERY_BGRA and RECOVERY_BGRX (4 bytes per px), data is just
 * array of pixels in selected px format.
 * For RECOVERY_RGB_565 (2 bytes per px), another 2 bytes with
 * alpha values are added after each pixel. So, one pixel is two uint16_t
 * entries in the result uint16_t array:
 * [0]: (R | (G << 5) | (B << 11))
 * [1]: (alphaForRB | (alphaForG << 8))
 * [2]: (R | (G << 5) | (B << 11))
 * [3]: (alphaForRB | (alphaForG << 8))
 * ...
 */
typedef struct
{
    FB_ITEM_HEAD

    int img_type;
    px_type *data;
    void *extra;
} fb_img;

typedef fb_img fb_text;
typedef fb_img fb_circle;

typedef struct
{
    FB_ITEM_HEAD;
    int x2, y2;
    int thickness;
    uint32_t color;
} fb_line;

typedef struct
{
    uint32_t background_color;
    fb_item_header *first_item;
    pthread_mutex_t mutex;
    volatile int batch_started;
    volatile pthread_t batch_thread;
} fb_context_t;

typedef struct
{
    int x, y;
    int level;
    fb_item_pos *parent;
    uint32_t color;
    int size;
    int justify;
    int style;
    char *text;
    int wrap_w;
} fb_text_proto;

void fb_remove_item(void *item);
int fb_generate_item_id(void);
px_type fb_convert_color(uint32_t c);
uint32_t fb_convert_color_img(uint32_t c);

fb_img *fb_add_text(int x, int y, uint32_t color, int size, const char *fmt, ...);
fb_text_proto *fb_text_create(int x, int y, uint32_t color, int size, const char *text);
fb_img *fb_text_finalize(fb_text_proto *p);
void fb_text_set_color(fb_img *img, uint32_t color);
void fb_text_set_size(fb_img *img, int size);
void fb_text_set_content(fb_img *img, const char *text);
char *fb_text_get_content(fb_img *img);

void fb_text_drop_cache_unused(void);
void fb_text_destroy(fb_img *i);

fb_rect *fb_add_rect_lvl(int level, int x, int y, int w, int h, uint32_t color);
#define fb_add_rect(x, y, w, h, color) fb_add_rect_lvl(LEVEL_RECT, x, y, w, h, color)
void fb_add_rect_notfilled(int level, int x, int y, int w, int h, uint32_t color, int thickness, fb_rect ***list);

fb_img *fb_add_img(int level, int x, int y, int w, int h, int img_type, px_type *data);
fb_img *fb_add_png_img_lvl(int level, int x, int y, int w, int h, const char *path);
#define fb_add_png_img(x, y, w, h, path) fb_add_png_img_lvl(LEVEL_PNG, x, y, w, h, path)

fb_circle *fb_add_circle_lvl(int level, int x, int y, int radius, uint32_t color);
#define fb_add_circle(x, y, radius, color) fb_add_circle_lvl(LEVEL_CIRCLE, x, y, radius, color)

fb_line *fb_add_line_lvl(int level, int x1, int y1, int x2, int y2, int thickness, uint32_t color);
#define fb_add_line(x1, y1, x2, y2, thickness, color) fb_add_line_lvl(LEVEL_LINE, x1, y1, x2, y2, thickness, color)

void fb_rm_text(fb_img *i);
void fb_rm_rect(fb_rect *r);
void fb_rm_img(fb_img *i);
void fb_rm_circle(fb_circle *c);
void fb_rm_line(fb_line *l);

void fb_draw_rect(fb_rect *r);
void fb_draw_img(fb_img *i);
void fb_draw_line(fb_line *l);
void fb_fill(uint32_t color);
void fb_request_draw(void);
void fb_force_draw(void);
void fb_clear(void);
void fb_freeze(int freeze);
int fb_clone(char **buff);
int fb_save_screenshot(void);
void fb_set_brightness(int val);

void fb_push_context(void);
void fb_pop_context(void);

void fb_batch_start(void);
void fb_batch_end(void);

void fb_ctx_add_item(void *item);
void fb_ctx_rm_item(void *item);
inline void fb_items_lock(void);
inline void fb_items_unlock(void);
void fb_set_background(uint32_t color);

px_type *fb_png_get(const char *path, int w, int h);
void fb_png_release(px_type *data);
void fb_png_drop_unused(void);
int fb_png_save_img(const char *path, int w, int h, int stride, px_type *data);

inline void center_text(fb_img *text, int targetX, int targetY, int targetW, int targetH);

int vt_set_mode(int graphics);

#endif
