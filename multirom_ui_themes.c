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

#include "multirom_ui.h"
#include "multirom_ui_themes.h"
#include "multirom.h"
#include "util.h"

multirom_themes_info *multirom_ui_init_themes(void)
{
    multirom_themes_info *i = mzalloc(sizeof(multirom_themes_info));

    i->data = mzalloc(sizeof(multirom_theme_data));
    i->data->selected_tab = -1;

#define ADD_THEME(RES) \
    extern struct multirom_theme theme_info_ ## RES; \
    list_add(&theme_info_ ## RES, &i->themes);

#ifdef MULTIROM_THEME_800x1280
    ADD_THEME(800x1280);
#endif
#ifdef MULTIROM_THEME_1280x800
    ADD_THEME(1280x800);
#endif
    return i;
}

void multirom_ui_free_themes(multirom_themes_info *i)
{
    list_clear(&i->themes, NULL);
    free(i->data);
    free(i);
}

multirom_theme *multirom_ui_select_theme(multirom_themes_info *i, int w, int h)
{
    if(i->themes == NULL)
        return NULL;

    multirom_theme **itr;
    for(itr = i->themes; *itr; ++itr)
        if((*itr)->width == w && (*itr)->height == h)
            return *itr;

    return NULL;
}
