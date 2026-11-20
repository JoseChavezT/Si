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

#include "ppsspp_config.h"

#include <algorithm>
#include <map>

#include "UI/Theme.h"

#include "Common/Data/Format/IniFile.h"
#include "Common/File/DirListing.h"
#include "Common/Log/LogManager.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Render/Text/draw_text.h"
#include "Core/Config.h"
#include "Common/StringUtils.h"

#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Core/System.h"

struct ThemeInfo {
	std::string name;

	uint32_t uItemStyleFg = 0xFFFFFFFF;
	uint32_t uItemStyleBg = 0x55000000;
	uint32_t uItemFocusedStyleFg = 0xFFFFFFFF;
	uint32_t uItemFocusedStyleBg = 0xFFEDC24C;
	uint32_t uItemDownStyleFg = 0xFFFFFFFF;
	uint32_t uItemDownStyleBg = 0xFFBD9939;
	uint32_t uItemDisabledStyleFg = 0x80EEEEEE;
	uint32_t uItemDisabledStyleBg = 0x55000000;

	uint32_t uHeaderStyleFg = 0xFFFFFFFF;
	uint32_t uHeaderStyleBg = 0x00000000;
	uint32_t uInfoStyleFg = 0xFFFFFFFF;
	uint32_t uInfoStyleBg = 0x00000000;
	uint32_t uPopupStyleFg = 0xFFFFFFFF;
	uint32_t uPopupStyleBg = 0xFF5E4D1F;
	uint32_t uPopupTitleStyleFg = 0xFFFFFFFF;
	uint32_t uPopupTitleStyleBg = 0x00000000;  // default to invisible
	uint32_t uTooltipStyleFg = 0xFFFFFFFF;
	uint32_t uTooltipStyleBg = 0xC0303030;
	uint32_t uCollapsibleHeaderStyleFg = 0xFFFFFFFF;
	uint32_t uCollapsibleHeaderStyleBg = 0x55000000;
	uint32_t uBackgroundColor = 0xFF754D24;
	uint32_t uScrollbarColor = 0x80FFFFFF;
	uint32_t uPopupSliderColor = 0xFFFFFFFF;
	uint32_t uPopupSliderFocusedColor = 0xFFEDC24C;

	// Tryled: a theme can also restyle the on-screen touch controls and the menu background.
	// Values < 0 (or has* == false) mean "not set by this theme, keep the user's setting".
	std::string description;
	bool hasTouchButtonColor = false;
	uint32_t uTouchButtonColor = 0xFFFFFFFF;
	bool hasTouchIconColor = false;
	uint32_t uTouchIconColor = 0xFFFFFFFF;
	int iTouchButtonStyle = -1;
	int iTouchButtonOpacity = -1;
	int iBackgroundAnimation = -1;

	bool operator == (const std::string &other) {
		return name == other;
	}
	bool operator == (const ThemeInfo &other) {
		return name == other.name;
	}
};

static UI::Theme ui_theme;
static std::vector<ThemeInfo> themeInfos;
static int currentThemeIndex = -1;

// Tryled's main theme. Used when the configured theme doesn't exist.
static const char *const TRYLED_DEFAULT_THEME = "Negro total";

