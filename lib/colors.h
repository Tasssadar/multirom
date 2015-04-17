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

#ifndef MROM_COLORS_H
#define MROM_COLORS_H

#include <stdint.h>

struct mrom_color_theme
{
    uint32_t background;
    uint32_t highlight_bg;
    uint32_t highlight_hover;
    uint32_t highlight_text;
    uint32_t text;
    uint32_t text_secondary;
    uint32_t ncard_bg;
    uint32_t ncard_text;
    uint32_t ncard_text_secondary;
    uint32_t ncard_shadow;
    uint32_t rom_highlight;
    uint32_t rom_highlight_shadow;
    uint32_t keyaction_frame;
    uint32_t btn_fake_shadow;
};

extern const struct mrom_color_theme *color_theme;
#define C_BACKGROUND (color_theme->background)
#define C_HIGHLIGHT_BG (color_theme->highlight_bg)
#define C_HIGHLIGHT_HOVER (color_theme->highlight_hover)
#define C_HIGHLIGHT_TEXT (color_theme->highlight_text)
#define C_TEXT (color_theme->text)
#define C_TEXT_SECONDARY (color_theme->text_secondary)
#define C_NCARD_BG (color_theme->ncard_bg)
#define C_NCARD_TEXT (color_theme->ncard_text)
#define C_NCARD_TEXT_SECONDARY (color_theme->ncard_text_secondary)
#define C_NCARD_SHADOW (color_theme->ncard_shadow)
#define C_ROM_HIGHLIGHT (color_theme->rom_highlight)
#define C_ROM_HIGHLIGHT_SHADOW (color_theme->rom_highlight_shadow)
#define C_KEYACT_FRAME (color_theme->keyaction_frame)
#define C_BTN_FAKE_SHADOW (color_theme->btn_fake_shadow)

void colors_select(size_t color_theme_idx);
const struct mrom_color_theme *colors_get(size_t color_theme_idx);
int colors_count(void);

#endif
