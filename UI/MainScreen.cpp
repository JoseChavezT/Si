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
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Util/DarwinFileSystemServices.h" // For the browser

#include "Core/HLE/sceUmd.h"
#include "Core/SaveState.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/TimeUtil.h"
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

namespace {

uint32_t ThemeAccent(const UIContext &dc) {
	return dc.GetTheme().itemFocusedStyle.background.color;
}

uint32_t ThemeAccentText(const UIContext &dc) {
	return dc.GetTheme().itemFocusedStyle.fgColor;
}

// One game in the top row. Draws the game's icon; the selected one gets an accent frame.
class ConsoleGameTile : public UI::Clickable {
public:
	ConsoleGameTile(const Path &path, int index, std::function<bool(int)> isSelected, std::function<void(int)> onFocus, UI::LayoutParams *layoutParams)
		: UI::Clickable(layoutParams), path_(path), index_(index), isSelected_(isSelected), onFocus_(onFocus) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 144.0f;
		h = 80.0f;
	}

	void FocusChanged(UI::FocusFlags focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
		if ((focusFlags & UI::FocusFlags::GOT_FOCUS) && onFocus_) {
			onFocus_(index_);
		}
	}

	void Draw(UIContext &dc) override {
		const bool selected = isSelected_ && isSelected_(index_);
		const uint32_t accent = ThemeAccent(dc);
		Bounds b = bounds_;

		if (selected) {
			dc.FillRect(UI::Drawable(accent), b.Expand(4.0f));
			dc.FillRect(UI::Drawable(dc.GetTheme().backgroundColor | 0xFF000000), b.Expand(1.5f));
		}
		dc.FillRect(UI::Drawable(dc.GetTheme().itemStyle.background.color | 0xFF000000), b);

		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), path_, GameInfoFlags::ICON | GameInfoFlags::PARAM_SFO);
		Draw::Texture *texture = (ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) ? ginfo->icon.texture : nullptr;
		if (texture) {
			uint32_t color = selected ? 0xFFFFFFFF : 0xFFB0B0B0;
			dc.Draw()->Flush();
			dc.GetDrawContext()->BindTexture(0, texture);
			dc.Draw()->DrawTexRect(b.x, b.y, b.x2(), b.y2(), 0, 0, 1, 1, color);
			dc.Draw()->Flush();
			dc.RebindTexture();
		} else {
			// No icon (homebrew, still loading...): show the initials of the title.
			std::string title = ginfo->Ready(GameInfoFlags::PARAM_SFO) ? ginfo->GetTitle() : path_.GetFilename();
			std::string initials;
			bool newWord = true;
			for (char c : title) {
				if (c == ' ' || c == '_' || c == '-') {
					newWord = true;
				} else if (newWord && initials.size() < 2 && (unsigned char)c < 0x80 && isalnum((unsigned char)c)) {
					initials.push_back((char)toupper((unsigned char)c));
					newWord = false;
				} else {
					newWord = false;
				}
			}
			dc.SetFontStyle(dc.GetTheme().uiFontBig);
			dc.DrawText(initials, b.centerX(), b.centerY(), dc.GetTheme().itemStyle.fgColor, ALIGN_CENTER);
			dc.SetFontStyle(dc.GetTheme().uiFont);
		}
		if (HasFocus() && !selected) {
			dc.Draw()->RectOutline(b.x, b.y, b.w, b.h, accent);
		}
	}

private:
	Path path_;
	int index_;
	std::function<bool(int)> isSelected_;
	std::function<void(int)> onFocus_;
};

// Big title text, drawn at a larger scale than the theme's biggest font.
class ConsoleTitleView : public UI::TextView {
public:
	ConsoleTitleView(UI::LayoutParams *layoutParams) : UI::TextView("", layoutParams) {}
	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override {
		w = horiz.size > 0 ? horiz.size : 600.0f;
		h = 76.0f;
	}
	void Draw(UIContext &dc) override {
		dc.SetFontStyle(dc.GetTheme().uiFontBig);
		dc.SetFontScale(1.9f, 1.9f);
		dc.DrawTextRectSqueeze(GetText(), bounds_, dc.GetTheme().infoStyle.fgColor, ALIGN_LEFT | ALIGN_VCENTER);
		dc.SetFontScale(1.0f, 1.0f);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}
};

