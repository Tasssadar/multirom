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

int property_get(const char *key, char *value, const char *default_value)
{
    if (!strcmp(key, "sys.listeners.registered"))
        default_value = "true";
    if (default_value)
        strncpy(value, default_value, PROP_VALUE_MAX);
    return strlen(value);
}
