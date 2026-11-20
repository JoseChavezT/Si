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

#include <algorithm>
#include <cmath>
#include <sstream>

#include "ppsspp_config.h"

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/GameManager.h"

#include "UI/BackgroundAudio.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/IAPScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/InstallZipScreen.h"
#include "UI/Background.h"
#include "UI/GameBrowser.h"
#include "UI/RetroScreen.h"
#include "UI/TryledDraw.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Util/DarwinFileSystemServices.h" // For the browser

#include "Core/HLE/sceUmd.h"
#include "Core/SaveState.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/File/DirListing.h"
#include <mutex>
#include <atomic>
#include <ctime>

bool MainScreen::showHomebrewTab = false;

// Tryled: whether the classic browser ("Biblioteca") is shown instead of the console home, and the selected game.
static bool s_tryledLibraryMode = false;
static int s_tryledSelected = 0;

static void LaunchFile(ScreenManager *screenManager, Screen *currentScreen, const Path &path) {
	std::string extension = path.GetFileExtension();
	if (extension == ".zip" || extension == ".7z") {
		// If is a zip file, we have a screen for that.
		screenManager->push(new InstallZipScreen(path));
	} else {
		// Check if we already know that this game isn't playable.
		auto info = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::FILE_TYPE);

		switch (info->fileType) {
		case IdentifiedFileType::PSP_UMD_VIDEO_ISO:
			// We show info about it.
			screenManager->push(new GameScreen(path, false));
			return;
		case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
		{
			// Show the savedata popup, why not?
			std::string title = SanitizeString(info->GetTitle(), StringRestriction::NoLineBreaksOrSpecials, 0, 200);
			screenManager->push(new SavedataPopupScreen(Path(), path, title));
			return;
		}
		default:
			break;
		}
		if (currentScreen) {
			screenManager->cancelScreensAbove(currentScreen);
		}
		// Otherwise let the EmuScreen take care of it, including error handling.
		screenManager->switchScreen(new EmuScreen(path));
	}
}

static bool IsTempPath(const Path &str) {
	std::string item = str.ToString();

#ifdef _WIN32
	// Normalize slashes.
	item = ReplaceAll(item, "/", "\\");
#endif

	std::vector<std::string> tempPaths = System_GetPropertyStringVec(SYSPROP_TEMP_DIRS);
	for (auto temp : tempPaths) {
#ifdef _WIN32
		temp = ReplaceAll(temp, "/", "\\");
		if (!temp.empty() && temp[temp.size() - 1] != '\\')
			temp += "\\";
#else
		if (!temp.empty() && temp[temp.size() - 1] != '/')
			temp += "/";
#endif
		if (startsWith(item, temp))
			return true;
	}

	return false;
}

MainScreen::MainScreen() {
	g_BackgroundAudio.SetGame(Path());
}

MainScreen::~MainScreen() {
	g_BackgroundAudio.SetGame(Path());
}

bool MainScreen::WantsTextInput() const {
	// We don't want to pop a software keyboard on the main screen, just for type-to-search.
	return !System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT);
}


#if PPSSPP_PLATFORM(IOS)
constexpr std::string_view getGamesUri = "https://www.ppsspp.org/getgames_ios";
constexpr std::string_view getHomebrewUri = "https://www.ppsspp.org/gethomebrew_ios";
#else
constexpr std::string_view getGamesUri = "https://www.ppsspp.org/getgames";
constexpr std::string_view getHomebrewUri = "https://www.ppsspp.org/gethomebrew";
#endif
constexpr std::string_view remoteGamesUri = "https://www.ppsspp.org/docs/reference/disc-streaming";

void MainScreen::CreateRecentTab() {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	LinearLayout *tabContainer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabContainer->SetSpacing(0.0f);
	SearchBar *search = tabContainer->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 8, 8, 0))));

	ScrollView *scrollView = tabContainer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	scrollView->SetTag("MainScreenRecentGames");

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	GameBrowser *tabRecentGames = new GameBrowser(GetRequesterToken(),
		Path("!RECENT"), BrowseFlags::NONE, portrait, &g_Config.bGridView1, screenManager(), "", "",
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabRecentGames->SetSearchBar(search);

	scrollView->Add(tabRecentGames);
	gameBrowsers_.push_back(tabRecentGames);

	tabHolder_->AddTab(mm->T("Recent"), ImageID::invalid(), tabContainer);
	tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	tabRecentGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	tabRecentGames->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);
}

GameBrowser *MainScreen::CreateBrowserTab(const Path &path, std::string_view title, std::string_view howToTitle, std::string_view howToUri, BrowseFlags browseFlags, bool *bGridView, float *scrollPos) {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	LinearLayout *tabContainer = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabContainer->SetSpacing(0.0f);
	SearchBar *search = tabContainer->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	ScrollView *scrollView = tabContainer->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f)));
	scrollView->SetTag(title);  // Re-use title as tag, should be fine.

	GameBrowser *gameBrowser = new GameBrowser(GetRequesterToken(), path, browseFlags, portrait, bGridView, screenManager(),
		mm->T(howToTitle), howToUri,
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	gameBrowser->SetSearchBar(search);

	scrollView->Add(gameBrowser);
	gameBrowsers_.push_back(gameBrowser);

	tabHolder_->AddTab(mm->T(title), ImageID::invalid(), tabContainer);
	if (scrollPos) {
		scrollView->RememberPosition(scrollPos);
	}

	gameBrowser->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	gameBrowser->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	gameBrowser->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);

	return gameBrowser;
}

class LogoView : public UI::AnchorLayout {
public:
	LogoView(bool portrait, UI::LayoutParams *layoutParams) : UI::AnchorLayout(layoutParams), portrait_(portrait) {}
	void Draw(UIContext &dc) override {
		using namespace UI;
		UI::AnchorLayout::Draw(dc);

		// Tryled: our name instead of PPSSPP's icon and logo.
		dc.SetFontStyle(dc.GetTheme().uiFontBig);
		dc.SetFontScale(1.4f, 1.4f);
		dc.DrawText("Tryled", bounds_.x + 8, bounds_.y + 2, dc.GetTheme().itemFocusedStyle.background.color, ALIGN_TOPLEFT);
		dc.SetFontScale(1.0f, 1.0f);

		std::string versionString = PPSSPP_GIT_VERSION;
		if (versionString.size() > 2 && versionString[0] == 'v' && isdigit(versionString[1])) {
			versionString = versionString.substr(1);
		}
		const FontStyle *style = GetTextStyle(dc, TextSize::Tiny);
		dc.SetFontStyle(*style);
		dc.DrawText("Basado en PPSSPP " + versionString, bounds_.x + 10, bounds_.y + 58, dc.GetTheme().infoStyle.fgColor);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *iconImg = dc.Draw()->GetAtlas()->getImage(GetIconID());
		w = iconImg->w;
		h = iconImg->h;
	}

	bool Touch(const TouchInput &touch) override {
		bool retval = UI::AnchorLayout::Touch(touch);
		if (!portrait_ && (touch.flags & TouchInputFlags::DOWN) && bounds_.Contains(touch.x, touch.y) && touch.y >= bounds_.y2() - 20) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			System_CopyStringToClipboard(PPSSPP_GIT_VERSION);
			g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions(di->T("Copied to clipboard: %1"), PPSSPP_GIT_VERSION), 0.0f, "copyToClip");
			return true;
		}
		return retval;
	}

private:
	ImageID GetIconID() const {
		return System_GetPropertyBool(SYSPROP_APP_GOLD) ? ImageID("I_ICON_GOLD") : ImageID("I_ICON");
	}

	const bool portrait_;
};