static void LoadThemeInfo(const std::vector<Path> &directories) {
	themeInfos.clear();
	ThemeInfo def{};
	def.name = "Default";
	themeInfos.push_back(def);

	// This will update the theme if already present, as such default in assets/theme will get priority if exist
	auto appendTheme = [&](const ThemeInfo &info) {
		auto beginErase = std::remove(themeInfos.begin(), themeInfos.end(), info.name);
		if (beginErase != themeInfos.end()) {
			themeInfos.erase(beginErase, themeInfos.end());
		}
		themeInfos.push_back(info);
	};

	for (const auto &d : directories) {
		std::vector<File::FileInfo> fileInfo;
		g_VFS.GetFileListing(d.c_str(), &fileInfo, "ini:");

		if (fileInfo.empty()) {
			File::GetFilesInDir(d, &fileInfo, "ini:");
		}

		for (const auto &f : fileInfo) {
			IniFile ini;
			bool success = false;
			if (f.isDirectory) {
				continue;
			}

			Path name(f.fullName);
			Path path = d;
			// Hack around Android VFS path bug. really need to redesign this.
			if (name.ToString().substr(0, 7) == "assets/") {
				name = Path(name.ToString().substr(7));
			}
			if (path.ToString().substr(0, 7) == "assets/") {
				path = Path(path.ToString().substr(7));
			}

			if (ini.LoadFromVFS(g_VFS, name.ToString()) || ini.Load(f.fullName)) {
				success = true;
			}

			if (!success)
				continue;

			// Alright, let's loop through the sections and see if any is a theme.
			for (size_t i = 0; i < ini.Sections().size(); i++) {
				Section &section = *(ini.Sections()[i].get());

				if (section.name().empty()) {
					continue;
				}

				ThemeInfo info;
				info.name = section.name();
				section.Get("Name", &info.name);

				section.Get("ItemStyleFg", &info.uItemStyleFg);
				section.Get("ItemStyleBg", &info.uItemStyleBg);
				section.Get("ItemFocusedStyleFg", &info.uItemFocusedStyleFg);
				section.Get("ItemFocusedStyleBg", &info.uItemFocusedStyleBg);
				section.Get("ItemDownStyleFg", &info.uItemDownStyleFg);
				section.Get("ItemDownStyleBg", &info.uItemDownStyleBg);
				section.Get("ItemDisabledStyleFg", &info.uItemDisabledStyleFg);
				section.Get("ItemDisabledStyleBg", &info.uItemDisabledStyleBg);

				section.Get("HeaderStyleFg", &info.uHeaderStyleFg);
				section.Get("HeaderStyleBg", &info.uHeaderStyleBg);
				section.Get("InfoStyleFg", &info.uInfoStyleFg);
				section.Get("InfoStyleBg", &info.uInfoStyleBg);
				section.Get("PopupStyleFg", &info.uPopupStyleFg);  // Backwards compat
				section.Get("PopupStyleBg", &info.uPopupStyleBg);
				section.Get("TooltipStyleFg", &info.uTooltipStyleFg);  // Backwards compat
				section.Get("TooltipStyleBg", &info.uTooltipStyleBg);
				info.uPopupTitleStyleFg = info.uItemStyleFg;
				section.Get("PopupTitleStyleFg", &info.uPopupTitleStyleFg);
				section.Get("PopupTitleStyleBg", &info.uPopupTitleStyleBg);
				info.uCollapsibleHeaderStyleFg = info.uInfoStyleFg;
				info.uCollapsibleHeaderStyleBg = info.uInfoStyleBg;
				section.Get("CollapsibleHeaderStyleFg", &info.uCollapsibleHeaderStyleFg);  // Backwards compat
				section.Get("CollapsibleHeaderStyleBg", &info.uCollapsibleHeaderStyleBg);
				section.Get("BackgroundColor", &info.uBackgroundColor);
				section.Get("ScrollbarColor", &info.uScrollbarColor);
				section.Get("PopupSliderColor", &info.uPopupSliderColor);
				section.Get("PopupSliderFocusedColor", &info.uPopupSliderFocusedColor);

				section.Get("Description", &info.description);
				info.hasTouchButtonColor = section.Get("TouchButtonColor", &info.uTouchButtonColor);
				info.hasTouchIconColor = section.Get("TouchButtonIconColor", &info.uTouchIconColor);
				section.Get("TouchButtonStyle", &info.iTouchButtonStyle);
				section.Get("TouchButtonOpacity", &info.iTouchButtonOpacity);
				section.Get("BackgroundAnimation", &info.iBackgroundAnimation);

				appendTheme(info);
			}
		}
	}
}

static UI::Style MakeStyle(uint32_t fg, uint32_t bg) {
	UI::Style s;
	s.background = UI::Drawable(bg);
	s.fgColor = fg;
	return s;
}

