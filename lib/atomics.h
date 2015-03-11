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

#ifndef ATOMICS_H
#define ATOMICS_H

#if (PLATFORM_SDK_VERSION >= 21)
#include <stdatomic.h>
#else
#include <sys/atomics.h>
typedef struct { volatile int __val; } atomic_int;
#define ATOMIC_VAR_INIT(value) { .__val = value }
#define atomic_compare_exchange_strong(valptr, oldval, newval) (!__atomic_cmpxchg((oldval)->__val, newval, &((valptr)->__val)))
#endif

#endif