void MainScreen::CreateMainButtons(UI::ViewGroup *parent, bool portrait) {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	if (portrait) {
		parent->Add(new Spacer(1.0f, new LinearLayoutParams(1.0f)));
	}
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		parent->Add(portrait ? new Choice(ImageID("I_FOLDER_OPEN"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("Load", "Load...")))->OnClick.Handle(this, &MainScreen::OnLoadFile);
	}
	parent->Add(portrait ? new Choice(ImageID("I_GEAR"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("Game Settings", "Settings")))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	// Tryled: back to the console-style home. (PPSSPP's about/website/Gold buttons removed.)
	parent->Add(portrait ? new Choice(ImageID("I_HOME"), portrait ? new LinearLayoutParams() : nullptr) : new Choice("Volver a Juegos", ImageID("I_HOME")))->OnClick.Add([this](UI::EventParams &) {
		s_tryledLibraryMode = false;
		RecreateViews();
	});
	parent->Add(portrait ? new Choice(ImageID("I_INFO"), portrait ? new LinearLayoutParams() : nullptr) : new Choice("Créditos"))->OnClick.Handle(this, &MainScreen::OnCredits);

	if (!portrait) {
		parent->Add(new Spacer(16.0));
	}

	// Remove the exit button in vertical layout on all platforms, just no space.
	bool showExitButton = !portrait;
	// Also, always hide the exit button on mobile platforms that are not supposed to have one.
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	showExitButton = false;
#elif PPSSPP_PLATFORM(ANDROID)
	// The exit button previously created problems on Android.
	// However now we allow it in landscape mode.
	showExitButton = !portrait; //  System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV;
#endif
	// Officially, iOS apps should not have exit buttons. Remove it to maximize app store review chances.
	if (showExitButton) {
		parent->Add(new Choice(di->T("Exit")))->OnClick.Add([](UI::EventParams &e) {
			// Let's make sure the config was saved, since it may not have been.
			if (!g_Config.Save("MainScreen::OnExit")) {
				System_Toast("Failed to save settings!\nCheck permissions, or try to restart the device.");
			}

			UpdateUIState(UISTATE_EXIT);
			// Request the framework to exit cleanly.
			System_ExitApp();
		});
	}
}

// ============================================================================
// Tryled: "Modo consola" home screen.
// A row of games at the top (most recently played first), the selected game's
// artwork filling the background, a big title, a Play button and info cards.
// "Biblioteca" switches to the classic PPSSPP file browser.
// ============================================================================

// Animation clocks (time_now_d() based).
static double s_consoleOpenTime = 0.0;    // When the console home was (re)created: intro animation.
static double s_consoleSelectTime = 0.0;  // When the selected game changed: title slide-in.
static double s_consoleArtTime = 0.0;     // When the background art changed: crossfade.
static Path s_consolePrevArt;             // Art fading out.
static bool s_startupSoundPlayed = false;

// Everything the console home needs about a game, always requested together (requesting it piecemeal
// sometimes left the icon or the artwork unloaded for the first game).
static const GameInfoFlags kConsoleInfoFlags = GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::PIC1;

namespace {

float EaseOut(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

uint32_t ThemeAccent(const UIContext &dc) {
	return dc.GetTheme().itemFocusedStyle.background.color;
}

uint32_t ThemeAccentText(const UIContext &dc) {
	return dc.GetTheme().itemFocusedStyle.fgColor;
}

// Text that slides in from the left and fades in whenever the selected game changes.
// scale > 1 draws bigger than the theme's biggest font (used for the title).
class ConsoleAnimText : public UI::TextView {
public:
	ConsoleAnimText(float scale, bool small, float delay, UI::LayoutParams *layoutParams)
		: UI::TextView("", layoutParams), scale_(scale), small_(small), delay_(delay) {}
	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override {
		w = horiz.size > 0 ? horiz.size : 600.0f;
		h = scale_ > 1.0f ? 40.0f * scale_ : (small_ ? 28.0f : 34.0f);
	}
	void Draw(UIContext &dc) override {
		const float t = EaseOut(float(time_now_d() - std::max(s_consoleSelectTime, s_consoleOpenTime + 0.15) - delay_) / 0.4f);
		Bounds b = bounds_;
		b.x += (1.0f - t) * 36.0f;
		const uint32_t color = colorAlpha(dc.GetTheme().infoStyle.fgColor & 0xFFFFFF, t * (small_ ? 0.8f : 1.0f));
		if (scale_ > 1.0f) {
			dc.SetFontStyle(dc.GetTheme().uiFontBig);
			dc.SetFontScale(scale_, scale_);
		} else {
			dc.SetFontStyle(small_ ? dc.GetTheme().uiFontSmall : dc.GetTheme().uiFont);
		}
		dc.DrawTextRectSqueeze(GetText(), b, color, ALIGN_LEFT | ALIGN_VCENTER);
		dc.SetFontScale(1.0f, 1.0f);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
private:
	float scale_;
	bool small_;
	float delay_;
};

// Soft particles of light drifting upwards, in the theme accent color. Purely decorative.
void DrawConsoleAmbient(UIContext &dc, double now) {
	const Bounds &b = dc.GetBounds();
	const uint32_t accent = ThemeAccent(dc) & 0xFFFFFF;
	for (int i = 0; i < 22; i++) {
		// Cheap deterministic pseudo-random values per particle.
		const float r1 = fmodf(sinf(i * 12.9898f) * 43758.547f, 1.0f) * 0.5f + 0.5f;
		const float r2 = fmodf(sinf(i * 78.233f) * 12543.31f, 1.0f) * 0.5f + 0.5f;
		const float r3 = fmodf(sinf(i * 39.425f) * 24634.63f, 1.0f) * 0.5f + 0.5f;
		const float speed = 0.012f + 0.02f * r2;
		const float phase = fmodf(r1 + (float)now * speed, 1.0f);
		const float x = b.x + b.w * (0.35f + 0.65f * r3) + sinf((float)now * 0.4f + i) * 18.0f;
		const float y = b.y2() - phase * (b.h + 40.0f);
		const float fade = sinf(phase * 3.14159f);
		dc.Draw()->DrawImage(ImageID("I_CIRCLE"), x, y, 0.15f + 0.35f * r2, colorAlpha(accent, 0.22f * fade), ALIGN_CENTER);
	}
}

// Draws a texture covering the whole screen, slightly zoomed, drifting very slowly.
void DrawConsoleArt(UIContext &dc, Draw::Texture *texture, float alpha, double now) {
	const Bounds &sb = dc.GetBounds();
	const float texAspect = (float)texture->Width() / (float)texture->Height();
	float w = sb.w, h = sb.w / texAspect;
	if (h < sb.h) {
		h = sb.h;
		w = sb.h * texAspect;
	}
	w *= 1.08f;
	h *= 1.08f;
	const float dx = sinf((float)now * 0.06f) * sb.w * 0.018f;
	const float dy = cosf((float)now * 0.045f) * sb.h * 0.014f;
	Bounds art = Bounds::FromCenterWH(sb.centerX() + dx, sb.centerY() + dy, w, h);
	dc.Flush();
	dc.GetDrawContext()->BindTexture(0, texture);
	dc.Draw()->DrawTexRect(art, 0, 0, 1, 1, colorAlpha(0xFFFFFF, std::clamp(alpha, 0.0f, 1.0f)));
	dc.Flush();
	dc.RebindTexture();
}

std::string FormatPlayTime(int seconds) {
	const int hours = seconds / 3600;
	const int minutes = (seconds / 60) % 60;
	char buf[64];
	if (hours > 0)
		snprintf(buf, sizeof(buf), "%d h %02d min", hours, minutes);
	else
		snprintf(buf, sizeof(buf), "%d min", minutes);
	return buf;
}

std::string ClockString() {
	time_t now = time(nullptr);
	struct tm local{};
#if PPSSPP_PLATFORM(WINDOWS)
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
	return buf;
}

// ---- Tryled console home: views (layout and look follow the approved preview) ----

static const float kTile = 56.0f;      // Unselected game tile (square).
static const float kTileSel = 84.0f;   // Selected game tile.
static const float kRadius = 12.0f;    // Corner radius for tiles, cards and the box art.

// What the home shows about the selected game. Filled by ConsoleRefreshInfo(), read by the views.
struct ConsoleGameDetails {
	bool empty = true;
	bool psp = false;
	std::string console;
	Path path;
	std::string title;
	bool played = false;
	int playedSeconds = 0;
	std::string lastPlayed;
	std::vector<bool> slots;
	bool customConfig = false;
	std::string coreName;
};
static ConsoleGameDetails s_details;

uint32_t ThemeFg(const UIContext &dc) {
	return dc.GetTheme().infoStyle.fgColor;
}

uint32_t ThemePanel(const UIContext &dc) {
	return dc.GetTheme().itemStyle.background.color | 0xFF000000;
}

uint32_t WithAlpha(uint32_t color, float a) {
	return colorAlpha(color & 0xFFFFFF, std::clamp(a, 0.0f, 1.0f));
}

// Short label for console badges.
std::string ConsoleShort(const std::string &console) {
	if (console == "Mega Drive") return "MD";
	if (console == "Master System") return "SMS";
	if (console == "Game Gear") return "GG";
	if (console == "PC Engine") return "PCE";
	if (console == "Atari 2600") return "2600";
	if (console == "Atari 7800") return "7800";
	if (console == "WonderSwan") return "WS";
	return console;
}

// A stable, pleasant color for games without artwork, from their name.
uint32_t ColorFromName(const std::string &name, float lightness) {
	uint32_t h = 2166136261u;
	for (char c : name)
		h = (h ^ (uint8_t)c) * 16777619u;
	const float hue = (h % 360) / 360.0f;
	// HSV -> RGB with saturation 0.65.
	const float s = 0.65f, v = lightness;
	const int i = (int)(hue * 6.0f);
	const float f = hue * 6.0f - i;
	const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	float r, g, b;
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	return 0xFF000000 | ((uint32_t)(b * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(r * 255);
}

std::string Initials(const std::string &title) {
	std::string initials;
	bool newWord = true;
	for (char c : title) {
		if (c == ' ' || c == '_' || c == '-' || c == '.') {
			newWord = true;
		} else if (newWord && initials.size() < 2 && (unsigned char)c < 0x80 && isalnum((unsigned char)c)) {
			initials.push_back((char)toupper((unsigned char)c));
			newWord = false;
		} else {
			newWord = false;
		}
	}
	return initials;
}

// Box art / tile fill for a game: its icon (or artwork) cropped to the box, or a colored square with initials.
void DrawGameArt(UIContext &dc, const Path &path, const std::string &console, const Bounds &b, float radius, bool preferPic1, uint32_t tint) {
	Draw::Texture *tex = nullptr;
	std::string title;
	if (console == "PSP") {
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), path, kConsoleInfoFlags);
		if (preferPic1 && ginfo->Ready(GameInfoFlags::PIC1) && ginfo->pic1.texture)
			tex = ginfo->pic1.texture;
		else if (ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture)
			tex = ginfo->icon.texture;
		if (ginfo->Ready(GameInfoFlags::PARAM_SFO))
			title = ginfo->GetTitle();
	}
	if (title.empty())
		title = path.WithReplacedExtension("").GetFilename();
	if (tex) {
		float u1, v1, u2, v2;
		TryledDraw::CoverUV((float)tex->Width() / (float)tex->Height(), b.w / b.h, &u1, &v1, &u2, &v2);
		TryledDraw::TextureRoundRect(dc, tex, b, radius, tint, u1, v1, u2, v2);
	} else {
		TryledDraw::FillRoundRect(dc, b, radius, ColorFromName(title, 0.55f));
		dc.SetFontStyle(dc.GetTheme().uiFontBig);
		const float scale = b.w / 84.0f;
		dc.SetFontScale(scale, scale);
		dc.DrawText(Initials(title), b.centerX(), b.centerY(), 0xFFFFFFFF, ALIGN_CENTER);
		dc.SetFontScale(1.0f, 1.0f);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
}

// One game in the top row: a rounded square that grows when selected, with a white frame and a soft glow.
class ConsoleGameTile : public UI::Clickable {
public:
	ConsoleGameTile(const Path &path, const std::string &console, bool showBadge, int index, std::function<bool(int)> isSelected, std::function<void(int)> onFocus)
		: UI::Clickable(new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT, 0.0f, UI::Gravity::G_VCENTER)),
		  path_(path), console_(console), showBadge_(showBadge), index_(index), isSelected_(isSelected), onFocus_(onFocus) {
		grow_ = isSelected_(index_) ? 1.0f : 0.0f;
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = h = kTile + (kTileSel - kTile) * grow_;
	}

	void FocusChanged(UI::FocusFlags focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
		if ((focusFlags & UI::FocusFlags::GOT_FOCUS) && onFocus_)
			onFocus_(index_);
	}

	void Draw(UIContext &dc) override {
		const bool selected = isSelected_(index_);
		const double now = time_now_d();
		const float dt = lastDraw_ > 0.0 ? (float)std::min(now - lastDraw_, 0.1) : 1.0f;
		lastDraw_ = now;
		grow_ += ((selected ? 1.0f : 0.0f) - grow_) * std::min(1.0f, dt * 14.0f);

		const float intro = EaseOut(float(now - s_consoleOpenTime - index_ * 0.04) / 0.45f);
		Bounds b = bounds_;
		b.y -= (1.0f - intro) * 26.0f;
		const uint32_t accent = ThemeAccent(dc);

		if (grow_ > 0.01f) {
			const float pulse = 0.75f + 0.25f * sinf((float)now * 2.6f);
			TryledDraw::Glow(dc, b.Expand(3.0f), kRadius + 3.0f, 18.0f, WithAlpha(accent, 0.9f * grow_ * pulse * intro));
			TryledDraw::StrokeRoundRect(dc, b.Expand(5.0f), kRadius + 5.0f, 2.0f, WithAlpha(ThemeFg(dc), grow_ * intro));
		}
		const float bright = 0.78f + 0.22f * grow_;
		DrawGameArt(dc, path_, console_, b, kRadius, false, WithAlpha(colorBlend(0xFFFFFF, 0x000000, bright), intro));
		if (grow_ < 0.99f)
			TryledDraw::FillRoundRect(dc, b, kRadius, WithAlpha(0x000000, 0.22f * (1.0f - grow_) * intro));

		if (showBadge_) {
			const std::string label = ConsoleShort(console_);
			dc.SetFontStyle(dc.GetTheme().uiFontTiny);
			float tw, th;
			dc.MeasureText(dc.GetTheme().uiFontTiny, 0.55f, 0.55f, label, &tw, &th);
			Bounds badge(b.x + 3.0f, b.y2() - th - 5.0f, tw + 6.0f, th + 2.0f);
			TryledDraw::FillRoundRect(dc, badge, 3.0f, WithAlpha(0x000000, 0.6f * intro));
			dc.SetFontScale(0.55f, 0.55f);
			dc.DrawText(label, badge.x + 3.0f, badge.y + 1.0f, WithAlpha(0xFFFFFF, intro), ALIGN_TOPLEFT);
			dc.SetFontScale(1.0f, 1.0f);
			dc.SetFontStyle(dc.GetTheme().uiFont);
		}
		if (HasFocus() && !selected)
			TryledDraw::StrokeRoundRect(dc, b.Expand(3.0f), kRadius + 3.0f, 2.0f, accent);
	}

private:
	Path path_;
	std::string console_;
	bool showBadge_;
	int index_;
	std::function<bool(int)> isSelected_;
	std::function<void(int)> onFocus_;
	float grow_ = 0.0f;
	double lastDraw_ = 0.0;
};

// Rounded square with an icon: the "library" tile at the end of the row, and the small top bar buttons.
class ConsoleIconButton : public UI::Clickable {
public:
	ConsoleIconButton(ImageID image, float size, bool panel, float iconScale = 0.85f)
		: UI::Clickable(new UI::LinearLayoutParams(size, size, 0.0f, UI::Gravity::G_VCENTER)), image_(image), size_(size), panel_(panel), iconScale_(iconScale) {}
	void Draw(UIContext &dc) override {
		const uint32_t fg = ThemeFg(dc);
		if (panel_)
			TryledDraw::FillRoundRect(dc, bounds_, kRadius, ThemePanel(dc));
		else if (HasFocus() || down_)
			TryledDraw::FillRoundRect(dc, bounds_, bounds_.h * 0.5f, WithAlpha(fg, 0.14f));
		if (HasFocus())
			TryledDraw::StrokeRoundRect(dc, bounds_.Expand(3.0f), panel_ ? kRadius + 3.0f : bounds_.h * 0.5f + 3.0f, 2.0f, ThemeAccent(dc));
		dc.Draw()->DrawImage(image_, bounds_.centerX(), bounds_.centerY(), iconScale_ * size_ / 48.0f, WithAlpha(fg, 0.9f), ALIGN_CENTER);
	}
private:
	ImageID image_;
	float size_;
	bool panel_;
	float iconScale_;
};

// Profile circle with an initial (from the PSP nickname setting).
class ConsoleAvatar : public UI::InertView {
public:
	ConsoleAvatar() : UI::InertView(new UI::LinearLayoutParams(30.0f, 30.0f, 0.0f, UI::Gravity::G_VCENTER)) {}
	void Draw(UIContext &dc) override {
		const float d = std::min(bounds_.w, bounds_.h);
		const Bounds c = Bounds::FromCenter(bounds_.centerX(), bounds_.centerY(), d * 0.5f);
		TryledDraw::FillRoundRect(dc, c, d * 0.5f, ThemeAccent(dc));
		TryledDraw::StrokeRoundRect(dc, c, d * 0.5f, 2.0f, WithAlpha(ThemeFg(dc), 0.45f));
		std::string letter = "T";
		if (!g_Config.sNickName.empty() && (unsigned char)g_Config.sNickName[0] < 0x80)
			letter = std::string(1, (char)toupper((unsigned char)g_Config.sNickName[0]));
		dc.SetFontStyle(dc.GetTheme().uiFontSmall);
		dc.DrawText(letter, c.centerX(), c.centerY(), ThemeAccentText(dc), ALIGN_CENTER);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
};

// Subtitle under the title: console badge + text, sliding in with the title.
class ConsoleSubText : public ConsoleAnimText {
public:
	ConsoleSubText(UI::LayoutParams *lp) : ConsoleAnimText(1.0f, true, 0.1f, lp) {}
	void SetBadge(const std::string &badge) { badge_ = badge; }
	void Draw(UIContext &dc) override {
		const float t = EaseOut(float(time_now_d() - std::max(s_consoleSelectTime, s_consoleOpenTime + 0.15) - 0.1) / 0.4f);
		float x = bounds_.x + (1.0f - t) * 36.0f;
		const uint32_t fg = WithAlpha(ThemeFg(dc), t);
		dc.SetFontStyle(dc.GetTheme().uiFontSmall);
		if (!badge_.empty()) {
			float tw, th;
			dc.SetFontScale(0.75f, 0.75f);
			dc.MeasureText(dc.GetTheme().uiFontSmall, 0.75f, 0.75f, badge_, &tw, &th);
			Bounds bb(x, bounds_.centerY() - th * 0.5f - 2.0f, tw + 10.0f, th + 4.0f);
			TryledDraw::StrokeRoundRect(dc, bb, 4.0f, 1.0f, WithAlpha(ThemeFg(dc), 0.55f * t));
			dc.DrawText(badge_, bb.x + 5.0f, bounds_.centerY(), fg, ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
			x += bb.w + 8.0f;
		}
		dc.DrawTextRectSqueeze(GetText(), Bounds(x, bounds_.y, bounds_.x2() - x, bounds_.h), WithAlpha(ThemeFg(dc), 0.85f * t), ALIGN_LEFT | ALIGN_VCENTER);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
private:
	std::string badge_;
};

// "Jugar": a pill. White on the Negro total theme, the accent color on the others.
class ConsolePlayButton : public UI::Clickable {
public:
	ConsolePlayButton(std::string_view text) : UI::Clickable(new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT, 0.0f, UI::Gravity::G_VCENTER)), text_(text) {}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		float tw, th;
		dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, &tw, &th);
		w = tw + 64.0f;
		h = 44.0f;
	}
	void Draw(UIContext &dc) override {
		const bool white = g_Config.sThemeName == "Negro total";
		uint32_t bg = white ? WithAlpha(ThemeFg(dc), 0.92f) : ThemeAccent(dc);
		uint32_t fg = white ? (dc.GetTheme().backgroundColor | 0xFF000000) : ThemeAccentText(dc);
		if (down_) {
			bg = dc.GetTheme().itemDownStyle.background.color;
			fg = dc.GetTheme().itemDownStyle.fgColor;
		}
		Bounds b = bounds_;
		if (HasFocus())
			b = b.Expand(2.0f);
		if (!white)
			TryledDraw::Glow(dc, b, b.h * 0.5f, 14.0f, WithAlpha(ThemeAccent(dc), 0.6f));
		TryledDraw::FillRoundRect(dc, b, b.h * 0.5f, bg);
		if (HasFocus())
			TryledDraw::StrokeRoundRect(dc, b.Expand(4.0f), b.h * 0.5f + 4.0f, 2.0f, ThemeAccent(dc));
		dc.SetFontStyle(dc.GetTheme().uiFont);
		dc.DrawText(text_, b.centerX(), b.centerY(), fg, ALIGN_CENTER);
	}
private:
	std::string text_;
};

// The round "···" button.
class ConsoleMoreButton : public UI::Clickable {
public:
	ConsoleMoreButton() : UI::Clickable(new UI::LinearLayoutParams(44.0f, 44.0f, 0.0f, UI::Gravity::G_VCENTER)) {}
	void Draw(UIContext &dc) override {
		const uint32_t fg = ThemeFg(dc);
		TryledDraw::FillRoundRect(dc, bounds_, bounds_.h * 0.5f, WithAlpha(fg, down_ ? 0.3f : 0.16f));
		if (HasFocus())
			TryledDraw::StrokeRoundRect(dc, bounds_.Expand(4.0f), bounds_.h * 0.5f + 4.0f, 2.0f, ThemeAccent(dc));
		for (int i = -1; i <= 1; i++)
			TryledDraw::FillRoundRect(dc, Bounds::FromCenter(bounds_.centerX() + i * 7.0f, bounds_.centerY(), 2.2f), 2.2f, fg);
	}
};

// Right card: the game's artwork in a rounded box with its name, and small pills under it.
class ConsoleArtCard : public UI::InertView {
public:
	ConsoleArtCard(UI::LayoutParams *lp) : UI::InertView(lp) {}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 150.0f;
		h = 150.0f + 10.0f + 28.0f;
	}
	void Draw(UIContext &dc) override {
		if (s_details.empty)
			return;
		const float t = EaseOut(float(time_now_d() - std::max(s_consoleSelectTime, s_consoleOpenTime + 0.1)) / 0.4f);
		Bounds box(bounds_.x, bounds_.y + (1.0f - t) * 12.0f, 150.0f, 150.0f);
		TryledDraw::Glow(dc, box, kRadius, 16.0f, WithAlpha(0x000000, 0.7f * t));
		DrawGameArt(dc, s_details.path, s_details.console, box, kRadius, true, WithAlpha(0xFFFFFF, t));
		// Darken the top a bit so the name reads well.
		TryledDraw::FillRoundRect(dc, Bounds(box.x, box.y, box.w, 44.0f), kRadius, WithAlpha(0x000000, 0.35f * t));
		dc.SetFontStyle(dc.GetTheme().uiFontSmall);
		dc.DrawTextRectSqueeze(s_details.title, Bounds(box.x + 10.0f, box.y + 6.0f, box.w - 20.0f, 30.0f), WithAlpha(0xFFFFFF, t), ALIGN_LEFT | ALIGN_VCENTER);

		// Pills, right-aligned: play time and save states.
		std::vector<std::string> pills;
		if (s_details.psp) {
			pills.push_back(s_details.played ? FormatPlayTime(s_details.playedSeconds) : "Sin jugar");
			int used = 0;
			for (bool s : s_details.slots)
				used += s ? 1 : 0;
			pills.push_back(StringFromFormat("%d guardados", used));
		} else {
			pills.push_back(s_details.coreName.empty() ? s_details.console : s_details.coreName);
		}
		float x = bounds_.x2();
		const float y = box.y2() + 10.0f;
		for (int i = (int)pills.size() - 1; i >= 0; i--) {
			float tw, th;
			dc.MeasureText(dc.GetTheme().uiFontSmall, 0.8f, 0.8f, pills[i], &tw, &th);
			Bounds pb(x - tw - 18.0f, y, tw + 18.0f, 26.0f);
			TryledDraw::FillRoundRect(dc, pb, 9.0f, WithAlpha(ThemePanel(dc), 0.92f * t));
			TryledDraw::StrokeRoundRect(dc, pb, 9.0f, 1.0f, WithAlpha(ThemeFg(dc), 0.12f * t));
			dc.SetFontScale(0.8f, 0.8f);
			dc.DrawText(pills[i], pb.centerX(), pb.centerY(), WithAlpha(ThemeFg(dc), t), ALIGN_CENTER);
			dc.SetFontScale(1.0f, 1.0f);
			x = pb.x - 6.0f;
		}
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
};

// Bottom cards: play time, save slots, game settings.
class ConsoleInfoCard : public UI::InertView {
public:
	ConsoleInfoCard(int kind) : UI::InertView(new UI::LinearLayoutParams(UI::FILL_PARENT, 150.0f, 1.0f)), kind_(kind) {}
	void Draw(UIContext &dc) override {
		const uint32_t fg = ThemeFg(dc);
		const uint32_t accent = ThemeAccent(dc);
		TryledDraw::FillRoundRect(dc, bounds_, kRadius, WithAlpha(ThemePanel(dc), 0.9f));
		TryledDraw::StrokeRoundRect(dc, bounds_, kRadius, 1.0f, WithAlpha(fg, 0.1f));
		static const char *const titles[3] = { "Tiempo jugado", "Partidas guardadas", "Ajustes del juego" };
		const float x = bounds_.x + 14.0f;
		float y = bounds_.y + 12.0f;
		TryledDraw::FillRoundRect(dc, Bounds(x, y + 4.0f, 8.0f, 8.0f), 4.0f, accent);
		dc.SetFontStyle(dc.GetTheme().uiFontSmall);
		dc.DrawText(titles[kind_], x + 16.0f, y + 8.0f, fg, ALIGN_VCENTER);
		y += 26.0f;
		const float w = bounds_.w - 28.0f;
		std::string big, small;
		if (s_details.empty || !s_details.psp) {
			big = "-";
			small = s_details.empty ? "" : "Disponible para juegos de PSP";
		} else if (kind_ == 0) {
			big = s_details.played ? FormatPlayTime(s_details.playedSeconds) : "Sin jugar";
			small = s_details.lastPlayed;
			// Progress bar: share of a 20 hour "journey".
			const float frac = std::min(1.0f, s_details.playedSeconds / (20.0f * 3600.0f));
			Bounds bar(x, y + 40.0f, w, 6.0f);
			TryledDraw::FillRoundRect(dc, bar, 3.0f, WithAlpha(fg, 0.12f));
			if (frac > 0.0f)
				TryledDraw::FillRoundRect(dc, Bounds(bar.x, bar.y, std::max(6.0f, bar.w * frac), bar.h), 3.0f, accent);
		} else if (kind_ == 1) {
			int used = 0;
			const int n = (int)s_details.slots.size();
			const float gap = 6.0f;
			const float sw = n > 0 ? (w - gap * (n - 1)) / n : 0.0f;
			for (int i = 0; i < n; i++) {
				Bounds sb(x + i * (sw + gap), y + 6.0f, sw, 36.0f);
				if (s_details.slots[i]) {
					TryledDraw::FillRoundRect(dc, sb, 6.0f, WithAlpha(accent, 0.75f));
					used++;
				} else {
					TryledDraw::FillRoundRect(dc, sb, 6.0f, WithAlpha(fg, 0.1f));
				}
			}
			small = used ? StringFromFormat("%d de %d ranuras usadas", used, n) : "Sin partidas guardadas";
		} else {
			big = s_details.customConfig ? "Personalizados" : "Globales";
			small = s_details.customConfig ? "Ajustes propios de este juego" : "Usa los ajustes generales";
		}
		if (!big.empty()) {
			dc.SetFontStyle(dc.GetTheme().uiFontBig);
			dc.SetFontScale(0.85f, 0.85f);
			dc.DrawTextRectSqueeze(big, Bounds(x, y, w, 34.0f), fg, ALIGN_LEFT | ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		if (!small.empty()) {
			dc.SetFontStyle(dc.GetTheme().uiFontSmall);
			dc.SetFontScale(0.85f, 0.85f);
			dc.DrawTextRectSqueeze(small, Bounds(x, y + 54.0f, w, 22.0f), WithAlpha(fg, 0.6f), ALIGN_LEFT | ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
private:
	int kind_;
};

// Holds the whole home and slides it up to reveal the bottom cards (swipe, wheel or d-pad down).
class ConsoleShiftLayout : public UI::AnchorLayout {
public:
	ConsoleShiftLayout(UI::LayoutParams *lp) : UI::AnchorLayout(lp) {}
	void SetDown(bool down) { target_ = down ? 150.0f : 0.0f; }
	bool IsDown() const { return target_ > 0.0f; }
	void Draw(UIContext &dc) override {
		const double now = time_now_d();
		const float dt = last_ > 0.0 ? (float)std::min(now - last_, 0.1) : 0.0f;
		last_ = now;
		shift_ += (target_ - shift_) * std::min(1.0f, dt * 10.0f);
		dc.PushTransform({ Lin::Vec3(0.0f, -shift_, 0.0f), Lin::Vec3(1.0f, 1.0f, 1.0f), 1.0f });
		UI::AnchorLayout::Draw(dc);
		dc.PopTransform();
	}
	bool Touch(const TouchInput &input) override {
		TouchInput t = input;
		t.y += shift_;
		if (input.flags & TouchInputFlags::DOWN) {
			startY_ = input.y;
			tracking_ = true;
		}
		if ((input.flags & TouchInputFlags::UP) && tracking_) {
			tracking_ = false;
			const float dy = input.y - startY_;
			if (dy < -60.0f && !IsDown()) {
				SetDown(true);
				UI::PlayUISound(UI::UISound::SELECT);
			} else if (dy > 60.0f && IsDown()) {
				SetDown(false);
				UI::PlayUISound(UI::UISound::SELECT);
			}
		}
		return UI::AnchorLayout::Touch(t);
	}
private:
	float target_ = 0.0f;
	float shift_ = 0.0f;
	double last_ = 0.0;
	float startY_ = 0.0f;
	bool tracking_ = false;
};

}  // namespace


// ---------------------------------------------------------------------------
// Tryled: game folder scanning. The user picks one folder; we look through it
// (and subfolders, a few levels deep) on a worker thread and sort the games by
// console using the file extension. Only PSP games can be played for now.
// ---------------------------------------------------------------------------

static const char *const g_tryledConsoleOrder[] = { "PSP", "PS1", "GBA", "GB", "SNES", "NES", "Mega Drive", "Master System", "Game Gear", "PC Engine", "Atari 2600", "Atari 7800", "WonderSwan", "DOS" };

static std::string TryledConsoleOf(const Path &path) {
	std::string ext = path.GetFileExtension();  // Lowercase, with the dot.
	// DOS games: .dosz anywhere, and programs/archives inside a folder called "DOS".
	if (ext == ".dosz")
		return "DOS";
	if (ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".zip") {
		std::string lower = path.ToString();
		for (auto &c : lower)
			c = (char)tolower((unsigned char)c);
		if (lower.find("/dos/") != std::string::npos || lower.find("%2fdos%2f") != std::string::npos)
			return "DOS";
	}
	if (ext == ".iso" || ext == ".cso" || ext == ".chd" || ext == ".pbp" || ext == ".elf" || ext == ".prx")
		return "PSP";
	if (ext == ".cue" || ext == ".m3u" || ext == ".ecm" || ext == ".img" || ext == ".mdf" || ext == ".toc" || ext == ".cbn" || ext == ".exe")
		return "PS1";
	if (ext == ".gba")
		return "GBA";
	if (ext == ".gb" || ext == ".gbc")
		return "GB";
	if (ext == ".sfc" || ext == ".smc" || ext == ".swc" || ext == ".fig" || ext == ".bs")
		return "SNES";
	if (ext == ".nes" || ext == ".fds" || ext == ".unf" || ext == ".unif")
		return "NES";
	if (ext == ".md" || ext == ".gen" || ext == ".smd")
		return "Mega Drive";
	if (ext == ".sms")
		return "Master System";
	if (ext == ".gg")
		return "Game Gear";
	if (ext == ".pce" || ext == ".sgx")
		return "PC Engine";
	if (ext == ".a26")
		return "Atari 2600";
	if (ext == ".a78")
		return "Atari 7800";
	if (ext == ".ws" || ext == ".wsc" || ext == ".pc2")
		return "WonderSwan";
	return "";
}

struct TryledScanResult {
	std::string folder;
	std::vector<std::pair<Path, std::string>> games;  // path, console
};

static std::mutex g_tryledScanLock;
static TryledScanResult g_tryledScan;
static std::atomic<bool> g_tryledScanning{ false };
static std::atomic<int> g_tryledScanGen{ 0 };

static void TryledScanDir(const Path &dir, int depth, std::vector<std::pair<Path, std::string>> *out) {
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(dir, &files);
	for (const auto &f : files) {
		if (f.isDirectory) {
			// Skip hidden and system-ish folders.
			if (depth < 4 && !f.name.empty() && f.name[0] != '.' && f.name != "PSP" && f.name != "Android")
				TryledScanDir(f.fullName, depth + 1, out);
			continue;
		}
		std::string console = TryledConsoleOf(f.fullName);
		if (console.empty())
			continue;
		// For EBOOT.PBP inside a game folder, only take the PBP itself (not .elf/.prx helpers).
		std::string ext = f.fullName.GetFileExtension();
		if ((ext == ".elf" || ext == ".prx"))
			continue;
		out->push_back({ f.fullName, console });
	}
}

class TryledScanTask : public Task {
public:
	explicit TryledScanTask(const std::string &folder) : folder_(folder) {}
	TaskType Type() const override { return TaskType::IO_BLOCKING; }
	TaskPriority Priority() const override { return TaskPriority::NORMAL; }
	void Run() override {
		std::vector<std::pair<Path, std::string>> games;
		TryledScanDir(Path(folder_), 0, &games);
		std::sort(games.begin(), games.end(), [](const auto &a, const auto &b) {
			return a.first.GetFilename() < b.first.GetFilename();
		});
		{
			std::lock_guard<std::mutex> guard(g_tryledScanLock);
			g_tryledScan.folder = folder_;
			g_tryledScan.games = std::move(games);
		}
		g_tryledScanning = false;
		g_tryledScanGen++;
	}
private:
	std::string folder_;
};

static void TryledStartScan(const std::string &folder) {
	if (folder.empty() || g_tryledScanning.exchange(true))
		return;
	g_threadManager.EnqueueTask(new TryledScanTask(folder));
}

static int s_tryledTab = 0;


namespace {

// One console tab in the top bar ("Todos", "PSP", "GBA"...). The current one is underlined in the accent color.
class ConsoleTab : public UI::Choice {
public:
	ConsoleTab(std::string_view text, bool selected, UI::LayoutParams *layoutParams) : UI::Choice(text, layoutParams), selected_(selected) {}
	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override {
		float tw, th;
		dc.MeasureText(dc.GetTheme().uiFont, 0.8f, 0.8f, text_, &tw, &th);
		w = tw + 20.0f;
		h = 44.0f;
	}
	void Draw(UIContext &dc) override {
		const uint32_t accent = ThemeAccent(dc);
		uint32_t fg = dc.GetTheme().infoStyle.fgColor;
		if (!selected_)
			fg = colorAlpha(fg & 0xFFFFFF, 0.5f);
		if (HasFocus())
			dc.FillRect(UI::Drawable(colorAlpha(fg & 0xFFFFFF, 0.12f)), bounds_);
		dc.SetFontStyle(dc.GetTheme().uiFont);
		dc.SetFontScale(0.8f, 0.8f);
		dc.DrawText(text_, bounds_.centerX(), bounds_.centerY(), fg, ALIGN_CENTER);
		dc.SetFontScale(1.0f, 1.0f);
		if (selected_) {
			const Bounds bar(bounds_.x + 10.0f, bounds_.y2() - 8.0f, bounds_.w - 20.0f, 3.0f);
			TryledDraw::Glow(dc, bar, 1.5f, 8.0f, WithAlpha(accent, 0.7f));
			TryledDraw::FillRoundRect(dc, bar, 1.5f, accent);
		}
	}
private:
	bool selected_;
};

// Small "L1" / "R1" hint next to the tabs.
class ShoulderHint : public UI::TextView {
public:
	ShoulderHint(std::string_view text, UI::LayoutParams *lp) : UI::TextView(text, ALIGN_CENTER, true, lp) {}
	void Draw(UIContext &dc) override {
		const uint32_t fg = colorAlpha(dc.GetTheme().infoStyle.fgColor & 0xFFFFFF, 0.55f);
		dc.FillRect(UI::Drawable(colorAlpha(dc.GetTheme().infoStyle.fgColor & 0xFFFFFF, 0.14f)), Bounds(bounds_.x + 2, bounds_.y + 12, bounds_.w - 4, bounds_.h - 24));
		dc.SetFontStyle(dc.GetTheme().uiFontTiny);
		dc.DrawText(GetText(), bounds_.centerX(), bounds_.centerY(), fg, ALIGN_CENTER);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
};

}  // namespace

bool MainScreen::ConsoleMode() const {
	return !s_tryledLibraryMode;
}

void MainScreen::CreateConsoleViews() {
	using namespace UI;

	// Games: recently played first, then everything found in the chosen folder.
	if (!g_Config.sTryledGamesFolder.empty()) {
		bool needScan;
		{
			std::lock_guard<std::mutex> guard(g_tryledScanLock);
			needScan = g_tryledScan.folder != g_Config.sTryledGamesFolder;
		}
		if (needScan)
			TryledStartScan(g_Config.sTryledGamesFolder);
	}
	consoleScanGen_ = g_tryledScanGen;

	std::vector<std::pair<Path, std::string>> all;
	for (const std::string &file : g_recentFiles.GetRecentFiles()) {
		all.push_back({ Path(file), "PSP" });
	}
	{
		std::lock_guard<std::mutex> guard(g_tryledScanLock);
		for (const auto &g : g_tryledScan.games) {
			bool dup = false;
			for (const auto &a : all) {
				if (a.first == g.first) {
					dup = true;
					break;
				}
			}
			if (!dup)
				all.push_back(g);
		}
	}

	consoleTabs_.clear();
	consoleTabs_.push_back("Todos");
	for (const char *c : g_tryledConsoleOrder) {
		for (const auto &g : all) {
			if (g.second == c) {
				consoleTabs_.push_back(c);
				break;
			}
		}
	}
	if (s_tryledTab >= (int)consoleTabs_.size())
		s_tryledTab = 0;

	consoleGames_.clear();
	consoleKinds_.clear();
	for (const auto &g : all) {
		if (s_tryledTab == 0 || g.second == consoleTabs_[s_tryledTab]) {
			consoleGames_.push_back(g.first);
			consoleKinds_.push_back(g.second);
		}
	}
	if (s_tryledSelected >= (int)consoleGames_.size())
		s_tryledSelected = 0;
	s_consoleOpenTime = time_now_d();

	// Everything sits in a layout that slides up to show the bottom cards.
	ConsoleShiftLayout *home = new ConsoleShiftLayout(new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
	root_ = home;
	consoleShift_ = home;
	const Bounds screen = GetLayoutBounds(*screenManager()->getUIContext());

	// Top bar: console tabs (L1/R1) on the left; search, folder, settings, profile and clock on the right.
	LinearLayout *topBar = home->Add(new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, 52.0f, 18.0f, 6.0f, 22.0f, NONE)));
	topBar->SetSpacing(6.0f);
	ScrollView *tabScroll = topBar->Add(new ScrollView(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f, Gravity::G_VCENTER)));
	LinearLayout *tabRow = tabScroll->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	tabRow->SetSpacing(2.0f);
	if (consoleTabs_.size() > 1)
		tabRow->Add(new ShoulderHint("L1", new LinearLayoutParams(32.0f, 44.0f, 0.0f, Gravity::G_VCENTER)));
	for (int t = 0; t < (int)consoleTabs_.size(); t++) {
		ConsoleTab *tabView = tabRow->Add(new ConsoleTab(consoleTabs_[t], t == s_tryledTab, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
		tabView->OnClick.Add([this, t](EventParams &) { ConsoleSetTab(t); });
	}
	if (consoleTabs_.size() > 1)
		tabRow->Add(new ShoulderHint("R1", new LinearLayoutParams(32.0f, 44.0f, 0.0f, Gravity::G_VCENTER)));
	tabScroll->ScrollTo(std::max(0.0f, (float)s_tryledTab - 2.0f) * 110.0f);
	if (g_tryledScanning)
		topBar->Add(new TextView("Buscando juegos…", ALIGN_RIGHT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
	topBar->Add(new ConsoleIconButton(ImageID("I_SEARCH"), 38.0f, false))->OnClick.Add([this](EventParams &) {
		s_tryledLibraryMode = true;
		RecreateViews();
	});
	if (System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER))
		topBar->Add(new ConsoleIconButton(ImageID("I_FOLDER_OPEN"), 38.0f, false))->OnClick.Add([this](EventParams &) { ConsoleChooseFolder(); });
	topBar->Add(new ConsoleIconButton(ImageID("I_GEAR"), 38.0f, false))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	topBar->Add(new ConsoleAvatar());
	consoleClock_ = topBar->Add(new TextView(ClockString(), ALIGN_VCENTER, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER, Margins(6, 0, 0, 0))));

	// The row of games.
	ScrollView *rowScroll = home->Add(new ScrollView(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, 104.0f, 0.0f, 54.0f, 0.0f, NONE)));
	LinearLayout *row = rowScroll->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT)));
	row->SetSpacing(12.0f);
	row->Add(new Spacer(16.0f));
	View *firstTile = nullptr;
	const bool showBadges = s_tryledTab == 0;
	for (int i = 0; i < (int)consoleGames_.size(); i++) {
		ConsoleGameTile *tile = row->Add(new ConsoleGameTile(consoleGames_[i], consoleKinds_[i], showBadges, i,
			[](int index) { return index == s_tryledSelected; },
			[this](int index) { ConsoleSelect(index); }));
		tile->OnClick.Add([this, i](EventParams &) {
			if (i == s_tryledSelected) {
				ConsoleLaunchSelected();
			} else {
				ConsoleSelect(i);
			}
		});
		if (i == s_tryledSelected)
			firstTile = tile;
	}
	row->Add(new ConsoleIconButton(ImageID("I_GRID"), kTile, true, 0.6f))->OnClick.Add([this](EventParams &) {
		s_tryledLibraryMode = true;
		RecreateViews();
	});
	row->Add(new Spacer(16.0f));
	if (s_tryledSelected > 4)
		rowScroll->ScrollTo((s_tryledSelected - 4) * (kTile + 12.0f));

	// Selected game, bottom left: big title, console badge + line, Play and "···".
	LinearLayout *hero = home->Add(new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(screen.w * 0.58f, WRAP_CONTENT, 30.0f, NONE, NONE, 92.0f)));
	hero->SetSpacing(2.0f);
	consoleSmallName_ = hero->Add(new TextView("", new LinearLayoutParams(0.0f, 0.0f)));
	consoleSmallName_->SetVisibility(V_GONE);
	consoleTitle_ = hero->Add(new ConsoleAnimText(1.75f, false, 0.05f, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	consoleSub_ = hero->Add(new ConsoleSubText(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	LinearLayout *buttons = hero->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Margins(0, 14, 0, 0))));
	buttons->SetSpacing(12.0f);
	View *playButton;
	if (consoleGames_.empty()) {
		ConsolePlayButton *pick = buttons->Add(new ConsolePlayButton("Elegir carpeta de juegos"));
		pick->OnClick.Add([this](EventParams &) { ConsoleChooseFolder(); });
		playButton = pick;
	} else {
		ConsolePlayButton *play = buttons->Add(new ConsolePlayButton("Jugar"));
		play->OnClick.Add([this](EventParams &) { ConsoleLaunchSelected(); });
		playButton = play;
		ConsoleMoreButton *more = buttons->Add(new ConsoleMoreButton());
		more->OnClick.Add([this](EventParams &) {
			if (s_tryledSelected < (int)consoleGames_.size() && consoleKinds_[s_tryledSelected] == "PSP") {
				screenManager()->push(new GameScreen(consoleGames_[s_tryledSelected], false));
			}
		});
	}

	// Right: artwork card with pills.
	home->Add(new ConsoleArtCard(new AnchorLayoutParams(150.0f, 188.0f, NONE, NONE, 30.0f, 92.0f)));

	// Bottom cards, peeking from the bottom edge; the home slides up to show them.
	LinearLayout *cards = home->Add(new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, 150.0f, 24.0f, screen.h - 70.0f, 24.0f, NONE)));
	cards->SetSpacing(12.0f);
	for (int i = 0; i < 3; i++)
		cards->Add(new ConsoleInfoCard(i));

	root_->SetDefaultFocusView(firstTile ? firstTile : playButton);
	root_->SetTag("mainroot");

	ConsoleRefreshInfo();
}

void MainScreen::ConsoleSelect(int index) {
	if (index < 0 || index >= (int)consoleGames_.size())
		return;
	if (index != s_tryledSelected) {
		s_consolePrevArt = consoleGames_[s_tryledSelected];
		s_consoleArtTime = time_now_d();
		s_consoleSelectTime = s_consoleArtTime;
		s_tryledSelected = index;
		if (consoleKinds_[index] == "PSP")
			g_BackgroundAudio.SetGame(consoleGames_[index]);
	}
	ConsoleRefreshInfo();
}

void MainScreen::ConsoleLaunchSelected() {
	if (s_tryledSelected >= (int)consoleGames_.size())
		return;
	Path path = consoleGames_[s_tryledSelected];
	const std::string &console = consoleKinds_[s_tryledSelected];
	if (console != "PSP") {
		const RetroCoreInfo *core = RetroCoreForConsole(console);
		if (!core) {
			g_OSD.Show(OSDType::MESSAGE_INFO, "El emulador de " + console + " aún no está en Tryled.", 4.0f, "tryled-noemu");
			return;
		}
		screenManager()->push(new RetroScreen(path, *core));
		return;
	}
	// After playing, this game moves to the front of the recent list.
	s_tryledSelected = 0;
	s_tryledTab = 0;
	LaunchFile(screenManager(), this, path);
}

void MainScreen::ConsoleSetTab(int tab) {
	if (consoleTabs_.empty())
		return;
	tab = (tab + (int)consoleTabs_.size()) % (int)consoleTabs_.size();
	if (tab == s_tryledTab)
		return;
	s_tryledTab = tab;
	s_tryledSelected = 0;
	s_consoleSelectTime = time_now_d();
	UI::PlayUISound(UI::UISound::TOGGLE_ON);
	RecreateViews();
}

void MainScreen::ConsoleChooseFolder() {
	System_BrowseForFolder(GetRequesterToken(), "Carpeta de juegos", Path(g_Config.sTryledGamesFolder), [](const std::string &value, int) {
		g_Config.sTryledGamesFolder = value;
		g_Config.Save("TryledGamesFolder");
		TryledStartScan(value);
		System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
	});
}

void MainScreen::ConsoleRefreshInfo() {
	consoleLastRefresh_ = time_now_d();
	if (consoleClock_)
		consoleClock_->SetText(ClockString());
	if (!consoleTitle_)
		return;
	ConsoleSubText *sub = static_cast<ConsoleSubText *>(consoleSub_);

	s_details = ConsoleGameDetails();
	if (consoleGames_.empty()) {
		sub->SetBadge("");
		if (g_tryledScanning) {
			consoleTitle_->SetText("Buscando juegos…");
			consoleSub_->SetText("Tus juegos aparecerán aquí, ordenados por consola.");
		} else {
			consoleTitle_->SetText("Tu colección está vacía");
			consoleSub_->SetText(g_Config.sTryledGamesFolder.empty() ? "Elige la carpeta donde guardas tus juegos." : "No encontré juegos en esa carpeta.");
		}
		return;
	}

	const Path &path = consoleGames_[s_tryledSelected];
	const std::string &console = consoleKinds_[s_tryledSelected];
	s_details.empty = false;
	s_details.path = path;
	s_details.console = console;
	s_details.psp = console == "PSP";
	sub->SetBadge(ConsoleShort(console));

	if (!s_details.psp) {
		const RetroCoreInfo *core = RetroCoreForConsole(console);
		s_details.title = path.WithReplacedExtension("").GetFilename();
		s_details.coreName = core ? core->name : "";
		consoleTitle_->SetText(s_details.title);
		consoleSub_->SetText(core ? std::string("Se juega con ") + core->name : "Emulador no disponible todavía");
		return;
	}

	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, path, kConsoleInfoFlags);
	const bool ready = ginfo->Ready(GameInfoFlags::PARAM_SFO);
	s_details.title = ready ? ginfo->GetTitle() : path.GetFilename();
	consoleTitle_->SetText(s_details.title);

	int seconds = 0;
	s_details.played = !ginfo->id.empty() && g_Config.TimeTracker().GetPlayedSeconds(ginfo->id, &seconds);
	s_details.playedSeconds = seconds;
	uint64_t last = 0;
	if (!ginfo->id.empty() && g_Config.TimeTracker().GetLastPlayed(ginfo->id, &last) && last > 0) {
		const int64_t ago = (int64_t)time(nullptr) - (int64_t)last;
		const int64_t days = ago / 86400;
		if (ago < 3600)
			s_details.lastPlayed = "Jugado hace un momento";
		else if (days == 0)
			s_details.lastPlayed = "Jugado hoy";
		else if (days == 1)
			s_details.lastPlayed = "Jugado ayer";
		else
			s_details.lastPlayed = StringFromFormat("Jugado hace %d días", (int)days);
	}
	consoleSub_->SetText(s_details.played ? (s_details.lastPlayed.empty() ? std::string("Continúa donde lo dejaste") : s_details.lastPlayed) : "Todavía no lo has jugado");

	if (ready) {
		const std::string prefix = SaveState::GetGamePrefix(ginfo->GetParamSFO());
		for (int slot = 0; slot < g_Config.iSaveStateSlotCount; slot++)
			s_details.slots.push_back(SaveState::HasSaveInSlot(prefix, slot));
	}
	s_details.customConfig = ginfo->hasConfig;
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	// Tryled: the console-style home is the default. The classic browser is the "Biblioteca".
	consoleTitle_ = nullptr;
	consoleClock_ = nullptr;
	consoleShift_ = nullptr;
	if (ConsoleMode()) {
		CreateConsoleViews();
		return;
	}

	const bool vertical = GetDeviceOrientation() == DeviceOrientation::Portrait;

	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 64, TabHolderFlags::Default, nullptr, nullptr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	ViewGroup *leftColumn = tabHolder_;
	tabHolder_->SetTag("MainScreenGames");
	gameBrowsers_.clear();

	tabHolder_->SetClip(true);

	bool showRecent = g_Config.iMaxRecent > 0;
	bool hasStorageAccess = !System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS) ||
		System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE) == PERMISSION_STATUS_GRANTED;
	bool storageIsTemporary = IsTempPath(GetSysDirectory(DIRECTORY_SAVEDATA)) && !confirmedTemporary_;
	if (showRecent && !hasStorageAccess) {
		showRecent = g_recentFiles.HasAny();
	}

	if (showRecent) {
		CreateRecentTab();
	}

	Button *focusButton = nullptr;
	if (hasStorageAccess) {
		CreateBrowserTab(Path(g_Config.currentDirectory), "Games", "How to get games", getGamesUri, BrowseFlags::STANDARD, &g_Config.bGridView2, &g_Config.fGameListScrollPosition);
		CreateBrowserTab(GetSysDirectory(DIRECTORY_GAME), "Homebrew & Demos", "How to get homebrew & demos", getHomebrewUri, BrowseFlags::HOMEBREW_STORE, &g_Config.bGridView3, &g_Config.fHomebrewScrollPosition);

		if (g_Config.bRemoteTab && !g_Config.sLastRemoteISOServer.empty()) {
			Path remotePath(FormatRemoteISOUrl(g_Config.sLastRemoteISOServer.c_str(), g_Config.iLastRemoteISOPort, RemoteSubdir().c_str()));
			GameBrowser *remoteBrowser = CreateBrowserTab(remotePath, "Remote disc streaming", "Remote disc streaming", remoteGamesUri, BrowseFlags::NAVIGATE, &g_Config.bGridView4, &g_Config.fRemoteScrollPosition);
			remoteBrowser->SetHomePath(remotePath);
		}

		if (g_recentFiles.HasAny()) {
			tabHolder_->SetCurrentTab(std::clamp(g_Config.iDefaultTab, 0, g_Config.bRemoteTab ? 3 : 2), true);
		} else if (g_Config.iMaxRecent > 0) {
			tabHolder_->SetCurrentTab(1, true);	
		}

		if (backFromStore_ || showHomebrewTab) {
			tabHolder_->SetCurrentTab(2, true);
			backFromStore_ = false;
			showHomebrewTab = false;
		}

		if (storageIsTemporary) {
			LinearLayout *buttonHolder = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
			buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));
			focusButton = new Button(mm->T("SavesAreTemporaryIgnore", "Ignore warning"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
			focusButton->SetPadding(32, 16);
			buttonHolder->Add(focusButton)->OnClick.Add([this](UI::EventParams &e) {
				confirmedTemporary_ = true;
				RecreateViews();
			});
			buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));

			leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
			leftColumn->Add(new TextView(mm->T("SavesAreTemporary", "PPSSPP saving in temporary storage"), ALIGN_HCENTER, false));
			leftColumn->Add(new TextView(mm->T("SavesAreTemporaryGuidance", "Extract PPSSPP somewhere to save permanently"), ALIGN_HCENTER, false));
			leftColumn->Add(new Spacer(10.0f));
			leftColumn->Add(buttonHolder);
			leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
		}
	} else {
		if (!showRecent) {
			leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
			// Just so it's destroyed on recreate.
			leftColumn->Add(tabHolder_);
			tabHolder_->SetVisibility(V_GONE);
		}

		LinearLayout *buttonHolder = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));
		focusButton = new Button(mm->T("Give PPSSPP permission to access storage"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		focusButton->SetPadding(32, 16);
		buttonHolder->Add(focusButton)->OnClick.Handle(this, &MainScreen::OnAllowStorage);
		buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));

		leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
		leftColumn->Add(buttonHolder);
		leftColumn->Add(new Spacer(10.0f));
		leftColumn->Add(new TextView(mm->T("PPSSPP can't load games or save right now"), ALIGN_HCENTER, false));
		leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
	}

	if (vertical) {
		LinearLayout *header = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 8, 8, 16)));
		header->SetSpacing(5.0f);
		header->Add(new LogoView(true, new LinearLayoutParams(1.0f)));

		LinearLayout *buttonGroup = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f, UI::Gravity::G_VCENTER));

		CreateMainButtons(buttonGroup, vertical);
		header->Add(buttonGroup);

		LinearLayout *rootLayout = new LinearLayout(ORIENT_VERTICAL);
		rootLayout->SetSpacing(0.0f);

		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(1.0f));
		rootLayout->Add(header);
		rootLayout->Add(leftColumn);
		root_ = rootLayout;

		// no space for a fullscreen button!
	} else {
		const Margins actionMenuMargins(0, 10, 10, 0);
		ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(320, FILL_PARENT, actionMenuMargins));
		LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		rightColumnItems->SetSpacing(0.0f);
		ViewGroup *logo = new LogoView(false, new LinearLayoutParams(FILL_PARENT, 80.0f));

		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
			auto gr = GetI18NCategory(I18NCat::GRAPHICS);
			Button *fullscreenButton = logo->Add(new Button("", ImageID(), new AnchorLayoutParams(48, 48, NONE, 0, 0, NONE, Centering::None)));
			fullscreenButton->SetIgnoreText(true);
			fullscreenButton->OnClick.Add([](UI::EventParams &e) {
				g_Config.bFullScreen = !g_Config.bFullScreen;
				System_ApplyFullscreenState();
			});
			fullscreenButton->SetImageIDFunc([]() {
				return g_Config.bFullScreen ? ImageID("I_RESTORE") : ImageID("I_FULLSCREEN");
			});
		}
		rightColumnItems->Add(logo);

		LinearLayout *rightColumnChoices = rightColumnItems;
		CreateMainButtons(rightColumnChoices, vertical);

		rightColumn->Add(rightColumnItems);

		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	if (focusButton) {
		root_->SetDefaultFocusView(focusButton);
	} else if (tabHolder_->GetVisibility() != V_GONE) {
		root_->SetDefaultFocusView(tabHolder_);
	}

	root_->SetTag("mainroot");

	if (!g_Config.sUpgradeMessage.empty()) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		Margins margins(0, 0);
		if (vertical) {
			margins.bottom = ITEM_HEIGHT;
		}
		UI::LinearLayout *upgradeBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, margins));

		UI::Margins textMargins(10, 5);
		UI::Margins buttonMargins(5, 0);
		UI::Drawable solid(0xFFbd9939);
		upgradeBar->SetSpacing(5.0f);
		upgradeBar->SetBG(solid);
		std::string upgradeMessage(di->T("New version of PPSSPP available"));
		if (!vertical) {
			// The version only really fits in the horizontal layout.
			upgradeMessage += ": " + g_Config.sUpgradeVersion;
		}
		upgradeBar->Add(new TextView(upgradeMessage, new LinearLayoutParams(1.0f, UI::Gravity::G_VCENTER, textMargins)));
		upgradeBar->Add(new Choice(di->T("Download"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDownloadUpgrade);
		Choice *dismiss = upgradeBar->Add(new Choice("", ImageID("I_CROSS"), new LinearLayoutParams(buttonMargins)));
		dismiss->OnClick.Add([this](UI::EventParams &e) {
			g_Config.DismissUpgrade();
			g_Config.Save("dismissupgrade");
			RecreateViews();
		});

		// Slip in under root_
		LinearLayout *newRoot = new LinearLayout(ORIENT_VERTICAL);
		newRoot->Add(root_);
		newRoot->Add(upgradeBar);
		root_->ReplaceLayoutParams(new LinearLayoutParams(1.0));
		root_ = newRoot;
	}
}

