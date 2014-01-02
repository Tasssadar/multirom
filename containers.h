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

void list_add(void *item, ptrToList list_p);
int list_add_from_list(listItself src_p, ptrToList list_p);
int list_rm(void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_noreorder(void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_opt(int reorder, void *item, ptrToList list_p, callback destroy_callback_p);
int list_rm_at(int idx, ptrToList list_p, callback destroy_callback_p);
int list_size(listItself list);
int list_item_count(listItself list);
int list_copy(listItself src, ptrToList dest_p);
int list_move(ptrToList source_p, ptrToList dest_p);
void list_clear(ptrToList list_p, callback destroy_callback_p);
void list_swap(ptrToList a_p, ptrToList b_p);

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
