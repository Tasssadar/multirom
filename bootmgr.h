#ifndef INIT_BOOT_MGR
#define INIT_BOOT_MGR

extern int8_t bootmgr_selected;
extern struct FB fb;
extern uint8_t disable_lg_charger;

static const char *battery_pct = "/sys/class/power_supply/battery/capacity";
static const char *battery_status = "/sys/class/power_supply/battery/status";

#define WHITE 0xFFFF
#define BLACK 0x0000

#define BOOTMGR_BACKUPS_MAX 128

enum _bootmgr_sections
{
    BOOTMGR_MAIN,
    BOOTMGR_SD_SEL,
    BOOTMGR_TETRIS,
    BOOTMGR_UMS,
    BOOTMGR_CHARGER,
    BOOTMGR_MISC,
};

enum _bootmgr_touch_res
{
    TCALL_NONE     = 0x00,
    TCALL_DELETE   = 0x01,
    TCALL_EXIT_MGR = 0x02,
};

enum _bootmgr_charger_settings
{
    CHARGER_AUTO_START = 0x01,
    CHARGER_DISABLE_LG = 0x02
};

//main
void bootmgr_start(int charger);
void bootmgr_exit(void);
uint8_t bootmgr_show_rom_list(void);
void bootmgr_boot_internal(void);
uint8_t bootmgr_boot_sd(void);
void bootmgr_import_boot(char *path);
void bootmgr_remove_rc_mounts(void);
void *bootmgr_time_thread(void *cookie);
inline void bootmgr_set_time_thread(uint8_t start);
int8_t bootmgr_get_file(char *name, char *buffer, uint8_t len);
uint8_t bootmgr_toggle_ums(void);
inline void __bootmgr_boot(void);
void bootmgr_load_settings(void);
void bootmgr_save_settings(void);
int bootmgr_toggle_sdcard(uint8_t on, uint8_t mknod_only);
void bootmgr_set_brightness(uint8_t pct);
void bootmgr_set_brightness_helper(uint16_t value);
void bootmgr_do_sleep(char on);
uint8_t bootmgr_boot_sd_auto(void);

//charger
void bootmgr_charger_init(void);
void bootmgr_charger_destroy(void);
uint8_t bootmgr_charger_key(int key);
void bootmgr_update_battery_status(void);
void bootmgr_update_battery_fill(int pct);
void *bootmgr_charger_thread(void *cookie);
int bootmgr_get_battery_pct(void);

//misc
void bootmgr_misc_init(void);
void bootmgr_misc_destroy(void);
uint8_t bootmgr_misc_key(int key);
void bootmgr_misc_draw_items(void);

inline int bootmgr_misc_charger(void);
inline int bootmgr_misc_tetris(void);
inline int bootmgr_misc_reboot(void);
inline int bootmgr_misc_recovery(void);
inline int bootmgr_misc_bootloader(void);

static int (*misc_callbacks[])(void) = {
    bootmgr_misc_charger,
    bootmgr_misc_tetris,
    bootmgr_misc_reboot,
    bootmgr_misc_recovery,
    bootmgr_misc_bootloader,
    NULL
};

//keys
int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, unsigned dont_wait);
void *bootmgr_input_thread(void *cookie);
int bootmgr_get_last_key(void);
struct input_event;
uint8_t bootmgr_handle_key(int key);
uint8_t bootmgr_get_last_touch(uint16_t *x, uint16_t *y);
void bootmgr_setup_touch(void);
void bootmgr_reset_input_iters(void);