bool MainScreen::key(const KeyInput &key) {
	if (consoleShift_ && (key.flags & KeyInputFlags::DOWN)) {
		// Slide the home up/down to show the bottom cards: d-pad down from the Play row, d-pad up back, or the mouse wheel.
		ConsoleShiftLayout *shift = static_cast<ConsoleShiftLayout *>(consoleShift_);
		UI::View *focused = UI::GetFocusedView();
		const bool onButtons = focused && (dynamic_cast<ConsolePlayButton *>(focused) || dynamic_cast<ConsoleMoreButton *>(focused));
		if (((key.keyCode == NKCODE_DPAD_DOWN && onButtons) || key.keyCode == NKCODE_EXT_MOUSEWHEEL_DOWN) && !shift->IsDown()) {
			shift->SetDown(true);
			UI::PlayUISound(UI::UISound::SELECT);
			return true;
		}
		if ((key.keyCode == NKCODE_DPAD_UP || key.keyCode == NKCODE_EXT_MOUSEWHEEL_UP || UI::IsEscapeKey(key)) && shift->IsDown()) {
			shift->SetDown(false);
			UI::PlayUISound(UI::UISound::SELECT);
			return true;
		}
	}
	if (consoleTitle_ && (key.flags & KeyInputFlags::DOWN)) {
		if (key.keyCode == NKCODE_BUTTON_L1 || key.keyCode == NKCODE_BUTTON_L2) {
			ConsoleSetTab(s_tryledTab - 1);
			return true;
		}
		if (key.keyCode == NKCODE_BUTTON_R1 || key.keyCode == NKCODE_BUTTON_R2) {
			ConsoleSetTab(s_tryledTab + 1);
			return true;
		}
	}
	if (key.flags & KeyInputFlags::DOWN) {
		if (key.keyCode == NKCODE_F && (key.flags & KeyInputFlags::MOD_CTRL) && System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
			auto se = GetI18NCategory(I18NCat::SEARCH);
			System_InputBoxGetString(GetRequesterToken(), se->T("Search term"), searchFilter_, false, [&](const std::string &value, int) {
				searchFilter_ = StripSpaces(value);
				searchChanged_ = true;
			});
		}
	}

	bool retval = UIBaseScreen::key(key);
	if (retval) {
		return true;
	}

	// This is not a DialogScreen so we have to implement behavior here too.
	// However we add a small safety hatch by checking for gamepad, and for now we only allow this behavior
	// on Android. Might reconsider for other platforms.
	#if PPSSPP_PLATFORM(ANDROID)
	if (key.flags & KeyInputFlags::DOWN) {
		if ((key.deviceId == DEVICE_ID_PAD_0 || key.deviceId == DEVICE_ID_XINPUT_0) && UI::IsEscapeKey(key)) {
			System_ExitApp();
		}
	}
	#endif
	return true;
}

