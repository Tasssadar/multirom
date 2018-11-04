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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utils/Log.h>

#define PROPERTY_SOCKET "/property_socket"
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

int property_set(char* property, char* value) {

    char* property_value;
    int i, s, len;
    struct sockaddr_un saun;

    ALOGE("property_set called for %s:%s\n", property, value);
    property_value = calloc(strlen(property) + strlen(value) + 1, 1);

    sprintf(property_value, "%s:%s", property, value);
    /*
     * Get a socket to work with.  This socket will
     * be in the UNIX domain, and will be a
     * datagram socket.
     */
    if ((s = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        ALOGE("client: socket");
    }

    /*
     * Create the address we will be connecting to.
     */
    saun.sun_family = AF_UNIX;
    strcpy(saun.sun_path, PROPERTY_SOCKET);
    len = sizeof(saun.sun_family) + strlen(saun.sun_path);

    if (sendto(s, property_value, strlen(property_value), 0, &saun, sizeof(struct sockaddr_un)) < 0) {
        ALOGE("sendto failed %s\n", strerror(errno));
    }

    free(property_value);

    close(s);
    return 0;
}
