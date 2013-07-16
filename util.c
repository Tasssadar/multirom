/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>

#ifdef HAVE_SELINUX
#include <selinux/label.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

/* for ANDROID_SOCKET_* */
#include <cutils/sockets.h>

#include <private/android_filesystem_config.h>

#include "log.h"
#include "util.h"

/*
 * android_name_to_id - returns the integer uid/gid associated with the given
 * name, or -1U on error.
 */
static unsigned int android_name_to_id(const char *name)
{
    struct android_id_info const *info = android_ids;
    unsigned int n;

    for (n = 0; n < android_id_count; n++) {
        if (!strcmp(info[n].name, name))
            return info[n].aid;
    }

    return -1U;
}

/*
 * decode_uid - decodes and returns the given string, which can be either the
 * numeric or name representation, into the integer uid or gid. Returns -1U on
 * error.
 */
unsigned int decode_uid(const char *s)
{
    unsigned int v;

    if (!s || *s == '\0')
        return -1U;
    if (isalpha(s[0]))
        return android_name_to_id(s);

    errno = 0;
    v = (unsigned int) strtoul(s, 0, 0);
    if (errno)
        return -1U;
    return v;
}

/*
 * create_socket - creates a Unix domain socket in ANDROID_SOCKET_DIR
 * ("/dev/socket") as dictated in init.rc. This socket is inherited by the
 * daemon. We communicate the file descriptor's value via the environment
 * variable ANDROID_SOCKET_ENV_PREFIX<name> ("ANDROID_SOCKET_foo").
 */
int create_socket(const char *name, int type, mode_t perm, uid_t uid, gid_t gid)
{
    struct sockaddr_un addr;
    int fd, ret;
#ifdef HAVE_SELINUX
    char *secon;
#endif

    fd = socket(PF_UNIX, type, 0);
    if (fd < 0) {
        ERROR("Failed to open socket '%s': %s\n", name, strerror(errno));
        return -1;
    }

    memset(&addr, 0 , sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), ANDROID_SOCKET_DIR"/%s",
             name);

    ret = unlink(addr.sun_path);
    if (ret != 0 && errno != ENOENT) {
        ERROR("Failed to unlink old socket '%s': %s\n", name, strerror(errno));
        goto out_close;
    }

#ifdef HAVE_SELINUX
    secon = NULL;
    if (sehandle) {
        ret = selabel_lookup(sehandle, &secon, addr.sun_path, S_IFSOCK);
        if (ret == 0)
            setfscreatecon(secon);
    }
#endif

    ret = bind(fd, (struct sockaddr *) &addr, sizeof (addr));
    if (ret) {
        ERROR("Failed to bind socket '%s': %s\n", name, strerror(errno));
        goto out_unlink;
    }

#ifdef HAVE_SELINUX
    setfscreatecon(NULL);
    freecon(secon);
#endif

    chown(addr.sun_path, uid, gid);
    chmod(addr.sun_path, perm);

    INFO("Created socket '%s' with mode '%o', user '%d', group '%d'\n",
         addr.sun_path, perm, uid, gid);

    return fd;

out_unlink:
    unlink(addr.sun_path);
out_close:
    close(fd);
    return -1;
}

/* reads a file, making sure it is terminated with \n \0 */
void *read_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;
    struct stat sb;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    // for security reasons, disallow world-writable
    // or group-writable files
    if (fstat(fd, &sb) < 0) {
        ERROR("fstat failed for '%s'\n", fn);
        goto oops;
    }
    if ((sb.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        ERROR("skipping insecure file '%s'\n", fn);
        goto oops;
    }

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz + 2);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);
    data[sz] = '\n';
    data[sz+1] = 0;
    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

#define MAX_MTD_PARTITIONS 16

static struct {
    char name[16];
    int number;
} mtd_part_map[MAX_MTD_PARTITIONS];

static int mtd_part_count = -1;