void MainScreen::OnAllowStorage(UI::EventParams &e) {
	System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
}

// See Config::SupportsUpgradeCheck() if you add more platforms.
void MainScreen::OnDownloadUpgrade(UI::EventParams &e) {
#if PPSSPP_PLATFORM(ANDROID)
	// Go to app store
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "market://details?id=org.ppsspp.ppssppgold");
	} else {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "market://details?id=org.ppsspp.ppsspp");
	}
#elif PPSSPP_PLATFORM(WINDOWS)
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/download");
#elif PPSSPP_PLATFORM(IOS_APP_STORE)
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "itms-apps://itunes.apple.com/app/id6496972903");
#else
	// Go directly to ppsspp.org and let the user sort it out
	// (for details and in case downloads doesn't have their platform.)
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/");
#endif
}

void MainScreen::sendMessage(UIMessage message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIBaseScreen::sendMessage(message, value);

	if (message == UIMessage::REQUEST_GAME_BOOT) {
		LaunchFile(screenManager(), this, Path(value));
	} else if (message == UIMessage::PERMISSION_GRANTED && !strcmp(value, "storage")) {
		RecreateViews();
	} else if (message == UIMessage::RECENT_FILES_CHANGED) {
		RecreateViews();
	}
}

void MainScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);

	// Game info loads asynchronously, and the clock ticks: refresh the console home now and then.
	if (consoleTitle_ && !s_startupSoundPlayed && time_now_d() - s_consoleOpenTime > 0.3) {
		s_startupSoundPlayed = true;
		if (g_Config.bUISound) {
			g_BackgroundAudio.SFX().Play(UI::UISound::STARTUP, Volume100ToMultiplier(g_Config.iUIVolume));
		}
	}
	if (consoleTitle_ && consoleScanGen_ != g_tryledScanGen) {
		RecreateViews();
	}
	if (consoleTitle_ && time_now_d() - consoleLastRefresh_ > 0.5) {
		ConsoleRefreshInfo();
	}

	if (searchChanged_) {
		for (auto browser : gameBrowsers_) {
			if (browser->GetVisibility() == UI::V_VISIBLE) {
				browser->SetSearchFilter(searchFilter_, false);
			}
		}
		searchChanged_ = false;
	}
}

