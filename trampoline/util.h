#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>

time_t gettime(void);
int mkdir_recursive(const char *pathname, mode_t mode);
void sanitize(char *p);
void make_link(const char *oldpath, const char *newpath);
void remove_link(const char *oldpath, const char *newpath);
int wait_for_file(const char *filename, int timeout);

#endif