static void find_mtd_partitions(void)
{
    int fd;
    char buf[1024];
    char *pmtdbufp;
    ssize_t pmtdsize;
    int r;

    fd = open("/proc/mtd", O_RDONLY);
    if (fd < 0)
        return;

    buf[sizeof(buf) - 1] = '\0';
    pmtdsize = read(fd, buf, sizeof(buf) - 1);
    pmtdbufp = buf;
    while (pmtdsize > 0) {
        int mtdnum, mtdsize, mtderasesize;
        char mtdname[16];
        mtdname[0] = '\0';
        mtdnum = -1;
        r = sscanf(pmtdbufp, "mtd%d: %x %x %15s",
                   &mtdnum, &mtdsize, &mtderasesize, mtdname);
        if ((r == 4) && (mtdname[0] == '"')) {
            char *x = strchr(mtdname + 1, '"');
            if (x) {
                *x = 0;
            }
            INFO("mtd partition %d, %s\n", mtdnum, mtdname + 1);
            if (mtd_part_count < MAX_MTD_PARTITIONS) {
                strcpy(mtd_part_map[mtd_part_count].name, mtdname + 1);
                mtd_part_map[mtd_part_count].number = mtdnum;
                mtd_part_count++;
            } else {
                ERROR("too many mtd partitions\n");
            }
        }
        while (pmtdsize > 0 && *pmtdbufp != '\n') {
            pmtdbufp++;
            pmtdsize--;
        }
        if (pmtdsize > 0) {
            pmtdbufp++;
            pmtdsize--;
        }
    }
    close(fd);
}

int mtd_name_to_number(const char *name)
{
    int n;
    if (mtd_part_count < 0) {
        mtd_part_count = 0;
        find_mtd_partitions();
    }
    for (n = 0; n < mtd_part_count; n++) {
        if (!strcmp(name, mtd_part_map[n].name)) {
            return mtd_part_map[n].number;
        }
    }
    return -1;
}

/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
time_t gettime(void)
{
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        ERROR("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
}

int mkdir_recursive(const char *pathname, mode_t mode)
{
    char buf[128];
    const char *slash;
    const char *p = pathname;
    int width;
    int ret;
    struct stat info;

    while ((slash = strchr(p, '/')) != NULL) {
        width = slash - pathname;
        p = slash + 1;
        if (width < 0)
            break;
        if (width == 0)
            continue;
        if ((unsigned int)width > sizeof(buf) - 1) {
            ERROR("path too long for mkdir_recursive\n");
            return -1;
        }
        memcpy(buf, pathname, width);
        buf[width] = 0;
        if (stat(buf, &info) != 0) {
            ret = mkdir(buf, mode);
            if (ret && errno != EEXIST)
                return ret;
        }
    }
    ret = mkdir(pathname, mode);
    if (ret && errno != EEXIST)
        return ret;
    return 0;
}

void sanitize(char *s)
{
    if (!s)
        return;
    while (isalnum(*s))
        s++;
    *s = 0;
}

int make_link(const char *oldpath, const char *newpath)
{
    int ret;
    char buf[256];
    char *slash;
    int width;

    slash = strrchr(newpath, '/');
    if (!slash)
        return -1;

    width = slash - newpath;
    if (width <= 0 || width > (int)sizeof(buf) - 1)
        return -1;

    memcpy(buf, newpath, width);
    buf[width] = 0;
    ret = mkdir_recursive(buf, 0755);
    if (ret)
    {
        ERROR("Failed to create directory %s: %s (%d)\n", buf, strerror(errno), errno);
        return -1;
    }

    ret = symlink(oldpath, newpath);
    if (ret && errno != EEXIST)
    {
        ERROR("Failed to symlink %s to %s: %s (%d)\n", oldpath, newpath, strerror(errno), errno);
        return -1;
    }
    return 0;
}

void remove_link(const char *oldpath, const char *newpath)
{
    char path[256];
    ssize_t ret;
    ret = readlink(newpath, path, sizeof(path) - 1);
    if (ret <= 0)
        return;
    path[ret] = 0;
    if (!strcmp(path, oldpath))
        unlink(newpath);
}

int wait_for_file(const char *filename, int timeout)
{
    struct stat info;
    time_t timeout_time = gettime() + timeout;
    int ret = -1;

    while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
        usleep(10000);

    return ret;
}

void open_devnull_stdio(void)
{
    int fd;
    static const char *name = "/dev/__null__";
    if (mknod(name, S_IFCHR | 0600, (1 << 8) | 3) == 0) {
        fd = open(name, O_RDWR);
        unlink(name);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2) {
                close(fd);
            }
            return;
        }
    }

    exit(1);
}