// ---------------------------------------------------------------------------
// Tryled color overrides. Stored in g_Config.sTryledThemeColors as
//   "Theme name=key:RRGGBB,key:RRGGBB;Other theme=..."
// ---------------------------------------------------------------------------

static const char *const g_tryledColorKeys[(int)TryledColor::COUNT] = {
	"bg", "acc", "tab", "sel", "title", "text", "card", "btn", "btnt", "glow"
};

static std::map<std::string, std::map<std::string, uint32_t>> ParseTryledColors() {
	std::map<std::string, std::map<std::string, uint32_t>> result;
	std::vector<std::string> entries;
	SplitString(g_Config.sTryledThemeColors, ';', entries);
	for (const std::string &entry : entries) {
		size_t eq = entry.find('=');
		if (eq == std::string::npos)
			continue;
		auto &colors = result[entry.substr(0, eq)];
		std::vector<std::string> pairs;
		SplitString(entry.substr(eq + 1), ',', pairs);
		for (const std::string &kv : pairs) {
			size_t colon = kv.find(':');
			if (colon == std::string::npos)
				continue;
			const uint32_t rgb = (uint32_t)strtoul(kv.substr(colon + 1).c_str(), nullptr, 16);
			// RRGGBB -> 0xAABBGGRR
			colors[kv.substr(0, colon)] = 0xFF000000 | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
		}
	}
	return result;
}

static void StoreTryledColors(const std::map<std::string, std::map<std::string, uint32_t>> &all) {
	std::string out;
	for (const auto &theme : all) {
		if (theme.second.empty())
			continue;
		if (!out.empty())
			out += ";";
		out += theme.first + "=";
		bool first = true;
		for (const auto &kv : theme.second) {
			const uint32_t c = kv.second;
			const uint32_t rgb = ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
			char buf[32];
			snprintf(buf, sizeof(buf), "%s%s:%06X", first ? "" : ",", kv.first.c_str(), rgb);
			out += buf;
			first = false;
		}
	}
	g_Config.sTryledThemeColors = out;
}

bool HasTryledColor(TryledColor c) {
	auto all = ParseTryledColors();
	auto it = all.find(g_Config.sThemeName);
	return it != all.end() && it->second.count(g_tryledColorKeys[(int)c]) > 0;
}

uint32_t GetTryledColor(TryledColor c, uint32_t fallback) {
	auto all = ParseTryledColors();
	auto it = all.find(g_Config.sThemeName);
	if (it == all.end())
		return fallback;
	auto kv = it->second.find(g_tryledColorKeys[(int)c]);
	return kv == it->second.end() ? fallback : kv->second;
}

void SetTryledColor(TryledColor c, uint32_t color, bool set) {
	auto all = ParseTryledColors();
	auto &colors = all[g_Config.sThemeName];
	if (set)
		colors[g_tryledColorKeys[(int)c]] = color | 0xFF000000;
	else
		colors.erase(g_tryledColorKeys[(int)c]);
	StoreTryledColors(all);
	UpdateTheme();
}

void ResetTryledColors() {
	auto all = ParseTryledColors();
	all.erase(g_Config.sThemeName);
	StoreTryledColors(all);
	UpdateTheme();
}

