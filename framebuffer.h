#ifndef H_FRAMEBUFFER
#define H_FRAMEBUFFER

#include <linux/fb.h>
#include <stdarg.h>

struct FB {
    uint32_t *bits;
    uint32_t *mapped;
    uint32_t size;
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
#define LBLUE     0xFFE5B533
#define LBLUE2    0xFFF4DFA8
#define GRAYISH   0xFFBEBEBE
#define GRAY      0xFF7F7F7F
#define DRED      0xFF0000CC

enum
{
    SIZE_SMALL     = 1,
    SIZE_NORMAL    = 2,
    SIZE_BIG       = 3,
    SIZE_EXTRA     = 4,
};

#define fb_size(fb) ((fb)->vi.xres * (fb)->vi.yres * 4)
extern int fb_width;
extern int fb_height;

int fb_open(void);
void fb_close(void);
void fb_update(void);
void fb_switch(int n_sig);
inline struct FB *get_active_fb();
void fb_set_active_framebuffer(unsigned n);

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

    int color;
    int8_t size;
    char *text;
} fb_text;

typedef struct
{
    fb_item_header head;

    int w;
    int h;
    int color;
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
fb_text *fb_add_text(int x, int y, int color, int size, const char *fmt, ...);
fb_rect *fb_add_rect(int x, int y, int w, int h, int color);
fb_msgbox *fb_create_msgbox(int w, int h);
fb_text *fb_msgbox_add_text(int x, int y, int size, char *txt, ...);
void fb_msgbox_rm_text(fb_text *text);
void fb_destroy_msgbox(void);
void fb_rm_text(fb_text *t);
void fb_rm_rect(fb_rect *r);

void fb_draw_text(fb_text *t);
void fb_draw_char(int x, int y, char c, int color, int size);
void fb_draw_square(int x, int y, int color, int size);
void fb_draw_overlay(void);
void fb_draw_rect(fb_rect *r);
void fb_fill(uint32_t color);
void fb_draw(void);
void fb_clear(void);
void fb_freeze(int freeze);

inline int center_x(int x, int width, int size, const char *text);
inline int center_y(int y, int height, int size);

#if 0
#define fb_debug(fmt, ...) fb_printf(fmt, ##__VA_ARGS__)
#else
#define fb_debug(fmt, ...) ERROR(fmt, ##__VA_ARGS__)
#define fb_printf(fmt, ...) ERROR(fmt, ##__VA_ARGS__)
#endif

int vt_set_mode(int graphics);

#endif