void get_hardware_name(char *hardware, unsigned int *revision)
{
    char data[1024];
    int fd, n;
    char *x, *hw, *rev;

    /* Hardware string was provided on kernel command line */
    if (hardware[0])
        return;

    fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd < 0) return;

    n = read(fd, data, 1023);
    close(fd);
    if (n < 0) return;

    data[n] = 0;
    hw = strstr(data, "\nHardware");
    rev = strstr(data, "\nRevision");

    if (hw) {
        x = strstr(hw, ": ");
        if (x) {
            x += 2;
            n = 0;
            while (*x && *x != '\n') {
                if (!isspace(*x))
                    hardware[n++] = tolower(*x);
                x++;
                if (n == 31) break;
            }
            hardware[n] = 0;
        }
    }

    if (rev) {
        x = strstr(rev, ": ");
        if (x) {
            *revision = strtoul(x + 2, 0, 16);
        }
    }
}

void import_kernel_cmdline(int in_qemu,
                           void (*import_kernel_nv)(char *name, int in_qemu))
{
    char cmdline[1024];
    char *ptr;
    int fd;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, cmdline, 1023);
        if (n < 0) n = 0;

        /* get rid of trailing newline, it happens */
        if (n > 0 && cmdline[n-1] == '\n') n--;

        cmdline[n] = 0;
        close(fd);
    } else {
        cmdline[0] = 0;
    }

    ptr = cmdline;
    while (ptr && *ptr) {
        char *x = strchr(ptr, ' ');
        if (x != 0) *x++ = 0;
        import_kernel_nv(ptr, in_qemu);
        ptr = x;
    }
}

int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "r");
    if(!in)
        return -1;

    FILE *out = fopen(to, "w");
    if(!out)
    {
        fclose(in);
        return -1;
    }

    fseek(in, 0, SEEK_END);
    int size = ftell(in);
    rewind(in);

    char *buff = malloc(size);
    fread(buff, 1, size, in);
    fwrite(buff, 1, size, out);

    fclose(in);
    fclose(out);
    free(buff);
    return 0;
}

int mkdir_with_perms(const char *path, mode_t mode, const char *owner, const char *group)
{
    int ret;

    ret = mkdir(path, mode);
    /* chmod in case the directory already exists */
    if (ret == -1 && errno == EEXIST) {
        ret = chmod(path, mode);
    }
    if (ret == -1) {
        return -errno;
    }

    if(owner)
    {
        uid_t uid = decode_uid(owner);
        gid_t gid = -1;

        if(group)
            gid = decode_uid(group);

        if(chown(path, uid, gid) < 0)
            return -errno;
    }
    return 0;
}

int write_file(const char *path, const char *value)
{
    int fd, ret, len;

    fd = open(path, O_WRONLY|O_CREAT, 0622);

    if (fd < 0)
    {
        ERROR("Failed to open file %s (%d: %s)\n", path, errno, strerror(errno));
        return -errno;
    }

    len = strlen(value);

    do {
        ret = write(fd, value, len);
    } while (ret < 0 && errno == EINTR);

    close(fd);
    if (ret < 0) {
        return -errno;
    } else {
        return 0;
    }
}

