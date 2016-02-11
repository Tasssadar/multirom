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

#ifndef KEXEC_H
#define KEXEC_H

struct kexec
{
    char **args;
};

void kexec_init(struct kexec *k, const char *path);
void kexec_destroy(struct kexec *k);
int kexec_load_exec(struct kexec *k);
void kexec_add_arg(struct kexec *k, const char *arg);
void kexec_add_arg_prefix(struct kexec *k, const char *prefix, const char *value);
void kexec_add_kernel(struct kexec *k, const char *path, int hardboot);

#endif
