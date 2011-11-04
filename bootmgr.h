#ifndef INIT_BOOT_MGR
#define INIT_BOOT_MGR

extern char bootmgr_selected;
struct input_event;

void bootmgr_start(unsigned short timeout_seconds);
int bootmgr_get_last_key();
int ev_init(void);
void ev_exit(void);
int ev_get(struct input_event *ev, unsigned dont_wait);
int bootmgr_open_framebuffer();
void bootmgr_close_framebuffer();
int bootmgr_show_img();

#endif