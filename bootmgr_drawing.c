#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cutils/memory.h>
#include <stdarg.h>
#include <linux/input.h>
#include <pthread.h>

#include "init.h"
#include "bootmgr.h"
#include "bootmgr_shared.h"
#include "iso_font.h"

const char* bootmgr_bg = "/bmgr_imgs/init.rle";
const char* bootmgr_img_folder = "/bmgr_imgs/%s";
static pthread_mutex_t bootmgr_draw_mutex = PTHREAD_MUTEX_INITIALIZER;

struct FB fb;
struct stat s;
int fd;
unsigned max_fb_size;

void bootmgr_init_display()
{
    bootmgr_display = (bootmgr_display_t*)malloc(sizeof(bootmgr_display_t));
    bootmgr_display->ln_count = 0;
    bootmgr_display->fill_count = 0;
    bootmgr_display->img_count = 0;
    bootmgr_display->touch_count = 0;
    bootmgr_display->lines = NULL;
    bootmgr_display->fills = NULL;
    bootmgr_display->imgs = NULL;
    bootmgr_display->touches = NULL;
    bootmgr_display->bg_img = 1;

    if(bootmgr_open_framebuffer() != 0)
    {
        ERROR("BootMgr: Cant open framebuffer");
        return;
    }
}

void bootmgr_destroy_display()
{
    bootmgr_clear();
    free(bootmgr_display);

    bootmgr_close_framebuffer();
}

void bootmgr_printf(int16_t x, uint8_t line, uint16_t color, char *what, ...)
{
    char txt[80];
    va_list ap;
    va_start(ap, what);
    vsnprintf(txt, 80, what, ap);
    va_end(ap);

    bootmgr_line *ln = NULL;
    if(ln = _bootmgr_get_line(line))
        free(ln->text);
    else
        ln = _bootmgr_new_line();

    int16_t text_len = strlen(txt);
    ln->text = malloc(text_len+1);
    strcpy(ln->text, txt);

    if(x == -1)
        ln->x = (BOOTMGR_DIS_W - text_len*8)/2;
    else
        ln->x = x;
    ln->line = line;
    ln->color = color;
}

void bootmgr_print_img(int16_t x, int16_t y, char *name)
{
    int16_t text_len = strlen(name);
    bootmgr_img *img = _bootmgr_new_img();
    img->name = malloc(text_len+1);
    strcpy(img->name, name);
    img->x = x;
    img->y = y;
}

void bootmgr_print_fill(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color, int8_t id)
{
    bootmgr_fill *f = NULL;
    if(id == -1 || !(f = _bootmgr_get_fill(id)))
        f= _bootmgr_new_fill();
    f->x = x;
    f->y = y;
    f->width = width;
    f->height = height;
    f->color = color;
    f->id = id;
}

void bootmgr_add_touch(uint16_t x_min, uint16_t y_min, uint16_t x_max, uint16_t y_max, int (*callback)(), int8_t id)
{
    bootmgr_touch *t = NULL;
    if(id == -1 || !(t = _bootmgr_get_touch(id)))
        t = _bootmgr_new_touch();
    t->x_min = x_min;
    t->y_min = y_min;
    t->x_max = x_max;
    t->y_max = y_max;
    t->callback = callback;
    t->id = id;
}

int bootmgr_open_framebuffer()
{
    if (vt_set_mode(1))
        return -1;

    fd = open(bootmgr_bg, O_RDONLY);
    if (fd < 0)
        return -1;

    if (fstat(fd, &s) < 0)
        return -1;

    if (fb_open(&fb))
        return -1;

    max_fb_size = fb_width(&fb) * fb_height(&fb);
    return 0;
}

void bootmgr_close_framebuffer()
{
    fb_close(&fb);
    close(fd);
}