void MainScreen::OnLoadFile(UI::EventParams &e) {
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		System_BrowseForFile(GetRequesterToken(), mm->T("Load"), BrowseFileType::BOOTABLE, [](const std::string &value, int) {
			System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, value);
		});
	}
}

void MainScreen::DrawBackground(UIContext &dc) {
	if (consoleTitle_) {
		const double now = time_now_d();
		if (!consoleGames_.empty() && s_tryledSelected < (int)consoleGames_.size() && consoleKinds_[s_tryledSelected] == "PSP") {
			const float fade = EaseOut(float(now - s_consoleArtTime) / 0.5f);
			// The previous game's art fades out underneath the new one.
			if (fade < 1.0f && !s_consolePrevArt.empty() && TryledConsoleOf(s_consolePrevArt) == "PSP") {
				std::shared_ptr<GameInfo> prev = g_gameInfoCache->GetInfo(dc.GetDrawContext(), s_consolePrevArt, kConsoleInfoFlags);
				if (prev->Ready(GameInfoFlags::PIC1) && prev->pic1.texture)
					DrawConsoleArt(dc, prev->pic1.texture, 1.0f - fade, now);
			}
			std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), consoleGames_[s_tryledSelected], kConsoleInfoFlags);
			if (ginfo->Ready(GameInfoFlags::PIC1) && ginfo->pic1.texture) {
				const float loaded = std::clamp(float(now - ginfo->pic1.timeLoaded) * 3.0f, 0.0f, 1.0f);
				DrawConsoleArt(dc, ginfo->pic1.texture, std::min(fade, loaded), now);
			}
		}
		const Bounds &b = dc.GetBounds();
		const uint32_t bg = dc.GetTheme().backgroundColor & 0xFFFFFF;
		dc.Flush();
		dc.RebindTexture();
		const int strips = 48;
		const float fadeW = b.w * 0.62f;
		for (int i = 0; i < strips; i++) {
			const float t = (float)i / (strips - 1);
			const float a = t < 0.45f ? 0.92f : 0.92f * (1.0f - (t - 0.45f) / 0.55f);
			dc.FillRect(UI::Drawable(colorAlpha(bg, a)), Bounds(b.x + fadeW * i / strips, b.y, fadeW / strips + 1.0f, b.h));
		}
		const float fadeH = b.h * 0.3f;
		for (int i = 0; i < strips / 2; i++) {
			const float t = (float)i / (strips / 2 - 1);
			dc.FillRect(UI::Drawable(colorAlpha(bg, 0.95f * t)), Bounds(b.x, b.y2() - fadeH + fadeH * i / (strips / 2), b.w, fadeH / (strips / 2) + 1.0f));
		}
		// Soft glow from the top edge, in the theme accent color (Tryled's own touch, not a wave).
		TryledDraw::RadialEllipse(dc, b.centerX(), b.y - b.h * 0.12f, b.w * 0.7f, b.h * 0.42f, WithAlpha(ThemeAccent(dc), 0.26f), WithAlpha(ThemeAccent(dc), 0.0f));
		DrawConsoleAmbient(dc, now);
		dc.Flush();
		return;
	}
	if (highlightedBackgrounds_.empty()) {
		return;
	}

	constexpr float fadeTime = 0.25f;

	double now = time_now_d();

	for (auto iter = highlightedBackgrounds_.begin(); iter != highlightedBackgrounds_.end(); ) {
		std::shared_ptr<GameInfo> ginfo;
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), iter->gamePath, GameInfoFlags::PIC1);
		float timeSinceStart = float(now - std::max(iter->startTime, ginfo->pic1.timeLoaded));
		float alpha = std::clamp(timeSinceStart / fadeTime, 0.0f, 1.0f);
		if (iter->endTime > 0.0) {
			// TODO: Consider only fading out if it's the last one in the list, to avoid background shine-through.
			float fadeOutAlpha = std::max(0.0f, float(now - iter->endTime) / fadeTime);
			if (fadeOutAlpha > 1.0f) {
				iter = highlightedBackgrounds_.erase(iter);
				continue;
			}
			alpha *= 1.0f - fadeOutAlpha;
		}
		iter++;

		if (!ginfo->pic1.texture) {
			continue;
		}

		DrawBackgroundTexture(dc, ginfo->pic1.texture, Lin::Vec3(0.0f, 0.0f, 0.0f), alpha);
	}
}

