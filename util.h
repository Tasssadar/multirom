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

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static const char *coldboot_done = "/dev/.coldboot_done";

int mtd_name_to_number(const char *name);
int create_socket(const char *name, int type, mode_t perm,
                  uid_t uid, gid_t gid);
void *read_file(const char *fn, unsigned *_sz);
time_t gettime(void);
unsigned int decode_uid(const char *s);

int mkdir_recursive(const char *pathname, mode_t mode);
void sanitize(char *p);
int make_link(const char *oldpath, const char *newpath);
void remove_link(const char *oldpath, const char *newpath);
int wait_for_file(const char *filename, int timeout);
void open_devnull_stdio(void);
void get_hardware_name(char *hardware, unsigned int *revision);
void import_kernel_cmdline(int in_qemu, void (*import_kernel_nv)(char *name, int in_qemu));
int copy_file(const char *from, const char *to);
int mkdir_with_perms(const char *path, mode_t mode, const char *owner, const char *group);
int write_file(const char *path, const char *value);
int remove_dir(const char *dir);
int run_cmd(char **cmd);
char *run_get_stdout(char **cmd);

char *parse_string(char *src);

// auto-conversion of pointer type occurs only for
// void*, not for void** nor void***
typedef void* ptrToList;
typedef void* listItself;
typedef void* callback;
typedef void(*callbackPtr)(void*);

void list_add(void *item, ptrToList list_p);
int list_rm(void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_noreorder(void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_opt(int reorder, void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_at(int idx, ptrToList list_p, callback destroy_callback_p);
int list_size(listItself list);
int list_item_count(listItself list);
int list_copy(listItself src, ptrToList dest_p);
int list_move(ptrToList source_p, ptrToList dest_p);
void list_clear(ptrToList list_p, callback destroy_callback_p);

inline int in_rect(int x, int y, int rx, int ry, int rw, int rh);

typedef struct
{
    char **keys;
    void **values;
} map;

map *map_create(void);
void map_destroy(map *m, void (*destroy_callback)(void*));
void map_add(map *m, char *key, void *val, void (*destroy_callback)(void*));
void map_add_not_exist(map *m, char *key, void *val);
void map_rm(map *m, char *key, void (*destroy_callback)(void*));
int map_find(map *m, char *key);
void *map_get_val(map *m, char *key);
void *map_get_ref(map *m, char *key);

#endif
