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

#include "colors.h"
#include "util.h"

static const struct mrom_color_theme color_themes[] = {
    // 0 - red/white, default
    {
        .background = 0xFFDCDCDC,
        .highlight_bg = 0xFFF72F2F,
        .highlight_hover = 0xFFF85555,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFF000000,
        .text_secondary = 0xFF4D4D4D,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFFFFFFFF,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFF0000FF,
        .btn_fake_shadow = 0xFFA1A1A1,
    },
    // 1 - orange/white
    {
        .background = 0xFFDCDCDC,
        .highlight_bg = 0xFFFF5722,
        .highlight_hover = 0xFFFF8A65,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFF000000,
        .text_secondary = 0xFF4D4D4D,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFFFFFFFF,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0xFFA1A1A1,
    },
    // 2 - blue/white
    {
        .background = 0xFFDCDCDC,
        .highlight_bg = 0xFF5677FC,
        .highlight_hover = 0xFF91A7FF,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFF000000,
        .text_secondary = 0xFF4D4D4D,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFFFFFFFF,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0xFFA1A1A1,
    },
    // 3 - purple/white
    {
        .background = 0xFFDCDCDC,
        .highlight_bg = 0xFF673AB7,
        .highlight_hover = 0xFF9575CD,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFF000000,
        .text_secondary = 0xFF4D4D4D,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFFFFFFFF,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0xFFA1A1A1,
    },
    // 4 - green/white
    {
        .background = 0xFFDCDCDC,
        .highlight_bg = 0xFF259B24,
        .highlight_hover = 0xFF72D572,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFF000000,
        .text_secondary = 0xFF4D4D4D,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFFFFFFFF,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0xFFA1A1A1,
    },
    // 5 - dark blue
    {
        .background = 0xFF263238,
        .highlight_bg = 0xFF607D8B,
        .highlight_hover = 0xFF90A4AE,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFFFFFFFF,
        .text_secondary = 0xFFE6E6E6,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54000000,
        .rom_highlight = 0xFF607D8B,
        .rom_highlight_shadow = 0x54000000,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0xFF1C2529,
    },
    // 6 - dark blue/black
    {
        .background = 0xFF000000,
        .highlight_bg = 0xFF263238,
        .highlight_hover = 0xFF607D8B,
        .highlight_text = 0xFFFFFFFF,
        .text = 0xFFFFFFFF,
        .text_secondary = 0xFFE6E6E6,
        .ncard_bg = 0xFF37474F,
        .ncard_text = 0xFFFFFFFF,
        .ncard_text_secondary = 0xFFE6E6E6,
        .ncard_shadow = 0x54424242,
        .rom_highlight = 0xFF263238,
        .rom_highlight_shadow = 0x54424242,
        .keyaction_frame = 0xFFFF0000,
        .btn_fake_shadow = 0x00000000,
    },
};

const struct mrom_color_theme *color_theme = &color_themes[0];

void colors_select(size_t color_theme_idx)
{
    if(color_theme_idx >= ARRAY_SIZE(color_themes))
        return;
    color_theme = &color_themes[color_theme_idx];
}

const struct mrom_color_theme *colors_get(size_t color_theme_idx)
{
    if(color_theme_idx >= ARRAY_SIZE(color_themes))
        return NULL;
    return &color_themes[color_theme_idx];
}

int colors_count(void)
{
    return ARRAY_SIZE(color_themes);
}