void MainScreen::DrawBackgroundFor(UIContext &dc, const Path &gamePath, float alpha) {
	::DrawGameBackground(dc, gamePath, Lin::Vec3(0.f, 0.f, 0.f), alpha);
}

void MainScreen::OnGameSelected(UI::EventParams &e) {
	Path path(e.s);
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::FILE_TYPE);
	if (ginfo->fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
		return;
	}
	if (g_GameManager.GetState() == GameManagerState::INSTALLING)
		return;

	// Restore focus if it was highlighted (e.g. by gamepad.)
	restoreFocusGamePath_ = highlightedGamePath_;
	g_BackgroundAudio.SetGame(path);
	lockBackgroundAudio_ = true;
	screenManager()->push(new GameScreen(path, false));
}

void MainScreen::InstantHighlight(const Path &path) {
	// Clear the previous highlight immediately, so we don't have multiple at once.
	highlightedBackgrounds_.clear();
	highlightedBackgrounds_.push_back({path, 0.0f, -1.0});
}

void MainScreen::OnGameHighlight(UI::EventParams &e) {
	using namespace UI;

	Path path(e.s);

	const FocusFlags focusFlags = (FocusFlags)e.a;

	if (path == highlightedGamePath_ && (focusFlags & FocusFlags::GOT_FOCUS)) {
		// Already highlighted, nothing to do.
		return;
	}

	if (focusFlags & FocusFlags::LOST_FOCUS) {
		// Lost focus, so we want to fade out the background.

		// Trigger fadeouts on any active highlights.
		for (auto &iter : highlightedBackgrounds_) {
			if (iter.endTime < 0.0) {
				iter.endTime = time_now_d();
			}
		}
		highlightedGamePath_.clear();
		if ((focusFlags & FocusFlags::CAUSE_FOCUS_MOVE) || (focusFlags & FocusFlags::CAUSE_KB_FOCUS_DISABLED)) {
			// Focus moved to another game, so we want to fade out so we can fade in the new one.
			g_BackgroundAudio.SetGame(Path());
		}
		return;
	}

	highlightedGamePath_ = path;

	_dbg_assert_(!path.empty());

	if (path.empty()) {
		// Nothing highlighed? Exit.
		return;
	}

	// Add a new entry to the highlight list.
	highlightedBackgrounds_.push_back({path, time_now_d(), -1.0});
	if ((!highlightedGamePath_.empty() || (focusFlags & FocusFlags::LOST_FOCUS)) && !lockBackgroundAudio_) {
		g_BackgroundAudio.SetGame(highlightedGamePath_);
	}

	lockBackgroundAudio_ = false;
}

