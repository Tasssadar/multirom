/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WORKERS_H
#define WORKERS_H

#include <stdint.h>
#include <pthread.h>

typedef void (*worker_call)(uint32_t, void *); // ms_diff, data

void workers_start(void);
void workers_stop(void);
void workers_add(worker_call call, void *data);
void workers_remove(worker_call call, void *data);
pthread_t workers_get_thread_id(void);

#endif