static uint32_t MixColors(uint32_t a, uint32_t b, float t) {
	auto ch = [&](int shift) {
		const float x = ((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t;
		return ((uint32_t)x & 0xFF) << shift;
	};
	return ch(0) | ch(8) | ch(16) | ch(24);
}

void UpdateTheme() {
	// First run, get the default in at least
	if (themeInfos.empty()) {
		ReloadAllThemeInfo();
	}

	int defaultThemeIndex = -1;
	int selectedThemeIndex = -1;
	for (int i = 0; i < themeInfos.size(); ++i) {
		if (themeInfos[i].name == "Default") {
			defaultThemeIndex = i;
		}
		if (themeInfos[i].name == g_Config.sThemeName) {
			selectedThemeIndex = i;
		}
	}

	// Reset to Tryled's main theme (or Default) if not found
	if (selectedThemeIndex < 0 || selectedThemeIndex >= themeInfos.size()) {
		for (int i = 0; i < themeInfos.size(); ++i) {
			if (themeInfos[i].name == TRYLED_DEFAULT_THEME) {
				defaultThemeIndex = i;
			}
		}
		g_Config.sThemeName = defaultThemeIndex >= 0 ? themeInfos[defaultThemeIndex].name : "Default";
		selectedThemeIndex = defaultThemeIndex;
		if (selectedThemeIndex < 0) {
			_dbg_assert_(false);
			// No themes? Bad.
			return;
		}
	}

	// Desktop font override support
	auto des = GetI18NCategory(I18NCat::DESKTOPUI);
	std::string_view fontOverride = des->T("Font", "");
	if (fontOverride == "Font") {
		fontOverride = "";
	}
	if (!fontOverride.empty()) {
		SetFontNameOverride(FontFamily::SansSerif, fontOverride);
	}

	ui_theme.uiFontTiny = FontStyle(FontFamily::SansSerif, 14, FontStyleFlags::Default);
	ui_theme.uiFontSmall = FontStyle(FontFamily::SansSerif, 17, FontStyleFlags::Default);
	ui_theme.uiFont = FontStyle(FontFamily::SansSerif, 22, FontStyleFlags::Default);
	ui_theme.uiFontBig = FontStyle(FontFamily::SansSerif, 28, FontStyleFlags::Bold);
	ui_theme.uiFontCode = FontStyle(FontFamily::Fixed, 14, FontStyleFlags::Default);

	ui_theme.checkOn = ImageID("I_CHECKEDBOX");
	ui_theme.checkOff = ImageID("I_UNCHECKEDBOX");
	ui_theme.whiteImage = ImageID("I_SOLIDWHITE");
	ui_theme.sliderKnob = ImageID("I_CIRCLE");
	ui_theme.dropShadow4Grid = ImageID("I_DROP_SHADOW");

	currentThemeIndex = selectedThemeIndex;
	const ThemeInfo &themeInfo = themeInfos[selectedThemeIndex];

	// Actual configurable themes setting start here
	ui_theme.itemStyle = MakeStyle(themeInfo.uItemStyleFg, themeInfo.uItemStyleBg);
	ui_theme.itemFocusedStyle = MakeStyle(themeInfo.uItemFocusedStyleFg, themeInfo.uItemFocusedStyleBg);
	ui_theme.itemDownStyle = MakeStyle(themeInfo.uItemDownStyleFg, themeInfo.uItemDownStyleBg);
	ui_theme.itemDisabledStyle = MakeStyle(themeInfo.uItemDisabledStyleFg, themeInfo.uItemDisabledStyleBg);

	ui_theme.headerStyle = MakeStyle(themeInfo.uHeaderStyleFg, themeInfo.uHeaderStyleBg);
	ui_theme.collapsibleHeaderStyle = MakeStyle(themeInfo.uCollapsibleHeaderStyleFg, themeInfo.uCollapsibleHeaderStyleBg);
	ui_theme.infoStyle = MakeStyle(themeInfo.uInfoStyleFg, themeInfo.uInfoStyleBg);

	ui_theme.popupStyle = MakeStyle(themeInfo.uPopupStyleFg, themeInfo.uPopupStyleBg);
	ui_theme.popupTitleStyle = MakeStyle(themeInfo.uPopupTitleStyleFg, themeInfo.uPopupTitleStyleBg);

	ui_theme.tooltipStyle = MakeStyle(themeInfo.uTooltipStyleFg, themeInfo.uTooltipStyleBg);

	ui_theme.backgroundColor = themeInfo.uBackgroundColor;
	ui_theme.scrollbarColor = themeInfo.uScrollbarColor;

	ui_theme.popupSliderColor = themeInfo.uPopupSliderColor;

	// Tryled: per-theme background and accent overrides (Personalizar > Temas).
	{
		auto all = ParseTryledColors();
		auto it = all.find(themeInfo.name);
		if (it != all.end()) {
			auto bg = it->second.find("bg");
			if (bg != it->second.end()) {
				const uint32_t fg = themeInfo.uItemStyleFg;
				ui_theme.backgroundColor = bg->second;
				const uint32_t panel = MixColors(bg->second, fg | 0xFF000000, 0.08f);
				ui_theme.itemStyle.background = UI::Drawable(panel);
				ui_theme.popupStyle.background = UI::Drawable(MixColors(bg->second, fg | 0xFF000000, 0.12f));
				ui_theme.collapsibleHeaderStyle.background = UI::Drawable(panel);
			}
			auto acc = it->second.find("acc");
			if (acc != it->second.end()) {
				ui_theme.itemFocusedStyle.background = UI::Drawable(acc->second);
				ui_theme.itemDownStyle.background = UI::Drawable(MixColors(acc->second, 0xFF000000, 0.2f));
				ui_theme.headerStyle.fgColor = acc->second;
				ui_theme.popupTitleStyle.fgColor = acc->second;
				ui_theme.popupSliderFocusedColor = acc->second;
				ui_theme.scrollbarColor = (acc->second & 0xFFFFFF) | 0x80000000;
			}
		}
	}

	// Tryled: rounded items everywhere, following the corner setting (Personalizar > Letra y forma).
	ui_theme.itemRadius = std::clamp(g_Config.iTryledCornerRadius, 0, 30) * 0.75f;
	ui_theme.itemGap = ui_theme.itemRadius > 0.0f ? 4.0f : 0.0f;
	ui_theme.popupSliderFocusedColor = themeInfo.uPopupSliderFocusedColor;
}

const UI::Theme *GetTheme() {
	return &ui_theme;
}

void ReloadAllThemeInfo() {
	std::vector<Path> directories;
	directories.push_back(Path("themes"));  // For VFS
	directories.push_back(GetSysDirectory(DIRECTORY_CUSTOM_THEMES));
	LoadThemeInfo(directories);
}

std::vector<std::string> GetThemeInfoNames() {
	std::vector<std::string> names;
	for (const auto &info : themeInfos) {
		names.push_back(info.name);
	}
	return names;
}

static const ThemeInfo *CurrentThemeInfo() {
	if (currentThemeIndex < 0 || currentThemeIndex >= (int)themeInfos.size())
		return nullptr;
	return &themeInfos[currentThemeIndex];
}

std::string GetThemeDescription(std::string_view name) {
	for (const auto &info : themeInfos) {
		if (info.name == name)
			return info.description;
	}
	return "";
}

uint32_t GetThemeBackgroundColor(std::string_view name) {
	for (const auto &info : themeInfos) {
		if (info.name == name)
			return info.uBackgroundColor;
	}
	return 0xFF000000;
}

uint32_t GetThemeAccentColor(std::string_view name) {
	for (const auto &info : themeInfos) {
		if (info.name == name)
			return info.uItemFocusedStyleBg;
	}
	return 0xFFFFFFFF;
}

bool GetThemeTouchButtonColor(uint32_t *color) {
	const ThemeInfo *info = CurrentThemeInfo();
	if (!info || !info->hasTouchButtonColor)
		return false;
	*color = info->uTouchButtonColor & 0x00FFFFFF;
	return true;
}

uint32_t GetThemeTouchIconColor() {
	const ThemeInfo *info = CurrentThemeInfo();
	if (!info || !info->hasTouchIconColor)
		return 0xFFFFFF;
	return info->uTouchIconColor & 0x00FFFFFF;
}

void ApplyThemeSettings() {
	const ThemeInfo *info = CurrentThemeInfo();
	if (!info)
		return;
	if (info->iTouchButtonStyle >= 0)
		g_Config.iTouchButtonStyle = info->iTouchButtonStyle;
	if (info->iTouchButtonOpacity >= 0)
		g_Config.iTouchButtonOpacity = info->iTouchButtonOpacity;
	if (info->iBackgroundAnimation >= 0)
		g_Config.iBackgroundAnimation = info->iBackgroundAnimation;
}
