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

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"
#include "workers.h"
#include "log.h"
#include "containers.h"

struct worker
{
    void *data;
    worker_call call;
};

struct worker_thread
{
    pthread_t thread;
    pthread_mutex_t mutex;
    struct worker **workers;
    volatile int run;
};

static struct worker_thread worker_thread = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .workers = NULL,
    .run = 0,
};

#define SLEEP_CONST 10
static void *worker_thread_work(void *data)
{
    struct worker_thread *t = (struct worker_thread*)data;
    struct worker **w;

    struct timespec last, curr;
    uint32_t diff = 0, prev_sleep = 0;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while(t->run)
    {
        pthread_mutex_lock(&t->mutex);

        clock_gettime(CLOCK_MONOTONIC, &curr);
        diff = timespec_diff(&last, &curr);

        if(t->workers)
        {
            for(w = t->workers; *w; ++w)
                (*w)->call(diff, (*w)->data);
        }

        pthread_mutex_unlock(&t->mutex);

        last = curr;
        if(diff <= SLEEP_CONST+prev_sleep)
        {
            prev_sleep = SLEEP_CONST+prev_sleep-diff;
            usleep(prev_sleep*1000);
        }
        else
            prev_sleep = 0;
    }
    return NULL;
}

void workers_start(void)
{
    if(worker_thread.run != 0)
        return;

    worker_thread.run = 1;
    pthread_create(&worker_thread.thread, NULL, worker_thread_work, &worker_thread);
}

void workers_stop(void)
{
    if(worker_thread.run != 1)
        return;

    worker_thread.run = 0;
    pthread_join(worker_thread.thread, NULL);

    list_clear(&worker_thread.workers, &free);
}

void workers_add(worker_call call, void *data)
{
    if(worker_thread.run != 1)
    {
        ERROR("workers: adding worker when the thread isn't running'\n");
        return;
    }

    struct worker *w = mzalloc(sizeof(struct worker));
    w->call = call;
    w->data = data;

    pthread_mutex_lock(&worker_thread.mutex);
    list_add(&worker_thread.workers, w);
    pthread_mutex_unlock(&worker_thread.mutex);
}

void workers_remove(worker_call call, void *data)
{
    if(worker_thread.run != 1)
    {
        ERROR("workers: removing worker when the thread isn't running'\n");
        return;
    }

    pthread_mutex_lock(&worker_thread.mutex);
    if(worker_thread.workers)
    {
        int i;
        struct worker *w;
        for(i = 0; worker_thread.workers[i]; ++i)
        {
            w = worker_thread.workers[i];
            if(w->call == call && w->data == data)
            {
                list_rm_at(&worker_thread.workers, i, &free);
                break;
            }
        }
    }
    pthread_mutex_unlock(&worker_thread.mutex);
}

pthread_t workers_get_thread_id(void)
{
    return worker_thread.thread;
}