void MainScreen::OnGameSelectedInstant(UI::EventParams &e) {
	ScreenManager *screen = screenManager();
	LaunchFile(screen, nullptr, Path(e.s));
}

void MainScreen::OnGameSettings(UI::EventParams &e) {
	// Not passing a game ID, changing the global settings.
	screenManager()->push(new GameSettingsScreen(Path()));
}

void MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
}

void LaunchBuyGold(ScreenManager *screenManager) {
	if (System_GetPropertyBool(SYSPROP_USE_IAP)) {
		screenManager->push(new IAPScreen(true));
	} else if (System_GetPropertyBool(SYSPROP_USE_APP_STORE)) {
#if PPSSPP_PLATFORM(ANDROID)
		LaunchPlayStoreOrWebsiteGold();
#else
		screenManager->push(new IAPScreen(false));
#endif
	} else {
#if PPSSPP_PLATFORM(IOS_APP_STORE)
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold_ios");
#else
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold");
#endif
	}
}

void MainScreen::OnPPSSPPOrg(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org");
}

void MainScreen::OnForums(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://forums.ppsspp.org");
}

void MainScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	std::string tag = dialog->tag();
	if (tag == "Store") {
		backFromStore_ = true;
		RecreateViews();
	} else if (tag == "Game") {
		if (!restoreFocusGamePath_.empty() && UI::IsFocusMovementEnabled()) {
			// Prevent the background from fading, since we just were displaying it.
			InstantHighlight(restoreFocusGamePath_);

			// Refocus the game button itself.
			int tab = tabHolder_->GetCurrentTab();
			if (tab >= 0 && tab < (int)gameBrowsers_.size()) {
				gameBrowsers_[tab]->FocusGame(restoreFocusGamePath_);
			}

			// Don't get confused next time.
			restoreFocusGamePath_.clear();
		} else {
			// Not refocusing, so we need to stop the audio.
			g_BackgroundAudio.SetGame(Path());
		}
	} else if (tag == "InstallZip") {
		INFO_LOG(Log::System, "InstallZip finished, refreshing");
		if (gameBrowsers_.size() >= 2) {
			gameBrowsers_[1]->RequestRefresh();
		}
	} else if (tag == "IAP") {
		// Gold status may have changed.
		RecreateViews();
	} else if (tag == "Upload") {
		// Files may have been uploaded.
		RecreateViews();
	} else if (tag == "SavedataPopup") {
		// We must have come from the file browser tab.
		if (gameBrowsers_.size() >= 2) {
			gameBrowsers_[1]->RequestRefresh();
		}
	}
}

