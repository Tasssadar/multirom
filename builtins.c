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

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <linux/kd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <linux/loop.h>
#include <poll.h>
#include <dirent.h>
#include <linux/input.h>

#include "init.h"
#include "keywords.h"
#include "property_service.h"
#include "devices.h"
#include "bootmgr.h"

#include <private/android_filesystem_config.h>

void add_environment(const char *name, const char *value);

extern int init_module(void *, unsigned long, const char *);

static int write_file(const char *path, const char *value)
{
    int fd, ret, len;

    fd = open(path, O_WRONLY|O_CREAT, 0622);

    if (fd < 0)
        return -errno;

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

static int insmod(const char *filename, char *options)
{
    void *module;
    unsigned size;
    int ret;

    module = read_file(filename, &size);
    if (!module)
        return -1;

    ret = init_module(module, size, options);

    free(module);

    return ret;
}

static int setkey(struct kbentry *kbe)
{
    int fd, ret;

    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (fd < 0)
        return -1;

    ret = ioctl(fd, KDSKBENT, kbe);

    close(fd);
    return ret;
}

static int __ifupdown(const char *interface, int up)
{
    struct ifreq ifr;
    int s, ret;

    strlcpy(ifr.ifr_name, interface, IFNAMSIZ);

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return -1;

    ret = ioctl(s, SIOCGIFFLAGS, &ifr);
    if (ret < 0) {
        goto done;
    }

    if (up)
        ifr.ifr_flags |= IFF_UP;
    else
        ifr.ifr_flags &= ~IFF_UP;

    ret = ioctl(s, SIOCSIFFLAGS, &ifr);
    
done:
    close(s);
    return ret;
}

static void service_start_if_not_disabled(struct service *svc)
{
    if (!(svc->flags & SVC_DISABLED)) {
        service_start(svc, NULL);
    }
}

int do_mkdir(int nargs, char **args)
{
    mode_t mode = 0755;

    /* mkdir <path> [mode] [owner] [group] */

    if (nargs >= 3) {
        mode = strtoul(args[2], 0, 8);
    }

    if (mkdir(args[1], mode)) {
        return -errno;
    }

    if (nargs >= 4) {
        uid_t uid = decode_uid(args[3]);
        gid_t gid = -1;

        if (nargs == 5) {
            gid = decode_uid(args[4]);
        }

        if (chown(args[1], uid, gid)) {
            return -errno;
        }
    }

    return 0;
}

int do_mknod(int nargs, char **args)
{
    int mode = 0666 | S_IFBLK;
    int major = 0, minor = 0;
    char *end;

    major = strtol(args[2], &end, 0);
    if (*end) {
        //printf(stderr, "bad major number %s\n", argv[3]);
        return -errno;
    }

    minor = strtol(args[3], &end, 0);
    if (*end) {
        //printf(stderr, "bad minor number %s\n", argv[4]);
        return -errno;
    }
    return mknod(args[1], mode, makedev(major, minor));
}

static struct {
    const char *name;
    unsigned flag;
} mount_flags[] = {
    { "noatime",    MS_NOATIME },
    { "nosuid",     MS_NOSUID },
    { "nodev",      MS_NODEV },
    { "nodiratime", MS_NODIRATIME },
    { "ro",         MS_RDONLY },
    { "rw",         0 },
    { "bind",       MS_BIND },
    { "remount",    MS_REMOUNT },
    { "defaults",   0 },
    { 0,            0 },
};

// Mount modified so that it tries fs types in array bellow, when system == multirom_sd_fs[0]
// (Not for mtd@ and loop@ mounts)
const char *multirom_sd_fs[] = { "ext4", "ext2", "ext3" };
#define MULTIROM_SD_FS_COUNT 3

/* mount <type> <device> <path> <flags ...> <options> */
int do_mount(int nargs, char **args)
{
    char tmp[64];
    char *source, *target, *system;
    char *options = NULL;
    unsigned flags = 0;
    int n, i;

    for (n = 4; n < nargs; n++) {
        for (i = 0; mount_flags[i].name; i++) {
            if(!strcmp(args[n], "multirom"))
            {
                if(!bootmgr_selected)
                {
                    INFO("Skipping mount because of bootmgr");
                    return 0;
                }
                break;
            }
            if (!strcmp(args[n], mount_flags[i].name)) {
                flags |= mount_flags[i].flag;
                break;
            }
        }

        /* if our last argument isn't a flag, wolf it up as an option string */
        if (n + 1 == nargs && !mount_flags[i].name)
            options = args[n];
    }

    system = args[1];
    source = args[2];
    target = args[3];

    if (!strncmp(source, "mtd@", 4)) {
        n = mtd_name_to_number(source + 4);
        if (n < 0) {
            return -1;
        }

        sprintf(tmp, "/dev/block/mtdblock%d", n);
        int res = mount(tmp, target, system, flags, options);
        if (res < 0) {
            return res;
        }

        return 0;
    } else if (!strncmp(source, "loop@", 5)) {
        int mode, loop, fd;
        struct loop_info info;

        mode = (flags & MS_RDONLY) ? O_RDONLY : O_RDWR;
        fd = open(source + 5, mode);
        if (fd < 0) {
            return -1112;
        }

        for (n = 0; ; n++) {
            sprintf(tmp, "/dev/block/loop%d", n);
            loop = open(tmp, mode);
            if (loop < 0) {
                return -1111;
            }

            /* if it is a blank loop device */
            if (ioctl(loop, LOOP_GET_STATUS, &info) < 0 && errno == ENXIO) {
                /* if it becomes our loop device */
                if (ioctl(loop, LOOP_SET_FD, fd) >= 0) {
                    close(fd);
                    int res = mount(tmp, target, system, flags, options);
                    if (res < 0) {
                        ioctl(loop, LOOP_CLR_FD, 0);
                        close(loop);
                        return res;
                    }

                    close(loop);
                    return 0;
                }
            }

            close(loop);
        }

        close(fd);
        ERROR("out of loopback devices");
        return -1;
    }
    else
    {
        int res = mount(source, target, system, flags, options);
        if (res < 0)
        {
            if(!strcmp(system, multirom_sd_fs[0]))
            {
                static unsigned char multirom_fs = 1;
                int res_tmp = mount(source, target, multirom_sd_fs[multirom_fs], flags, options);
                if(res_tmp == 0)
                    return 0;

                unsigned char cur_fs = 1;
                for(; cur_fs < MULTIROM_SD_FS_COUNT; ++cur_fs)
                {
                    if(multirom_fs == cur_fs)
                        continue;

                    res_tmp = mount(source, target, multirom_sd_fs[cur_fs], flags, options);

                    if(res_tmp < 0)
                        continue;

                    multirom_fs = cur_fs;
                    return 0;
                }
            }
            return res;
        }

        return 0;
    }
}

int do_symlink(int nargs, char **args)
{
    return symlink(args[1], args[2]);
}

int do_sysclktz(int nargs, char **args)
{
    struct timezone tz;

    if (nargs != 2)
        return -1;

    memset(&tz, 0, sizeof(tz));
    tz.tz_minuteswest = atoi(args[1]);   
    if (settimeofday(NULL, &tz))
        return -1;
    return 0;
}

int __copy(char *from, char *to)
{
    char *buffer = NULL;
    int rc = 0;
    int fd1 = -1, fd2 = -1;
    struct stat info;
    int brtw, brtr;
    char *p;

    if (stat(from, &info) < 0)
        return -1;

    if ((fd1 = open(from, O_RDONLY)) < 0)
        goto out_err;

    if ((fd2 = open(to, O_WRONLY|O_CREAT|O_TRUNC, 0660)) < 0)
        goto out_err;

    if (!(buffer = malloc(info.st_size)))
        goto out_err;

    p = buffer;
    brtr = info.st_size;
    while(brtr) {
        rc = read(fd1, p, brtr);
        if (rc < 0)
            goto out_err;
        if (rc == 0)
            break;
        p += rc;
        brtr -= rc;
    }

    p = buffer;
    brtw = info.st_size;
    while(brtw) {
        rc = write(fd2, p, brtw);
        if (rc < 0)
            goto out_err;
        if (rc == 0)
            break;
        p += rc;
        brtw -= rc;
    }

    rc = 0;
    goto out;
out_err:
    rc = -1;
out:
    if (buffer)
        free(buffer);
    if (fd1 >= 0)
        close(fd1);
    if (fd2 >= 0)
        close(fd2);
    return rc;
}

int do_loglevel(int nargs, char **args) {
    if (nargs == 2) {
        log_set_level(atoi(args[1]));
        return 0;
    }
    return -1;
}

int do_import_boot(int nargs, char **args)
{
    DIR *d = opendir(args[1]);
    if(d == NULL)
        return -1;
    struct dirent *dp;
    char to[100];
    char from[100];

    // copy init binary
    INFO("Copy init binary to ramdisk");
    sprintf(from, "%s/init", args[1]);
    __copy(from, "/main_init");
    chmod("/main_init", 0750);

    // /default.prop
    sprintf(from, "%s/default.prop", args[1]);
    __copy(from, "/default.prop");

    // /sbin/adbd
    sprintf(from, "%s/adbd", args[1]);
    __copy(from, "/sbin/adbd");

    while(dp = readdir(d))
    {
        if(strstr(dp->d_name, ".rc") == NULL)
            continue;

        // copy to our ramdisk
        INFO("Copy %s to ramdisk", dp->d_name);
        sprintf(from, "%s/%s", args[1], dp->d_name);
        sprintf(to, "/%s", dp->d_name);
        __copy(from, to);
        chmod(to, 0750);
    }
    closedir(d);
    return 0;
}

// Remove system, data and cache mounts from rc files
inline unsigned char __commentLine(char *line)
{
    if(strstr(line, "mount") && strstr(line, "yaffs2") &&
      (strstr(line, "/system") || strstr(line, "/data") || strstr(line, "/cache")))
        return 1;

    // Useless?
    //if(strstr(line, "mkdir /data") || strstr(line, "mkdir /system") || strstr(line, "mkdir /cache"))
    //    return 1;
    return 0;
}

int do_remove_rc_mounts(int nargs, char **args)
{
    DIR *d = opendir("/");
    if(d == NULL)
        return -1;

    struct dirent *dp = NULL;

    char file[100];
    char new_file[100];
    char line[512];
    unsigned short itr = 0;
    int c = 0;
    FILE *f = NULL;
    FILE *f_out = NULL;
    
    while(dp = readdir(d))
    {
        if(!strstr(dp->d_name, ".rc") || strstr(dp->d_name, "preinit"))
            continue;

        itr = 0;

        sprintf(file, "/%s", dp->d_name);
        f = fopen(file, "r");
        INFO("Parsing %s", file);

        sprintf(new_file, "/%s.new", dp->d_name);
        f_out = fopen(new_file, "w");
        if(f == NULL || f_out == NULL) continue;

        char first_done = 0;
        while(1)
        {
           c = fgetc(f);
           if(c == EOF)
           {
               if(itr > 0)
                   fwrite(line, 1, itr, f_out);
               break;
           }

           line[itr++] = (char)c;

           if(c == '\n')
           {
               line[itr] = 0; // null-terminated string

               if(__commentLine(line))
               {
                   if(first_done)
                       fputc((int)'#', f_out);
                   else
                   {
                       first_done = 1;
                       fputs("\n export DUMMY_LINE_INGORE_IT 1 \n#", f_out);
                   }
               }
               fwrite(line, 1, itr, f_out);
               itr = 0;
           }
        }
        fflush(f);
        fflush(f_out);
        fclose(f);
        fclose(f_out);

        __copy(new_file, file);
    }
    closedir(d);
    return 0;
}

int do_unlink(int nargs, char **args)
{
    if (nargs != 2)
        return -1;

    return unlink(args[1]);
}

int do_bootmgr(int nargs, char **args)
{
    if (nargs != 2)
        return -1;

    if(battchg_pause)
    {
        INFO("Disabling bootmgr due to battchg_pause == 1 (charger plugged-in?)");
        return 0;
    }

    int timeout = atoi(args[1]);
    if(timeout < 0)
    {
        INFO("Disabling bootmgr because of timeout < 0");
        return 0;
    }

    bootmgr_start(timeout);
    return 0;
}

#if 0
int do_chdir(int nargs, char **args)
{
    chdir(args[1]);
    return 0;
}

int do_chroot(int nargs, char **args)
{
    chroot(args[1]);
    return 0;
}

int do_class_start(int nargs, char **args)
{
        /* Starting a class does not start services
         * which are explicitly disabled.  They must
         * be started individually.
         */
    service_for_each_class(args[1], service_start_if_not_disabled);
    return 0;
}

int do_class_stop(int nargs, char **args)
{
    service_for_each_class(args[1], service_stop);
    return 0;
}

int do_domainname(int nargs, char **args)
{
    return write_file("/proc/sys/kernel/domainname", args[1]);
}

/*exec <path> <arg1> <arg2> ... */
#define MAX_PARAMETERS 64
int do_exec(int nargs, char **args)
{
    pid_t pid;
    int status, i, j;
    char *par[MAX_PARAMETERS];
    if (nargs > MAX_PARAMETERS)
    {
        return -1;
    }
    for(i=0, j=1; i<(nargs-1) ;i++,j++)
    {
        par[i] = args[j];
    }
    par[i] = (char*)0;
    pid = fork();
    if (!pid)
    {
        execv(par[0],par);
    }
    else
    {
        while(wait(&status)!=pid);
    }
    return 0;
}

int do_export(int nargs, char **args)
{
    add_environment(args[1], args[2]);
    return 0;
}

int do_hostname(int nargs, char **args)
{
    return write_file("/proc/sys/kernel/hostname", args[1]);
}

int do_ifup(int nargs, char **args)
{
    return __ifupdown(args[1], 1);
}


static int do_insmod_inner(int nargs, char **args, int opt_len)
{
    char options[opt_len + 1];
    int i;

    options[0] = '\0';
    if (nargs > 2) {
        strcpy(options, args[2]);
        for (i = 3; i < nargs; ++i) {
            strcat(options, " ");
            strcat(options, args[i]);
        }
    }

    return insmod(args[1], options);
}

int do_insmod(int nargs, char **args)
{
    int i;
    int size = 0;

    if (nargs > 2) {
        for (i = 2; i < nargs; ++i)
            size += strlen(args[i]) + 1;
    }

    return do_insmod_inner(nargs, args, size);
}

int do_import(int nargs, char **args)
{
    return parse_config_file(args[1]);
}

int do_setkey(int nargs, char **args)
{
    struct kbentry kbe;
    kbe.kb_table = strtoul(args[1], 0, 0);
    kbe.kb_index = strtoul(args[2], 0, 0);
    kbe.kb_value = strtoul(args[3], 0, 0);
    return setkey(&kbe);
}

int do_setprop(int nargs, char **args)
{
    property_set(args[1], args[2]);
    return 0;
}

int do_setrlimit(int nargs, char **args)
{
    struct rlimit limit;
    int resource;
    resource = atoi(args[1]);
    limit.rlim_cur = atoi(args[2]);
    limit.rlim_max = atoi(args[3]);
    return setrlimit(resource, &limit);
}

int do_start(int nargs, char **args)
{
    struct service *svc;
    svc = service_find_by_name(args[1]);
    if (svc) {
        service_start(svc, NULL);
    }
    return 0;
}

int do_stop(int nargs, char **args)
{
    struct service *svc;
    svc = service_find_by_name(args[1]);
    if (svc) {
        service_stop(svc);
    }
    return 0;
}

int do_restart(int nargs, char **args)
{
    struct service *svc;
    svc = service_find_by_name(args[1]);
    if (svc) {
        service_stop(svc);
        service_start(svc, NULL);
    }
    return 0;
}

int do_trigger(int nargs, char **args)
{
    action_for_each_trigger(args[1], action_add_queue_tail);
    drain_action_queue();
    return 0;
}

int do_write(int nargs, char **args)
{
    return write_file(args[1], args[2]);
}

int do_copy(int nargs, char **args)
{
    if (nargs != 3)
        return -1;

    return __copy(args[1], args[2]);
}

int do_chown(int nargs, char **args) {
    /* GID is optional. */
    if (nargs == 3) {
        if (chown(args[2], decode_uid(args[1]), -1) < 0)
            return -errno;
    } else if (nargs == 4) {
        if (chown(args[3], decode_uid(args[1]), decode_uid(args[2])))
            return -errno;
    } else {
        return -1;
    }
    return 0;
}

static mode_t get_mode(const char *s) {
    mode_t mode = 0;
    while (*s) {
        if (*s >= '0' && *s <= '7') {
            mode = (mode<<3) | (*s-'0');
        } else {
            return -1;
        }
        s++;
    }
    return mode;
}

int do_chmod(int nargs, char **args) {
    mode_t mode = get_mode(args[1]);
    if (chmod(args[2], mode) < 0) {
        return -errno;
    }
    return 0;
}

int do_device(int nargs, char **args) {
    int len;
    char tmp[64];
    char *source = args[1];
    int prefix = 0;

    if (nargs != 5)
        return -1;
    /* Check for wildcard '*' at the end which indicates a prefix. */
    len = strlen(args[1]) - 1;
    if (args[1][len] == '*') {
        args[1][len] = '\0';
        prefix = 1;
    }
    /* If path starts with mtd@ lookup the mount number. */
    if (!strncmp(source, "mtd@", 4)) {
        int n = mtd_name_to_number(source + 4);
        if (n >= 0) {
            snprintf(tmp, sizeof(tmp), "/dev/mtd/mtd%d", n);
            source = tmp;
        }
    }
    add_devperms_partners(source, get_mode(args[2]), decode_uid(args[3]),
                          decode_uid(args[4]), prefix);
    return 0;
}

int do_devwait(int nargs, char **args) {
    int dev_fd, uevent_fd, rc, timeout = DEVWAIT_TIMEOUT;
    struct pollfd ufds[1];

    uevent_fd = open_uevent_socket();

    ufds[0].fd = uevent_fd;
    ufds[0].events = POLLIN;

    for(;;) {

        dev_fd = open(args[1], O_RDONLY);
        if (dev_fd < 0) {
            if (errno != ENOENT) {
                ERROR("%s: open failed with error %d\n", __func__, errno);
                rc = -errno;
                break;
            }
        } else {
            return 0;
        }

        ufds[0].revents = 0;

        rc = poll(ufds, 1, DEVWAIT_POLL_TIME);
        if (rc == 0) {
            if (timeout > 0)
                timeout -= DEVWAIT_POLL_TIME;
            else {
                ERROR("%s: timed out waiting on file: %s\n", __func__, args[1]);
                rc = -ETIME;
                break;
            }
            continue;
        } else if (rc < 0) {
            ERROR("%s: poll request failed for file: %s\n", __func__, args[1]);
            break;
        }

        if (ufds[0].revents == POLLIN)
            handle_device_fd(uevent_fd);
    }

    return rc;
}
#endif