// A pill-ish button. "primary" uses the theme accent as background.
class ConsoleButton : public UI::Choice {
public:
	ConsoleButton(std::string_view text, ImageID image, bool primary, UI::LayoutParams *layoutParams)
		: UI::Choice(text, image, layoutParams), primary_(primary) {}
	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override {
		UI::Choice::GetContentDimensionsBySpec(dc, horiz, vert, w, h);
		w += primary_ ? 48.0f : 8.0f;
		h = 56.0f;
	}
	void Draw(UIContext &dc) override {
		uint32_t bg = primary_ ? ThemeAccent(dc) : colorAlpha(dc.GetTheme().infoStyle.fgColor & 0xFFFFFF, 0.16f);
		uint32_t fg = primary_ ? ThemeAccentText(dc) : dc.GetTheme().infoStyle.fgColor;
		if (down_) {
			bg = dc.GetTheme().itemDownStyle.background.color;
			fg = dc.GetTheme().itemDownStyle.fgColor;
		}
		dc.FillRect(UI::Drawable(bg), bounds_);
		if (HasFocus()) {
			dc.Draw()->RectOutline(bounds_.x - 3, bounds_.y - 3, bounds_.w + 6, bounds_.h + 6, ThemeAccent(dc));
		}
		float x = bounds_.x + 18.0f;
		if (image_.isValid()) {
			dc.Draw()->DrawImage(image_, x + 14.0f, bounds_.centerY(), 0.8f, fg, ALIGN_CENTER);
			x += 36.0f;
		}
		if (!text_.empty()) {
			dc.SetFontStyle(dc.GetTheme().uiFont);
			dc.DrawText(text_, x, bounds_.centerY(), fg, ALIGN_VCENTER);
		}
	}
private:
	bool primary_;
};

// Info card: small label on top, value below.
class ConsoleCard : public UI::LinearLayout {
public:
	ConsoleCard(UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_VERTICAL, layoutParams) {}
	void Draw(UIContext &dc) override {
		dc.FillRect(UI::Drawable(colorAlpha(dc.GetTheme().itemStyle.background.color & 0xFFFFFF, 0.85f)), bounds_);
		UI::LinearLayout::Draw(dc);
	}
};

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

}  // namespace

bool MainScreen::ConsoleMode() const {
	return !s_tryledLibraryMode;
}

void MainScreen::CreateConsoleViews() {
	using namespace UI;

	consoleGames_.clear();
	for (const std::string &file : g_recentFiles.GetRecentFiles()) {
		consoleGames_.push_back(Path(file));
	}
	if (s_tryledSelected >= (int)consoleGames_.size())
		s_tryledSelected = 0;

	LinearLayout *rootLayout = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	rootLayout->SetSpacing(0.0f);
	root_ = rootLayout;

	// Top bar: section names on the left, settings and clock on the right.
	LinearLayout *topBar = rootLayout->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 12, 24, 0))));
	topBar->SetSpacing(12.0f);
	TextView *gamesLabel = topBar->Add(new TextView("Juegos", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
	gamesLabel->SetBig(true);
	Choice *libraryTab = topBar->Add(new Choice("Biblioteca", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));
	libraryTab->OnClick.Add([this](EventParams &) {
		s_tryledLibraryMode = true;
		RecreateViews();
	});
	topBar->Add(new Spacer(new LinearLayoutParams(1.0f)));
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		topBar->Add(new Choice(ImageID("I_FOLDER_OPEN"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Handle(this, &MainScreen::OnLoadFile);
	}
	topBar->Add(new Choice(ImageID("I_GEAR"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	consoleClock_ = topBar->Add(new TextView(ClockString(), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 0.0f, Gravity::G_VCENTER)));

	// The row of games.
	ScrollView *rowScroll = rootLayout->Add(new ScrollView(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, 104.0f, Margins(0, 4, 0, 0))));
	LinearLayout *row = rowScroll->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT)));
	row->SetSpacing(14.0f);
	row->Add(new Spacer(10.0f));
	View *firstTile = nullptr;
	for (int i = 0; i < (int)consoleGames_.size(); i++) {
		ConsoleGameTile *tile = row->Add(new ConsoleGameTile(consoleGames_[i], i,
			[](int index) { return index == s_tryledSelected; },
			[this](int index) { ConsoleSelect(index); },
			new LinearLayoutParams(144.0f, 80.0f, 0.0f, Gravity::G_VCENTER)));
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
	Choice *libraryTile = row->Add(new Choice(ImageID("I_GRID"), new LinearLayoutParams(80.0f, 80.0f, 0.0f, Gravity::G_VCENTER)));
	libraryTile->OnClick.Add([this](EventParams &) {
		s_tryledLibraryMode = true;
		RecreateViews();
	});
	row->Add(new Spacer(10.0f));

	// Selected game: small name, big title, description.
	LinearLayout *hero = rootLayout->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(28, 6, 28, 0))));
	hero->SetSpacing(2.0f);
	consoleSmallName_ = hero->Add(new TextView("", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	consoleTitle_ = hero->Add(new ConsoleTitleView(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	consoleSub_ = hero->Add(new TextView("", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	LinearLayout *buttons = hero->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Margins(0, 14, 0, 0))));
	buttons->SetSpacing(12.0f);
	ConsoleButton *playButton;
	if (consoleGames_.empty()) {
		playButton = buttons->Add(new ConsoleButton("Abrir Biblioteca", ImageID("I_GRID"), true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
		playButton->OnClick.Add([this](EventParams &) {
			s_tryledLibraryMode = true;
			RecreateViews();
		});
	} else {
		playButton = buttons->Add(new ConsoleButton("Jugar", ImageID("I_PLAY"), true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
		playButton->OnClick.Add([this](EventParams &) { ConsoleLaunchSelected(); });
		ConsoleButton *more = buttons->Add(new ConsoleButton("", ImageID("I_THREE_DOTS"), false, new LinearLayoutParams(64.0f, 56.0f)));
		more->OnClick.Add([this](EventParams &) {
			if (s_tryledSelected < (int)consoleGames_.size()) {
				screenManager()->push(new GameScreen(consoleGames_[s_tryledSelected], false));
			}
		});
	}

	rootLayout->Add(new Spacer(new LinearLayoutParams(1.0f)));

	// Info cards at the bottom.
	static const char *const cardLabels[3] = { "Tiempo jugado", "Guardados rápidos", "Ajustes del juego" };
	LinearLayout *cards = rootLayout->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 0, 24, 16))));
	cards->SetSpacing(12.0f);
	for (int i = 0; i < 3; i++) {
		ConsoleCard *card = cards->Add(new ConsoleCard(new LinearLayoutParams(1.0f)));
		card->SetSpacing(0.0f);
		TextView *label = card->Add(new TextView(cardLabels[i], ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 8, 12, 0))));
		consoleCardValue_[i] = card->Add(new TextView("—", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0, 12, 8))));
	}

	root_->SetDefaultFocusView(firstTile ? firstTile : (View *)playButton);
	root_->SetTag("mainroot");

	ConsoleRefreshInfo();
}