int remove_dir(const char *dir)
{
    struct DIR *d = opendir(dir);
    if(!dir)
        return -1;

    struct dirent *dt;
    int res = 0;

    int dir_len = strlen(dir) + 1;
    char *n = malloc(dir_len + 1);
    strcpy(n, dir);
    strcat(n, "/");

    while(res == 0 && (dt = readdir(d)))
    {
        if(dt->d_name[0] == '.' && (dt->d_name[1] == '.' || dt->d_name[1] == 0))
            continue;

        n = realloc(n, dir_len + strlen(dt->d_name) + 1);
        n[dir_len] = 0;
        strcat(n, dt->d_name);

        if(dt->d_type == DT_DIR)
        {
            if(remove_dir(n) < 0)
                res = -1;
        }
        else
        {
            if(remove(n) < 0)
                res = -1;
        }
    }

    free(n);
    closedir(d);

    if(res == 0 && remove(dir) < 0)
        res = -1;
    return res;
}

int run_cmd(char **cmd)
{
    pid_t pID = fork();
    if(pID == 0)
    {
        int res = execve(cmd[0], cmd, NULL);
        ERROR("exec failed %d %d %s\n", res, errno, strerror(errno));
        _exit(127);
    }
    int status = 0;
    while(waitpid(pID, &status, WNOHANG) == 0) { usleep(300000); }
    return status;
}

char *run_get_stdout(char **cmd)
{
   int fd[2];
   if(pipe(fd) < 0)
        return NULL;

    pid_t pid = fork();
    if (pid < 0)
    {
        close(fd[0]);
        close(fd[1]);
        return NULL;
    }

    if(pid == 0) // child
    {
        close(fd[0]);
        dup2(fd[1], 1);  // send stdout to the pipe
        dup2(fd[1], 2);  // send stderr to the pipe
        close(fd[1]);

        execv(cmd[0], cmd);
        ERROR("execv failed: %d %s\n", errno, strerror(errno));
        exit(0);
    }
    else
    {
        close(fd[1]);

        char *res = malloc(512);
        char buffer[512];
        int size = 512, written = 0, len;
        while ((len = read(fd[0], buffer, sizeof(buffer))) > 0)
        {
            if(written + len + 1 > size)
            {
                size = written + len + 256;
                res = realloc(res, size);
            }
            memcpy(res+written, buffer, len);
            written += len;
            res[written] = 0;
        }

        if(written == 0)
        {
            free(res);
            return NULL;
        }
        return res;
    }
    return NULL;
}

uint32_t timespec_diff(struct timespec *f, struct timespec *s)
{
    uint32_t res = 0;
    if(s->tv_nsec-f->tv_nsec < 0)
    {
        res = (s->tv_sec-f->tv_sec-1)*1000;
        res += 1000 + ((s->tv_nsec-f->tv_nsec)/1000000);
    }
    else
    {
        res = (s->tv_sec-f->tv_sec)*1000;
        res += (s->tv_nsec-f->tv_nsec)/1000000;
    }
    return res;
}

int list_item_count(listItself list)
{
    void **l = (void**)list;
    int i = 0;
    while(l && l[i])
        ++i;
    return i;
}

int list_size(listItself list)
{
    return list_item_count(list)+1;
}

void list_add(void *item, ptrToList list_p)
{
    void ***list = (void***)list_p;

    int i = 0;
    while(*list && (*list)[i])
        ++i;
    i += 2;

    *list = realloc(*list, i*sizeof(item));

    (*list)[--i] = NULL;
    (*list)[--i] = item;
}

int list_rm_opt(int reorder, void *item, ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    int size = list_size(*list);

    int i;
    for(i = 0; *list && (*list)[i]; ++i)
    {
        if((*list)[i] != item)
            continue;

        if(destroy_callback)
            (*destroy_callback)(item);

        --size;
        if(size == 1)
        {
            free(*list);
            *list = NULL;
            return 0;
        }

        if(i != size-1)
        {
            if(reorder)
                (*list)[i] = (*list)[size-1];
            else
            {
                for(; *list && (*list)[i]; ++i)
                    (*list)[i] = (*list)[i+1];
            }
        }

        *list= realloc(*list, size*sizeof(item));
        (*list)[size-1] = NULL;
        return 0;
    }
    return -1;
}