int bootmgr_show_img(uint16_t start_x, uint16_t start_y, char *custom_img)
{
    uint16_t *data, *bits, *ptr;
    uint32_t rgb32, red, green, blue, alpha;
    unsigned count, max;
    struct stat *_s = NULL;
    int *_fd = NULL;

    if(custom_img)
    {
        char *path = (char*)malloc(50);
        sprintf(path, bootmgr_img_folder, custom_img);

        _fd = malloc(sizeof(*_fd));

        *_fd = open(path, O_RDONLY);
        free(path);

        if(*_fd < 0)
            return -1;

        _s = malloc(sizeof(*_s));
        if (fstat(*_fd, _s) < 0)
            return -1;
    }
    else
    {
        _s = &s;
        _fd = &fd;
    }

    data = mmap(0, _s->st_size, PROT_READ, MAP_SHARED, *_fd, 0);
    if (data == MAP_FAILED)
        return -1;

    max = max_fb_size;
    ptr = data;
    count = _s->st_size;
    bits = fb.bits;

    if(start_x || start_y)
        bits += fb_width(&fb)*start_y + start_x;

    while (count > 3) {
        unsigned n = ptr[0];
        if (n > max)
            break;
        android_memset16(bits, ptr[1], n << 1);
        bits += n;
        max -= n;
        ptr += 2;
        count -= 4;
    }

    munmap(data, _s->st_size);

    if(custom_img)
    {
        close(*_fd);
        free(_s);
        free(_fd);
    }
    return 0;
}

void _bootmgr_set_px(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t *bits = fb.bits;
    bits += BOOTMGR_DIS_W*y + x;
    android_memset16(bits, color, 2);
}

void _bootmgr_draw_char(char c, uint16_t x, uint16_t y, uint16_t color)
{
    unsigned char line = 0;
    char bit = 0;
    for(; line < ISO_CHAR_HEIGHT; ++line)
    {
        int f = iso_font[ISO_CHAR_HEIGHT*c+line];
        for(bit = 0; bit < 8; ++bit)
            if(f & (1 << bit))
                _bootmgr_set_px(x+bit, y, color);
        ++y;
    }
}

void bootmgr_draw_text()
{
    uint16_t i,z,y;
    bootmgr_line *c = NULL;
    char *txt;

    for(i = 0; i < bootmgr_display->ln_count; ++i)
    {
        c = bootmgr_display->lines[i];
        txt = c->text;
        y = ISO_CHAR_HEIGHT*c->line;

        for(z = 0; *txt; ++txt,z+=8)
            _bootmgr_draw_char(*txt, c->x + z, y, c->color);
    }
}

void bootmgr_draw_fills()
{
    uint16_t i,z;
    bootmgr_fill *c = NULL;
    uint16_t *bits;
    for(i = 0; i < bootmgr_display->fill_count; ++i)
    {
        c = bootmgr_display->fills[i];
        bits = fb.bits;
        bits += BOOTMGR_DIS_W*c->y + c->x;

        for(z = 0; z <= c->height; ++z)
        {
            android_memset16(bits, c->color, c->width*2);
            bits += BOOTMGR_DIS_W;
        }
    }
}

void bootmgr_draw_imgs()
{
    bootmgr_img *c = NULL;
    uint16_t i = 0;
    for(; i < bootmgr_display->img_count; ++i)
    {
        c = bootmgr_display->imgs[i];
        bootmgr_show_img(c->x, c->y, c->name);
    }
}

void bootmgr_set_lines_count(uint16_t c)
{
    bootmgr_line **tmp_lines = bootmgr_display->lines;

    bootmgr_display->lines = (bootmgr_line**)malloc(sizeof(bootmgr_line*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_lines && itr < bootmgr_display->ln_count)
            bootmgr_display->lines[itr] = tmp_lines[itr];
        else
            bootmgr_display->lines[itr] = (bootmgr_line*)malloc(sizeof(bootmgr_line));
    }

    if(tmp_lines)
    {
        for(; itr < bootmgr_display->ln_count; ++itr)
        {
            free(tmp_lines[itr]->text);
            free(tmp_lines[itr]);
        }
        free(tmp_lines);
    }

    bootmgr_display->ln_count = c;
}