//Drawing
void bootmgr_init_display(void);
void bootmgr_destroy_display(void);
int bootmgr_open_framebuffer(void);
void bootmgr_close_framebuffer(void);
int bootmgr_show_img(uint16_t start_x, uint16_t start_y, char *custom_img);
void bootmgr_main_draw_sel(void);
void bootmgr_set_lines_count(uint16_t c);
void bootmgr_set_fills_count(uint16_t c);
void bootmgr_set_imgs_count(uint16_t c);
void bootmgr_set_touches_count(uint16_t c);
void bootmgr_printf(short x, uint8_t line, uint16_t color, char *what, ...);
void bootmgr_print_img(int16_t x, int16_t y, char *name, int8_t id);
void bootmgr_print_fill(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color, int8_t id);
void bootmgr_add_touch(uint16_t x_min, uint16_t y_min, uint16_t x_max, uint16_t y_max, int (*callback)(void), int8_t id);
inline void bootmgr_select(int8_t line);
void bootmgr_draw(void);
void bootmgr_draw_text(void);
void bootmgr_draw_fills(void);
void bootmgr_draw_imgs(void);
void bootmgr_erase_text(uint8_t line);
void bootmgr_erase_fill(int8_t id);
void bootmgr_erase_touch(int8_t id);
int bootmgr_check_touch(uint16_t x, uint16_t y);
void bootmgr_fill_fb_black(void);

inline void _bootmgr_set_px(uint16_t x, uint16_t y, uint16_t color);
inline void _bootmgr_draw_char(char c, uint16_t x, uint16_t y, uint16_t color);
inline void bootmgr_clear(void);

inline int bootmgr_touch_int(void);
inline int bootmgr_touch_sd(void);
inline int bootmgr_touch_exit_ums(void);
inline int bootmgr_touch_sd_up(void);
inline int bootmgr_touch_sd_down(void);
inline int bootmgr_touch_sd_select(void);
inline int bootmgr_touch_sd_exit(void);
inline int bootmgr_touch_ums(void);
inline int bootmgr_touch_misc(void);
inline int bootmgr_touch_exit_charger(void);
inline int bootmgr_touch_exit_misc(void);

#define BOOTMGR_FILL_SELECT 1

#define BOOTMGR_DIS_W 320
#define BOOTMGR_DIS_H 480

typedef struct
{
    char *text;
    uint16_t x; // x y of top left corner
    uint16_t color;
    uint8_t line;
} bootmgr_line;

typedef struct
{
    uint16_t x; // x y of top left corner
    uint16_t y;
    uint16_t height;
    uint16_t width;
    uint16_t color;
    int8_t id;
} bootmgr_fill;

typedef struct
{
    uint16_t x; // x y of top left corner
    uint16_t y;
    char *name;
    int8_t id;
    uint8_t show;
} bootmgr_img;

typedef struct
{
    uint16_t x_min;
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
    int8_t id;
    int (*callback)(void);
} bootmgr_touch;

typedef struct
{
    uint16_t ln_count;
    uint16_t fill_count;
    uint16_t img_count;
    uint16_t touch_count;

    uint8_t bg_img; //boolean
    bootmgr_line **lines;
    bootmgr_fill **fills;
    bootmgr_img **imgs;
    bootmgr_touch **touches;
} bootmgr_display_t;

extern bootmgr_display_t *bootmgr_display;

inline bootmgr_line *_bootmgr_new_line(void);
inline bootmgr_fill *_bootmgr_new_fill(void);
inline bootmgr_touch *_bootmgr_new_touch(void);
inline bootmgr_img *_bootmgr_new_img(void);
inline bootmgr_line *_bootmgr_get_line(uint8_t line);
inline bootmgr_fill *_bootmgr_get_fill(int8_t id);
inline bootmgr_touch *_bootmgr_get_touch(int8_t id);
inline bootmgr_img *_bootmgr_get_img(int8_t id);

typedef struct
{
    int8_t timezone;
    int8_t timezone_mins;
    int8_t timeout_seconds;
    uint8_t show_seconds;
    uint8_t touch_ui;
    uint32_t tetris_max_score;
    uint8_t brightness;
    uint8_t default_boot;
    char *default_boot_sd;
    uint8_t charger_settings;
} bootmgr_settings_t;

#endif