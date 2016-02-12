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

#ifndef MR_DEVICE_HOOKS_H
#define MR_DEVICE_HOOKS_H

#ifdef MR_DEVICE_HOOKS

#if MR_DEVICE_HOOKS >= 1
int mrom_hook_after_android_mounts(const char *busybox_path, const char *base_path, int type);
#endif

#if MR_DEVICE_HOOKS >= 2
void mrom_hook_before_fb_close(void);
#endif

#if MR_DEVICE_HOOKS >= 3
void tramp_hook_before_device_init(void);
#endif

#if MR_DEVICE_HOOKS >= 4
int mrom_hook_allow_incomplete_fstab(void);
#endif

#if MR_DEVICE_HOOKS >= 5
void mrom_hook_fixup_bootimg_cmdline(char *bootimg_cmdline, size_t bootimg_cmdline_cap);
int mrom_hook_has_kexec(void);
#endif

#if MR_DEVICE_HOOKS >= 6
void tramp_hook_encryption_setup(void);
void tramp_hook_encryption_cleanup(void);
void mrom_hook_fixup_full_cmdline(char *bootimg_cmdline, size_t bootimg_cmdline_cap);
#endif

#endif /* MR_DEVICE_HOOKS */

#endif /* MR_DEVICE_HOOKS_H */