void bootmgr_set_fills_count(uint16_t c)
{
    bootmgr_fill **tmp_fills = bootmgr_display->fills;

    bootmgr_display->fills = (bootmgr_fill**)malloc(sizeof(bootmgr_fill*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_fills && itr < bootmgr_display->fill_count)
            bootmgr_display->fills[itr] = tmp_fills[itr];
        else
            bootmgr_display->fills[itr] = (bootmgr_fill*)malloc(sizeof(bootmgr_fill));
    }

    if(tmp_fills)
    {
        for(; itr < bootmgr_display->fill_count; ++itr)
            free(tmp_fills[itr]);
        free(tmp_fills);
    }

    bootmgr_display->fill_count = c;
}

void bootmgr_set_imgs_count(uint16_t c)
{
    bootmgr_img **tmp_imgs = bootmgr_display->imgs;

    bootmgr_display->imgs = (bootmgr_img**)malloc(sizeof(bootmgr_img*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_imgs && itr < bootmgr_display->img_count)
            bootmgr_display->imgs[itr] = tmp_imgs[itr];
        else
            bootmgr_display->imgs[itr] = (bootmgr_img*)malloc(sizeof(bootmgr_img));
    }

    if(tmp_imgs)
    {
        for(; itr < bootmgr_display->img_count; ++itr)
        {
            free(tmp_imgs[itr]->name);
            free(tmp_imgs[itr]);
        }
        free(tmp_imgs);
    }

    bootmgr_display->img_count = c;
}

void bootmgr_set_touches_count(uint16_t c)
{
    bootmgr_touch **tmp_touches = bootmgr_display->touches;

    bootmgr_display->touches = (bootmgr_touch**)malloc(sizeof(bootmgr_touch*)*c);
    uint16_t itr = 0;
    for(; itr < c; ++itr)
    {
        if(tmp_touches && itr < bootmgr_display->touch_count)
            bootmgr_display->touches[itr] = tmp_touches[itr];
        else
            bootmgr_display->touches[itr] = (bootmgr_touch*)malloc(sizeof(bootmgr_touch));
    }

    if(tmp_touches)
    {
        for(; itr < bootmgr_display->touch_count; ++itr)
            free(tmp_touches[itr]);
        free(tmp_touches);
    }

    bootmgr_display->touch_count = c;
}

void bootmgr_main_draw_sel()
{
    static const uint16_t height = 120;
    static const uint16_t width = 120;
    static const uint16_t x[] = { 40,  160,  40,  160 };
    static const uint16_t y[] = { 125, 125,  245, 245 };

    uint16_t *bits;
    uint16_t *line = fb.bits + y[bootmgr_selected]*BOOTMGR_DIS_W + x[bootmgr_selected];
    uint16_t i,z;

    for(i = 0; i < height; ++i)
    {
        bits = line;
        for(z = 0; z < width; ++z)
        {
            android_memset16(bits, ~(*bits), 2);
            ++bits;
        }
        line += BOOTMGR_DIS_W;
    }
}

void bootmgr_draw()
{
    pthread_mutex_lock(&bootmgr_draw_mutex);

    if(bootmgr_display->bg_img)
    {
        bootmgr_show_img(0, 0, NULL);
        bootmgr_main_draw_sel();
    }
    else
        android_memset16(fb.bits, BLACK, BOOTMGR_DIS_W*BOOTMGR_DIS_H*2);

    bootmgr_draw_imgs();
    bootmgr_draw_fills();
    bootmgr_draw_text();
    fb_update(&fb);

    pthread_mutex_unlock(&bootmgr_draw_mutex);
}

bootmgr_line *_bootmgr_new_line()
{
    bootmgr_set_lines_count(bootmgr_display->ln_count+1);
    return bootmgr_display->lines[bootmgr_display->ln_count-1];
}

bootmgr_fill *_bootmgr_new_fill()
{
    bootmgr_set_fills_count(bootmgr_display->fill_count+1);
    return bootmgr_display->fills[bootmgr_display->fill_count-1];
}

