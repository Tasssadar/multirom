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
#define _GNU_SOURCE

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <cutils/android_reboot.h>
#include <unistd.h>

#ifdef HAVE_SELINUX
#include <selinux/label.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <linux/loop.h>

#include <private/android_filesystem_config.h>

#include "log.h"
#include "util.h"
#include "mrom_data.h"

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

int mkdir_recursive(const char *pathname, mode_t mode)
{
    return mkdir_recursive_with_perms(pathname, mode, NULL, NULL);
}

int mkdir_recursive_with_perms(const char *pathname, mode_t mode, const char *owner, const char *group)
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
            ret = mkdir_with_perms(buf, mode, owner, group);
            if (ret && errno != EEXIST)
                return ret;
        }
    }
    ret = mkdir(pathname, mode);
    if (ret && errno != EEXIST)
        return ret;
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

/*
* replaces any unacceptable characters with '_', the
* length of the resulting string is equal to the input string
*/
void sanitize(char *s)
{
    const char* accept =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "_-.";

    if (!s)
        return;

    for (; *s; s++) {
        s += strspn(s, accept);
        if (*s) *s = '_';
    }
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

int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "re");
    if(!in)
        return -1;

    FILE *out = fopen(to, "we");
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

int write_file(const char *path, const char *value)
{
    int fd, ret, len;

    fd = open(path, O_WRONLY|O_CREAT|O_CLOEXEC, 0622);

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
    if(!d)
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

void stdio_to_null(void)
{
    int fd = open("/dev/null", O_RDWR|O_CLOEXEC);
    if(fd >= 0)
    {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
}

int run_cmd(char **cmd)
{
    return run_cmd_with_env(cmd, NULL);
}

int run_cmd_with_env(char **cmd, char *const *envp)
{
    pid_t pID = vfork();
    if(pID == 0)
    {
        stdio_to_null();
        execve(cmd[0], cmd, envp);
        _exit(127);
    }
    else
    {
        int status = 0;
        waitpid(pID, &status, 0);
        return status;
    }
}


char *run_get_stdout(char **cmd)
{
    int exit_code;
    return run_get_stdout_with_exit(cmd, &exit_code);
}

char *run_get_stdout_with_exit(char **cmd, int *exit_code)
{
    return run_get_stdout_with_exit_with_env(cmd, exit_code, NULL);
}

char *run_get_stdout_with_exit_with_env(char **cmd, int *exit_code, char *const *envp)
{
   int fd[2];
   if(pipe2(fd, O_CLOEXEC) < 0)
        return NULL;

    pid_t pid = vfork();
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

        execve(cmd[0], cmd, envp);
        _exit(127);
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

        close(fd[0]);

        waitpid(pid, exit_code, 0);

        if(written == 0)
        {
            free(res);
            return NULL;
        }
        return res;
    }
    return NULL;
}

int mr_system(const char *shell_fmt, ...)
{
    int ret;
    char busybox_path[256];
    char path[256];
    char shell[256];
    char *real_shell = NULL;
    char *const cmd_envp[] = { path, NULL };
    //               0            1     2     3
    char *cmd[] = { busybox_path, "sh", "-c", NULL, NULL };

    snprintf(path, sizeof(path), "PATH=%s:/sbin:/system/bin", mrom_dir());
    snprintf(busybox_path, sizeof(busybox_path), "%s/busybox", mrom_dir());

    va_list ap;
    va_start(ap, shell_fmt);
    ret = vsnprintf(shell, sizeof(shell), shell_fmt, ap);
    if(ret < (int)sizeof(shell))
        real_shell = shell;
    else
    {
        real_shell = malloc(ret+1);
        vsnprintf(real_shell, ret+1, shell_fmt, ap);
    }
    va_end(ap);

    cmd[3] = real_shell;
    ret = run_cmd_with_env(cmd, cmd_envp);

    if(real_shell != shell)
        free(real_shell);
    return ret;
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

int64_t timeval_us_diff(struct timeval now, struct timeval prev)
{
    return ((int64_t)(now.tv_sec - prev.tv_sec))*1000000+
        (now.tv_usec - prev.tv_usec);
}

// Tries to recursively resolve symlinks. If lstat fails at some point,
// presumably because the target does not exist, returns the last resolved path.
char *readlink_recursive(const char *link)
{
    struct stat info;
    if(lstat(link, &info) < 0 || !S_ISLNK(info.st_mode))
        return strdup(link);

    char path[256];
    char buff[256];
    char *p = (char*)link;

    do
    {
        if(info.st_size >= sizeof(path)-1)
        {
            ERROR("readlink_recursive(): Couldn't resolve, too long path.\n");
            return NULL;
        }

        if(readlink(p, buff, info.st_size) != info.st_size)
        {
            ERROR("readlink_recursive: readlink() failed on %s!\n", p);
            return NULL;
        }

        buff[info.st_size] = 0;
        strcpy(path, buff);
        p = path;
    }
    while(lstat(buff, &info) >= 0 && S_ISLNK(info.st_mode));

    return strdup(buff);
}

/* Check to see if /proc/mounts contains any writeable filesystems
 * backed by a block device.
 * Return true if none found, else return false.
 */
static int remount_ro_done(void)
{
    FILE *f;
    char mount_dev[256];
    char mount_dir[256];
    char mount_type[256];
    char mount_opts[256];
    int mount_freq;
    int mount_passno;
    int match;
    int found_rw_fs = 0;

    f = fopen("/proc/mounts", "re");
    if (! f) {
        /* If we can't read /proc/mounts, just give up */
        return 1;
    }

    do {
        match = fscanf(f, "%255s %255s %255s %255s %d %d\n",
                       mount_dev, mount_dir, mount_type,
                       mount_opts, &mount_freq, &mount_passno);
        mount_dev[255] = 0;
        mount_dir[255] = 0;
        mount_type[255] = 0;
        mount_opts[255] = 0;
        if ((match == 6) && !strncmp(mount_dev, "/dev/block", 10) && strstr(mount_opts, "rw")) {
            found_rw_fs = 1;
            break;
        }
    } while (match != EOF);

    fclose(f);

    return !found_rw_fs;
}

/* Remounting filesystems read-only is difficult when there are files
 * opened for writing or pending deletes on the filesystem.  There is
 * no way to force the remount with the mount(2) syscall.  The magic sysrq
 * 'u' command does an emergency remount read-only on all writable filesystems
 * that have a block device (i.e. not tmpfs filesystems) by calling
 * emergency_remount(), which knows how to force the remount to read-only.
 * Unfortunately, that is asynchronous, and just schedules the work and
 * returns.  The best way to determine if it is done is to read /proc/mounts
 * repeatedly until there are no more writable filesystems mounted on
 * block devices.
 */
void emergency_remount_ro(void)
{
    int fd, cnt = 0;

    sync();

    /* Trigger the remount of the filesystems as read-only,
     * which also marks them clean.
     */
    fd = open("/proc/sysrq-trigger", O_WRONLY|O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    write(fd, "u", 1);
    close(fd);


    /* Now poll /proc/mounts till it's done */
    while (!remount_ro_done() && (cnt < 3600)) {
        usleep(100000);
        cnt++;
    }

    return;
}


int imin(int a, int b)
{
    return (a < b) ? a : b;
}

int imax(int a, int b)
{
    return (a > b) ? a : b;
}

int iabs(int a)
{
    return a >= 0 ? a : -a;
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

// alloc and fill with 0s
void *mzalloc(size_t size)
{
    void *res = malloc(size);
    memset(res, 0, size);
    return res;
}

char *strtoupper(const char *str)
{
    int i;
    const int len = strlen(str);
    char *res = malloc(len + 1);
    for(i = 0; i < len; ++i)
    {
        res[i] = str[i];
        if(str[i] >= 'a' && str[i] <= 'z')
            res[i] -= 'a'-'A';
    }
    res[i] = 0;
    return res;
}

int strstartswith(const char *haystack, const char *needle)
{
    return strncmp(haystack, needle, strlen(needle)) == 0;
}

int strendswith(const char *haystack, const char *needle)
{
    size_t h_len = strlen(haystack);
    size_t n_len = strlen(needle);
    if(n_len == 0 || n_len > h_len)
        return 0;
    return strncmp(haystack + h_len - n_len, needle, n_len) == 0;
}

int create_loop_device(const char *dev_path, const char *img_path, int loop_num, int loop_chmod)
{
    int file_fd, device_fd, res = -1;

    file_fd = open(img_path, O_RDWR | O_CLOEXEC);
    if (file_fd < 0) {
        ERROR("Failed to open image %s\n", img_path);
        return -1;
    }

    INFO("create_loop_device: loop_num = %d\n", loop_num);

    if(mknod(dev_path, S_IFBLK | loop_chmod, makedev(7, loop_num)) < 0)
    {
        if(errno != EEXIST)
        {
            ERROR("Failed to create loop file (%d: %s)\n", errno, strerror(errno));
            goto close_file;
        }
        else
            INFO("Loop file %s already exists, using it.\n", dev_path);
    }

    device_fd = open(dev_path, O_RDWR | O_CLOEXEC);
    if (device_fd < 0)
    {
        ERROR("Failed to open loop file (%d: %s)\n", errno, strerror(errno));
        goto close_file;
    }

    if (ioctl(device_fd, LOOP_SET_FD, file_fd) < 0)
    {
        ERROR("ioctl LOOP_SET_FD failed on %s (%d: %s)\n", dev_path, errno, strerror(errno));
        goto close_dev;
    }

    res = 0;
close_dev:
    close(device_fd);
close_file:
    close(file_fd);
    return res;
}

#define MAX_LOOP_NUM 1023
int mount_image(const char *src, const char *dst, const char *fs, int flags, const void *data)
{
    char path[64];
    int device_fd;
    int loop_num = 0;
    int res = -1;
    struct stat info;
    struct loop_info64 lo_info;

    for(; loop_num < MAX_LOOP_NUM; ++loop_num)
    {
        sprintf(path, "/dev/block/loop%d", loop_num);
        if(stat(path, &info) < 0)
        {
            if(errno == ENOENT)
                break;
        }
        else if(S_ISBLK(info.st_mode) && (device_fd = open(path, O_RDWR | O_CLOEXEC)) >= 0)
        {
            int ioctl_res = ioctl(device_fd, LOOP_GET_STATUS64, &lo_info);
            close(device_fd);

            if (ioctl_res < 0 && errno == ENXIO)
                break;
        }
    }

    if(loop_num == MAX_LOOP_NUM)
    {
        ERROR("mount_image: failed to find suitable loop device number!\n");
        return -1;
    }

    if(create_loop_device(path, src, loop_num, 0777) < 0)
        return -1;

    if(mount(path, dst, fs, flags, data) < 0)
        ERROR("Failed to mount loop (%d: %s)\n", errno, strerror(errno));
    else
        res = 0;

    return res;
}

#define MULTIROM_LOOP_NUM_START   231
#define MULTIROM_DEV_PATH "/multirom/dev"
int multirom_mount_image(const char *src, const char *dst, const char *fs, int flags, const void *data)
{
    static int next_loop_num = MULTIROM_LOOP_NUM_START;
    char path[64];
    int device_fd;
    int loop_num = 0;
    int res = -1;
    struct stat info;
    struct loop_info64 lo_info;

    for(loop_num = next_loop_num; loop_num < MAX_LOOP_NUM; ++loop_num)
    {
        sprintf(path, "/dev/block/loop%d", loop_num);
        if(stat(path, &info) < 0)
        {
            if(errno == ENOENT)
                break;
        }
        else if(S_ISBLK(info.st_mode) && (device_fd = open(path, O_RDWR | O_CLOEXEC)) >= 0)
        {
            int ioctl_res = ioctl(device_fd, LOOP_GET_STATUS64, &lo_info);
            close(device_fd);

            if (ioctl_res < 0 && errno == ENXIO)
                break;
        }
    }

    if(loop_num == MAX_LOOP_NUM)
    {
        ERROR("mount_image: failed to find suitable loop device number!\n");
        return -1;
    }

    // now change to /multirom/dev/<partition name>
    mkdir_recursive_with_perms(MULTIROM_DEV_PATH, 0777, NULL, NULL);
    sprintf(path, MULTIROM_DEV_PATH "/%s", dst);

    if(create_loop_device(path, src, loop_num, 0777) < 0)
        return -1;

    // never reuse an existing loop
    next_loop_num = loop_num + 1;

    if(mount(path, dst, fs, flags, data) < 0)
        ERROR("Failed to mount loop (%d: %s)\n", errno, strerror(errno));
    else
        res = 0;

    sync();

    return res;
}

void do_reboot(int type)
{
    sync();
    emergency_remount_ro();

    switch(type)
    {
        default:
        case REBOOT_SYSTEM:
            android_reboot(ANDROID_RB_RESTART, 0, 0);
            break;
        case REBOOT_RECOVERY:
            android_reboot(ANDROID_RB_RESTART2, 0, "recovery");
            break;
        case REBOOT_BOOTLOADER:
            android_reboot(ANDROID_RB_RESTART2, 0, "bootloader");
            break;
        case REBOOT_SHUTDOWN:
            android_reboot(ANDROID_RB_POWEROFF, 0, 0);
            break;
    }

    while(1);
}
