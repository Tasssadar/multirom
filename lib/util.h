/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _INIT_UTIL_H_
#define _INIT_UTIL_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#define UNUSED __attribute__((unused))

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#define REBOOT_SYSTEM 0
#define REBOOT_RECOVERY 1
#define REBOOT_BOOTLOADER 2
#define REBOOT_SHUTDOWN 3

time_t gettime(void);
unsigned int decode_uid(const char *s);
int mkdir_recursive(const char *pathname, mode_t mode);
int mkdir_recursive_with_perms(const char *pathname, mode_t mode, const char *owner, const char *group);
void sanitize(char *p);
int make_link(const char *oldpath, const char *newpath);
void remove_link(const char *oldpath, const char *newpath);
int wait_for_file(const char *filename, int timeout);
int copy_file(const char *from, const char *to);
int copy_dir(const char *from, const char *to);
int mkdir_with_perms(const char *path, mode_t mode, const char *owner, const char *group);
int write_file(const char *path, const char *value);
int remove_dir(const char *dir);
int run_cmd(char **cmd);
int run_cmd_with_env(char **cmd, char *const *envp);
char *run_get_stdout(char **cmd);
char *run_get_stdout_with_exit(char **cmd, int *exit_code);
char *run_get_stdout_with_exit_with_env(char **cmd, int *exit_code, char *const *envp);
char *readlink_recursive(const char *link);
void stdio_to_null();
char *parse_string(char *src);
uint32_t timespec_diff(struct timespec *f, struct timespec *s);
inline int64_t timeval_us_diff(struct timeval now, struct timeval prev);
void emergency_remount_ro(void);
int create_loop_device(const char *dev_path, const char *img_path, int loop_num, int loop_chmod);
int mount_image(const char *src, const char *dst, const char *fs, int flags, const void *data);
void do_reboot(int type);
int mr_system(const char *shell_fmt, ...);

inline int imin(int a, int b);
inline int imax(int a, int b);
inline int iabs(int a);
inline int in_rect(int x, int y, int rx, int ry, int rw, int rh);

inline void *mzalloc(size_t size); // alloc and fill with 0s
char *strtoupper(const char *str);
int strstartswith(const char *haystack, const char *needle);
int strendswith(const char *haystack, const char *needle);

#endif
