#ifndef BOOTMGR_SHARED_H
#define BOOTMGR_SHARED_H

#define ISO_CHAR_MIN    0x00
#define ISO_CHAR_MAX    0xFF
#define ISO_CHAR_HEIGHT 16

bootmgr_display_t *bootmgr_display;
extern bootmgr_settings_t settings;
extern pthread_mutex_t *bootmgr_input_mutex;
extern volatile uint8_t bootmgr_input_run;
extern uint8_t bootmgr_phase;
extern int8_t selected;
extern uint8_t total_backups;
extern uint8_t backups_has_active;
extern int8_t bootmgr_touch_itr;
extern int8_t bootmgr_key_itr;
extern char sleep_mode;

#endif
