#pragma once

// Copyright (c) 2013- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <string>
#include <vector>
#include <string_view>
#include <cstdint>

#include "Common/UI/Context.h"
#include "Common/Render/TextureAtlas.h"

void ReloadAllThemeInfo();

std::vector<std::string> GetThemeInfoNames();
void UpdateTheme();
const UI::Theme *GetTheme();

// Tryled theme extras.
std::string GetThemeDescription(std::string_view name);
uint32_t GetThemeBackgroundColor(std::string_view name);
uint32_t GetThemeAccentColor(std::string_view name);
// Returns false if the current theme doesn't set a touch button color. Color is 0x00BBGGRR.
bool GetThemeTouchButtonColor(uint32_t *color);
// Color for the symbols drawn on touch buttons, 0x00BBGGRR. White if the theme doesn't set it.
uint32_t GetThemeTouchIconColor();
// Copies the current theme's touch control style, opacity and background animation into g_Config.
void ApplyThemeSettings();