bootmgr_img *_bootmgr_new_img()
{
    bootmgr_set_imgs_count(bootmgr_display->img_count+1);
    return bootmgr_display->imgs[bootmgr_display->img_count-1];
}

bootmgr_touch *_bootmgr_new_touch()
{
    bootmgr_set_touches_count(bootmgr_display->touch_count+1);
    return bootmgr_display->touches[bootmgr_display->touch_count-1];
}

void bootmgr_erase_text(uint8_t line)
{
    bootmgr_line **tmp_lines = (bootmgr_line**)malloc(sizeof(bootmgr_line*)*(bootmgr_display->ln_count-1));

    uint16_t i = 0;
    uint16_t z = 0;
    bootmgr_line *c = NULL;

    for(; i < bootmgr_display->ln_count; ++i)
    {
        c = bootmgr_display->lines[i];
        if(c->line == line)
        {
            free(c->text);
            free(c);
            continue;
        }
        tmp_lines[z++] = c;
    }

    if(i == z)
        return;

    free(bootmgr_display->lines );
    bootmgr_display->lines = tmp_lines;
    --bootmgr_display->ln_count;
}

void bootmgr_erase_fill(int8_t id)
{
    bootmgr_fill **tmp_fills = (bootmgr_fill**)malloc(sizeof(bootmgr_fill*)*(bootmgr_display->fill_count-1));

    uint16_t i = 0;
    uint16_t z = 0;
    bootmgr_fill *c = NULL;

    for(; i < bootmgr_display->fill_count; ++i)
    {
        c = bootmgr_display->fills[i];
        if(c->id == id)
        {
            free(c);
            continue;
        }
        tmp_fills[z++] = c;
    }

    if(i == z)
        return;

    free(bootmgr_display->fills);
    bootmgr_display->fills = tmp_fills;
    --bootmgr_display->fill_count;
}

void bootmgr_erase_touch(int8_t id)
{
    bootmgr_touch **tmp_touches = (bootmgr_touch**)malloc(sizeof(bootmgr_touch*)*(bootmgr_display->touch_count-1));

    uint16_t i = 0;
    uint16_t z = 0;
    bootmgr_touch *c = NULL;

    for(; i < bootmgr_display->touch_count; ++i)
    {
        c = bootmgr_display->touches[i];
        if(c->id == id)
        {
            free(c);
            continue;
        }
        tmp_touches[z++] = c;
    }

    if(i == z)
        return;

    free(bootmgr_display->touches);
    bootmgr_display->touches = tmp_touches;
    --bootmgr_display->touch_count;
}

bootmgr_line *_bootmgr_get_line(uint8_t line)
{
    uint16_t i = 0;

    for(; i < bootmgr_display->ln_count; ++i)
    {
        if(bootmgr_display->lines[i]->line == line)
             return bootmgr_display->lines[i];
    }
    return NULL;
}

bootmgr_fill *_bootmgr_get_fill(int8_t id)
{
    uint16_t i = 0;

    for(; i < bootmgr_display->fill_count; ++i)
    {
        if(bootmgr_display->fills[i]->id == id)
             return bootmgr_display->fills[i];
    }
    return NULL;
}

bootmgr_touch *_bootmgr_get_touch(int8_t id)
{
    uint16_t i = 0;

    for(; i < bootmgr_display->touch_count; ++i)
    {
        if(bootmgr_display->touches[i]->id == id)
             return bootmgr_display->touches[i];
    }
    return NULL;
}

int bootmgr_check_touch(uint16_t x, uint16_t y)
{
    uint16_t i = 0;
    bootmgr_touch *c = NULL;
    for(; i < bootmgr_display->touch_count; ++i)
    {
        c = bootmgr_display->touches[i];
        if(c->x_min <= x && c->y_min <= y &&
           c->x_max >= x && c->y_max >= y)
        {
            int res = c->callback();
            if(res & TCALL_DELETE)
                bootmgr_erase_touch(c->id);
            return res;
        }
    }
    return TCALL_NONE;
}