void UmdReplaceScreen::CreateViews() {
	using namespace UI;
	Margins actionMenuMargins(0, 100, 15, 0);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, TabHolderFlags::Default, nullptr, nullptr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
	leftColumn->SetTag("UmdReplace");
	leftColumn->SetClip(true);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(270, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	if (g_Config.iMaxRecent > 0) {
		ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollRecentGames->SetTag("UmdReplaceRecentGames");
		GameBrowser *tabRecentGames = new GameBrowser(GetRequesterToken(),
			Path("!RECENT"), BrowseFlags::NONE, portrait, &g_Config.bGridView1, screenManager(), "", "",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		scrollRecentGames->Add(tabRecentGames);
		leftColumn->AddTab(mm->T("Recent"), ImageID::invalid(), scrollRecentGames);
		tabRecentGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
		tabRecentGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
	}
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollAllGames->SetTag("UmdReplaceAllGames");

	GameBrowser *tabAllGames = new GameBrowser(GetRequesterToken(), Path(g_Config.currentDirectory), BrowseFlags::STANDARD, portrait, &g_Config.bGridView2, screenManager(),
		mm->T("How to get games"), "https://www.ppsspp.org/getgames.html",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	scrollAllGames->Add(tabAllGames);

	leftColumn->AddTab(mm->T("Games"), ImageID::invalid(), scrollAllGames);

	tabAllGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	tabAllGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		rightColumnItems->Add(new Choice(mm->T("Load", "Load...")))->OnClick.Add([&](UI::EventParams &e) {
			auto mm = GetI18NCategory(I18NCat::MAINMENU);
			System_BrowseForFile(GetRequesterToken(), mm->T("Load"), BrowseFileType::BOOTABLE, [this](const std::string &value, int) {
				__UmdReplace(Path(value));
				TriggerFinish(DR_OK);
			});
		});
	}

	rightColumnItems->Add(new Choice(di->T("Cancel")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
	rightColumnItems->Add(new Spacer());
	rightColumnItems->Add(new Choice(mm->T("Game Settings")))->OnClick.Handle(this, &UmdReplaceScreen::OnGameSettings);

	if (g_recentFiles.HasAny()) {
		leftColumn->SetCurrentTab(0, true);
	} else if (g_Config.iMaxRecent > 0) {
		leftColumn->SetCurrentTab(1, true);
	}

	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

void UmdReplaceScreen::update() {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update();
}

void UmdReplaceScreen::OnGameSelected(UI::EventParams &e) {
	__UmdReplace(Path(e.s));
	TriggerFinish(DR_OK);
}

void UmdReplaceScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(Path()));
}

void GridSettingsPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);
	items->SetSpacing(0.0f);

	items->Add(new CheckBox(&g_Config.bGridView1, sy->T("Display Recent on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView2, sy->T("Display Games on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView3, sy->T("Display Homebrew on a grid")));
	static const char *defaultTabs[] = { "Recent", "Games", "Homebrew & Demos" };
	PopupMultiChoice *beziersChoice = items->Add(new PopupMultiChoice(&g_Config.iDefaultTab, sy->T("Default tab"), defaultTabs, 0, ARRAY_SIZE(defaultTabs), I18NCat::MAINMENU, screenManager()));

	items->Add(new ItemHeader(sy->T("Grid icon size")));
	items->Add(new Choice(sy->T("Increase size")))->OnClick.Handle(this, &GridSettingsPopupScreen::GridPlusClick);
	items->Add(new Choice(sy->T("Decrease size")))->OnClick.Handle(this, &GridSettingsPopupScreen::GridMinusClick);

	items->Add(new ItemHeader(sy->T("Display Extra Info")));
	items->Add(new CheckBox(&g_Config.bShowIDOnGameIcon, sy->T("Show ID")));
	items->Add(new CheckBox(&g_Config.bShowRegionOnGameIcon, sy->T("Show region flag")));

	if (g_Config.iMaxRecent > 0) {
		items->Add(new ItemHeader(sy->T("Clear Recent")));
		items->Add(new Choice(sy->T("Clear Recent Games List")))->OnClick.Handle(this, &GridSettingsPopupScreen::OnRecentClearClick);
	}

	scroll->Add(items);
	parent->Add(scroll);
}

void GridSettingsPopupScreen::GridPlusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::min(g_Config.fGameGridScale*1.25f, MAX_GAME_GRID_SCALE);
}

void GridSettingsPopupScreen::GridMinusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::max(g_Config.fGameGridScale/1.25f, MIN_GAME_GRID_SCALE);
}

void GridSettingsPopupScreen::OnRecentClearClick(UI::EventParams &e) {
	g_recentFiles.Clear();
	OnRecentChanged.Trigger(e);
	TriggerFinish(DR_OK);
}
