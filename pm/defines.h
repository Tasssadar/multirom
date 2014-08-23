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

#ifndef PM_DEFINES_H
#define PM_DEFINES_H

#define FIRST_APPLICATION_UID 10000
#define LAST_APPLICATION_UID  19999

enum
{
    COMPONENT_ENABLED_STATE_DEFAULT  = 0,
    COMPONENT_ENABLED_STATE_ENABLED  = 1,
    COMPONENT_ENABLED_STATE_DISABLED = 2,
    COMPONENT_ENABLED_STATE_DISABLED_USER = 3,
    COMPONENT_ENABLED_STATE_DISABLED_UNTIL_USED = 4
};

#define PKG_INSTALL_COMPLETE 1
#define PKG_INSTALL_INCOMPLETE 0

enum
{
    PROTECTION_NORMAL = 0,
    PROTECTION_DANGEROUS = 1,
    PROTECTION_SIGNATURE = 2,
    PROTECTION_SIGNATURE_OR_SYSTEM = 3,

    PROTECTION_FLAG_SYSTEM = 0x10
};

enum
{
    BASEPERM_TYPE_NORMAL  = 0,
    BASEPERM_TYPE_BUILTIN = 1,
    BASEPERM_TYPE_DYNAMIC = 2
};

#define PRIVILEGED_APP_PATH "/system/priv-app/"
#define PRIVILEGED_APP_PATH_U ((const xmlChar*)PRIVILEGED_APP_PATH)
#define PRIVILEGED_APP_PATH_LEN 17

#endif
