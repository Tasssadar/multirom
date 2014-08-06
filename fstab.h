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

#ifndef FSTAB_H
#define FSTAB_H

struct fstab_part
{
    char *path;
    char *device;
    char *type;
    char *options_raw;
    unsigned long mountflags;
    char *options;
    char *options2;
    int disabled;
};

struct fstab
{
    int version;
    int count;
    struct fstab_part **parts;
};

struct fstab *fstab_create_empty(int version);
struct fstab *fstab_load(const char *path, int resolve_symlinks);
struct fstab *fstab_auto_load(void);
void fstab_destroy(struct fstab *f);
void fstab_destroy_part(struct fstab_part *p);
void fstab_dump(struct fstab *f);
struct fstab_part *fstab_find_by_path(struct fstab *f, const char *path);
void fstab_parse_options(char *opt, struct fstab_part *p);
int fstab_save(struct fstab *f, const char *path);
int fstab_disable_part(struct fstab *f, const char *path);
void fstab_add_part(struct fstab *f, const char *dev, const char *path, const char *type, const char *options, const char *options2);
void fstab_add_part_struct(struct fstab *f, struct fstab_part *p);
struct fstab_part *fstab_clone_part(struct fstab_part *p);


#endif