void MainScreen::ConsoleSelect(int index) {
	if (index < 0 || index >= (int)consoleGames_.size())
		return;
	if (index != s_tryledSelected) {
		s_tryledSelected = index;
		g_BackgroundAudio.SetGame(consoleGames_[index]);
	}
	ConsoleRefreshInfo();
}

void MainScreen::ConsoleLaunchSelected() {
	if (s_tryledSelected >= (int)consoleGames_.size())
		return;
	Path path = consoleGames_[s_tryledSelected];
	// After playing, this game moves to the front of the recent list.
	s_tryledSelected = 0;
	LaunchFile(screenManager(), this, path);
}

void MainScreen::ConsoleRefreshInfo() {
	consoleLastRefresh_ = time_now_d();
	if (consoleClock_)
		consoleClock_->SetText(ClockString());
	if (!consoleTitle_)
		return;

	if (consoleGames_.empty()) {
		consoleSmallName_->SetText("Tryled");
		consoleTitle_->SetText("Tu colección está vacía");
		consoleSub_->SetText("Abre la Biblioteca y elige la carpeta donde guardas tus juegos. Los que juegues aparecerán aquí.");
		for (auto *card : consoleCardValue_)
			card->SetText("—");
		return;
	}

	const Path &path = consoleGames_[s_tryledSelected];
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::PARAM_SFO);
	const bool ready = ginfo->Ready(GameInfoFlags::PARAM_SFO);
	std::string title = ready ? ginfo->GetTitle() : path.GetFilename();

	std::string kind = "PSP";
	if (ginfo->fileType == IdentifiedFileType::PSP_ELF || ginfo->fileType == IdentifiedFileType::PSP_PBP_DIRECTORY)
		kind = "Homebrew";
	consoleSmallName_->SetText(kind + "  ·  " + (ginfo->id_version.empty() ? path.GetFilename() : ginfo->id_version));
	consoleTitle_->SetText(title);

	int seconds = 0;
	const bool played = !ginfo->id.empty() && g_Config.TimeTracker().GetPlayedSeconds(ginfo->id, &seconds);
	consoleSub_->SetText(played ? "Pulsa Jugar para continuar donde lo dejaste." : "Todavía no lo has jugado en Tryled.");
	consoleCardValue_[0]->SetText(played ? FormatPlayTime(seconds) : "Sin jugar");

	int saves = 0;
	if (ready) {
		const std::string prefix = SaveState::GetGamePrefix(ginfo->GetParamSFO());
		for (int slot = 0; slot < g_Config.iSaveStateSlotCount; slot++) {
			if (SaveState::HasSaveInSlot(prefix, slot))
				saves++;
		}
	}
	consoleCardValue_[1]->SetText(saves == 0 ? std::string("Ninguno") : StringFromFormat("%d %s", saves, saves == 1 ? "ranura" : "ranuras"));
	consoleCardValue_[2]->SetText(ginfo->hasConfig ? "Personalizados" : "Los globales");
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	// Tryled: the console-style home is the default. The classic browser is the "Biblioteca".
	consoleTitle_ = nullptr;
	consoleClock_ = nullptr;
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
		if (consoleGames_.empty() || s_tryledSelected >= (int)consoleGames_.size())
			return;
		std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), consoleGames_[s_tryledSelected], GameInfoFlags::PIC1);
		if (!ginfo->Ready(GameInfoFlags::PIC1) || !ginfo->pic1.texture)
			return;
		const float alpha = std::clamp(float(time_now_d() - ginfo->pic1.timeLoaded) * 3.0f, 0.0f, 1.0f);
		DrawBackgroundTexture(dc, ginfo->pic1.texture, Lin::Vec3(0.0f, 0.0f, 0.0f), alpha);
		// Fade the artwork into the theme background on the left (where the text is) and at the bottom.
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
