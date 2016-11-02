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

#ifndef VERSION_H
#define VERSION_H
    #define VERSION_MULTIROM 33
    #define VERSION_TRAMPOLINE 27
    #define VERSION_APKL 2

    // For device-specific fixes. Use letters, the version will then be like "12a"
    #ifdef MR_DEVICE_SPECIFIC_VERSION
        #define VERSION_DEV_FIX MR_DEVICE_SPECIFIC_VERSION
    #else
        #define VERSION_DEV_FIX ""
    #endif
#endif
