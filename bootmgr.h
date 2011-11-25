#ifndef INIT_BOOT_MGR
#define INIT_BOOT_MGR

extern char bootmgr_selected;

//main
#define BOOTMGR_MAIN 1
#define BOOTMGR_SD_SEL 2
#define BOOTMGR_BACKUPS_MAX 128
void bootmgr_start(unsigned short timeout_seconds);
void bootmgr_show_rom_list();
unsigned char bootmgr_boot_sd(char *path);
void bootmgr_import_boot(char *path);
void bootmgr_remove_rc_mounts();

//keys
int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, unsigned dont_wait);
void *bootmgr_input_thread(void *cookie);
int bootmgr_get_last_key();
struct input_event;
unsigned char bootmgr_handle_key(int key);

//Drawing
void bootmgr_init_display();
void bootmgr_destroy_display();
int bootmgr_open_framebuffer();
void bootmgr_close_framebuffer();
int bootmgr_show_img(unsigned short start_x, unsigned short start_y, char *custom_img);
void bootmgr_set_lines_count(unsigned short c);
void bootmgr_set_fills_count(unsigned short c);
void bootmgr_set_imgs_count(uint16_t c);
void bootmgr_printf(short x, char line, uint16_t color, char *what, ...);
void bootmgr_print_img(short x, short y, char *name);
void bootmgr_print_fill(short x, short y, short width, short height, uint16_t color, char id);
inline void bootmgr_select(char line);
void bootmgr_draw();
void bootmgr_draw_text();
void bootmgr_draw_fills();
void bootmgr_draw_imgs();
void bootmgr_erase_text(char line);
void bootmgr_erase_fill(char id);

inline void _bootmgr_set_px(int x, int y, uint16_t color);
inline void _bootmgr_draw_char(char c, uint16_t x, uint16_t y, uint16_t color);

#define BOOTMGR_FILL_SELECT 1

#define BOOTMGR_DIS_W 320
#define BOOTMGR_DIS_H 480

typedef struct
{
    char *text;
    unsigned short x; // x y of top left corner
    unsigned short y;
    unsigned short color;
    char line;
} bootmgr_line;

typedef struct
{
    unsigned short x; // x y of top left corner
    unsigned short y;
    unsigned short height;
    unsigned short width;
    unsigned short color;
    char id;
} bootmgr_fill;

typedef struct
{
    unsigned short x; // x y of top left corner
    unsigned short y;
    char *name;
    char id;
} bootmgr_img;

typedef struct
{
    unsigned short ln_count;
    unsigned short fill_count;
    unsigned short img_count;

    unsigned char bg_img; //boolean
    bootmgr_line **lines;
    bootmgr_fill **fills;
    bootmgr_img **imgs;
} bootmgr_display_t;

bootmgr_display_t *bootmgr_display;

inline bootmgr_line *_bootmgr_new_line();
inline bootmgr_fill *_bootmgr_new_fill();
inline bootmgr_img *_bootmgr_new_img();
inline bootmgr_line *_bootmgr_get_line(char line);
inline bootmgr_fill *_bootmgr_get_fill(char id);

#endif