#ifndef DEVICES_H
#define DEVICES_H

void devices_init(void);

#include <sys/stat.h>

extern void devices_close(void);
extern void handle_device_fd(void);
extern int add_dev_perms(const char *name, const char *attr,
                         mode_t perm, unsigned int uid,
                         unsigned int gid, unsigned short prefix);
int get_device_fd(void);

#endif