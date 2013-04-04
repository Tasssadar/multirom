#ifndef ADB_H
#define ADB_H

void adb_init(void);
void adb_quit(void);
void adb_init_usb(void);
int adb_init_busybox(void);
void adb_init_fs(void);
void adb_cleanup(void);

#endif