int list_rm(void *item, ptrToList list_p, callback destroy_callback_p)
{
    return list_rm_opt(1, item, list_p, destroy_callback_p);
}

int list_rm_noreorder(void *item, ptrToList list_p, callback destroy_callback_p)
{
    return list_rm_opt(0, item, list_p, destroy_callback_p);
}

int list_rm_at(int idx, ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    int size = list_size(*list);
    if(idx < 0 || idx >= size-1)
        return -1;

    void *item = (*list)[idx];
    if(destroy_callback)
        (*destroy_callback)(item);

    --size;
    if(size == 1)
    {
        free(*list);
        *list = NULL;
        return 0;
    }

    int i = idx;
    for(; i+1 < size; ++i)
        (*list)[i] = (*list)[i+1];

    *list= realloc(*list, size*sizeof(item));
    return 0;
}

void list_clear(ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    if(*list == NULL)
        return;

    if(destroy_callback)
    {
        int i;
        for(i = 0; *list && (*list)[i]; ++i)
            (*destroy_callback)((*list)[i]);
    }

    free(*list);
    *list = NULL;
}

int list_copy(listItself src, ptrToList dest_p)
{
    void **source = (void**)src;
    void ***dest = (void***)dest_p;

    if(!source)
        return 0;

    if(*dest)
        return -1;

    int size = list_size(source);
    *dest = calloc(size, sizeof(*source));

    int i;
    for(i = 0; source[i]; ++i)
        (*dest)[i] = source[i];
    return 0;
}

int list_move(ptrToList source_p, ptrToList dest_p)
{
    void ***source = (void***)source_p;
    void ***dest = (void***)dest_p;

    if(!source)
        return 0;

    if(*dest)
        return -1;

    list_copy(*source, dest);
    list_clear(source, NULL);
    return 0;
}

int in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    if(x < rx || y < ry)
        return 0;

    if(x > rx+rw || y > ry+rh)
        return 0;
    return 1;
}

char *parse_string(char *src)
{
    char *start = strchr(src, '"');
    char *end = strrchr(src, '"');

    if(!start || start == end || start+1 == end)
        return NULL;
    ++start;
    return strndup(start, end-start);
}

map *map_create(void)
{
    map *m = malloc(sizeof(map));
    memset(m, 0, sizeof(map));
    return m;
}

void map_destroy(map *m, void (*destroy_callback)(void*))
{
    if(!m)
        return;

    list_clear(&m->keys, &free);
    list_clear(&m->values, destroy_callback);
    free(m);
}

void map_add(map *m, char *key, void *val, void (*destroy_callback)(void*))
{
    int idx = map_find(m, key);
    if(idx >= 0)
    {
        if(destroy_callback)
            (*destroy_callback)(m->values[idx]);
        m->values[idx] = val;
    }
    else
        map_add_not_exist(m, key, val);
}

void map_add_not_exist(map *m, char *key, void *val)
{
    list_add(strdup(key), &m->keys);
    list_add(val, &m->values);
}

void map_rm(map *m, char *key, void (*destroy_callback)(void*))
{
    int idx = map_find(m, key);
    if(idx < 0)
        return;

    list_rm_at(idx, &m->keys, &free);
    list_rm_at(idx, &m->values, destroy_callback);
}

int map_find(map *m, char *key)
{
    int i;
    for(i = 0; m->keys && m->keys[i]; ++i)
        if(strcmp(m->keys[i], key) == 0)
            return i;
    return -1; 
}

void *map_get_val(map *m, char *key)
{
    int idx = map_find(m, key);
    if(idx < 0)
        return NULL;
    return m->values[idx];
}

void *map_get_ref(map *m, char *key)
{
    int idx = map_find(m, key);
    if(idx < 0)
        return NULL;
    return &m->values[idx];
}

// alloc and fill with 0s
void *mzalloc(size_t size)
{
    void *res = malloc(size);
    memset(res, 0, size);
    return res;
}
