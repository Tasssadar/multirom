#include "multirom_ui.h"
#include "multirom_ui_themes.h"
#include "multirom.h"
#include "util.h"

multirom_themes_info *multirom_ui_init_themes(void)
{
    multirom_themes_info *i = mzalloc(sizeof(multirom_themes_info));

    i->data = mzalloc(sizeof(multirom_theme_data));
    i->data->selected_tab = -1;

#ifdef MULTIROM_THEME_800x1280
    list_add(init_theme_info_800x1280(), &i->themes);
#endif
#ifdef MULTIROM_THEME_1280x800
    list_add(init_theme_info_1280x800(), &i->themes);
#endif
    return i;
}

void multirom_ui_free_themes(multirom_themes_info *i)
{
    list_clear(&i->themes, &free);
    free(i->data);
    free(i);
}

multirom_theme *multirom_ui_select_theme(multirom_themes_info *i, int w, int h)
{
    if(i->themes == NULL)
        return NULL;

    multirom_theme **itr;
    for(itr = i->themes; *itr; ++itr)
    {
        if((*itr)->width == w && (*itr)->height == h)
        {
            (*itr)->data = i->data;
            return *itr;
        }
    }
    return NULL;
}
