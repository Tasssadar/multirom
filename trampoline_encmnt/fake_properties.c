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
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>

/* MultiROM doesn't initialize the property service,
 * but decryption on Nexus 6P waits for one property to become true
 * so we hardcode it here
 */

#ifdef MR_ENCRYPTION_FAKE_PROPERTIES_EXTRAS
extern const char *mr_fake_properties[][2];
#endif

int property_get(const char *key, char *value, const char *default_value)
{
    if (!strcmp(key, "sys.listeners.registered"))
        default_value = "true";

    /* For Keymaster 3 HAL, we need security patch and build version
     * to match with the one in bootimg header. Pass the OS version
     * and security patch version as environment variable OSVER and OSPATCH
     * respectively to keymaster and qseecom (Make sure they LD_PRELOAD this lib)
     * process after reading from bootimg in tramp_hook_encryption_setup() function */

    if (!strcmp(key, "ro.build.version.release")) {
        if (getenv("OSVER")) {
            strcpy(value, getenv("OSVER"));
            return strlen(value);
        }
    }

    if (!strcmp(key, "ro.build.version.security_patch")) {
        if (getenv("OSPATCH")) {
            strcpy(value, getenv("OSPATCH"));
            return strlen(value);
        }
    }

#ifdef MR_ENCRYPTION_FAKE_PROPERTIES_EXTRAS
    int i;
    for(i = 0; mr_fake_properties[i][0]; ++i)
    {
        if (!strcmp(key, mr_fake_properties[i][0])) {
            strncpy(value, mr_fake_properties[i][1], PROP_VALUE_MAX);
            return strlen(value);
        }
    }
#endif

    if (default_value)
        strncpy(value, default_value, PROP_VALUE_MAX);
    return strlen(value);
}
