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

#ifndef CONTAINERS_H
#define CONTAINERS_H

// auto-conversion of pointer type occurs only for
// void*, not for void** nor void***
typedef void* ptrToList; // void ***
typedef void* listItself; // void **
typedef void* callback;
typedef void(*callbackPtr)(void*);

void list_add(ptrToList list_p, void *item);
void list_add_at(ptrToList list_p, int idx, void *item);
int list_add_from_list(ptrToList list_p, listItself src_p);
int list_rm(ptrToList list_p, void *item, callback destroy_callback_p);
int list_rm_noreorder(ptrToList list_p, void *item, callback destroy_callback_p);
int list_rm_opt(ptrToList list_p, void *item, callback destroy_callback_p, int reorder);
int list_rm_at(ptrToList list_p, int idx, callback destroy_callback_p);
int list_size(listItself list);
int list_item_count(listItself list);
int list_copy(ptrToList dest_p, listItself src);
int list_move(ptrToList dest_p, ptrToList source_p);
void list_clear(ptrToList list_p, callback destroy_callback_p);
void list_swap(ptrToList a_p, ptrToList b_p);

typedef struct
{
    char **keys;
    void **values;
    size_t size;
} map;

map *map_create(void);
void map_destroy(map *m, void (*destroy_callback)(void*));
void map_add(map *m, const char *key, void *val, void (*destroy_callback)(void*));
void map_add_not_exist(map *m, const char *key, void *val);
void map_rm(map *m, const char *key, void (*destroy_callback)(void*));
int map_find(map *m, const char *key);
void *map_get_val(map *m, const char *key);
void *map_get_ref(map *m, const char *key);

typedef struct
{
    int *keys;
    void **values;
    size_t size;
} imap;

imap *imap_create(void);
void imap_destroy(imap *m, void (*destroy_callback)(void*));
void imap_add(imap *m, int key, void *val, void (*destroy_callback)(void*));
void imap_add_not_exist(imap *m, int key, void *val);
void imap_rm(imap *m, int key, void (*destroy_callback)(void*));
int imap_find(imap *m, int key);
void *imap_get_val(imap *m, int key);
void *imap_get_ref(imap *m, int key);

#endif
