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

#include <libxml/xmlstring.h>
#include "xmlutils.h"

xmlNode *xml_find_node(xmlNode *root, const char *name)
{
    xmlNode *ptr;
    for (ptr = root; ptr; ptr = ptr->next)
        if (ptr->type == XML_ELEMENT_NODE && xmlStrEqual(ptr->name, (xmlChar*)name))
            return ptr;
    return NULL;
}

int xml_str_equal(const void *str1, const void *str2)
{
    return xmlStrEqual((const xmlChar*)str1, (const xmlChar*)str2);
}

xmlChar *xml_get_prop(xmlNode *node, const void *name)
{
    return xmlGetProp(node, (const xmlChar*)name);
}
