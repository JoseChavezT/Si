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

#include "Common/System/Display.h"
#include "Common/Render/TextureAtlas.h"
#include <functional>

#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Color/RGBAUtil.h"

#include "Core/Config.h"

#include "UI/TouchControlVisibilityScreen.h"
#include "UI/GamepadEmu.h"
#include "Common/System/System.h"
#include "Core/KeyMap.h"
#include "UI/TryledDraw.h"
#include "UI/CustomButtonMappingScreen.h"

static const int leftColumnWidth = 140;

class CheckBoxChoice : public UI::Choice {
public:
	CheckBoxChoice(std::string_view text, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(text, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CheckBoxChoice::HandleClick);
	}
	CheckBoxChoice(ImageID imgID, UI::CheckBox *checkbox, UI::LayoutParams *lp)
		: Choice(imgID, lp), checkbox_(checkbox) {
		OnClick.Handle(this, &CheckBoxChoice::HandleClick);
	}

private:
	void HandleClick(UI::EventParams &e);

	UI::CheckBox *checkbox_;
};

namespace {

// Tryled: a card for one touch button: its symbol, its name and an on/off switch.
// For combined buttons, a tap on the card opens its setup and the switch shows or hides it.
class TryledButtonTile : public UI::Clickable {
public:
	TryledButtonTile(const std::string &name, const std::string &sub, ImageID img, const std::string &glyph, bool *value, UI::LayoutParams *lp)
		: UI::Clickable(lp), name_(name), sub_(sub), img_(img), glyph_(glyph), value_(value) {
		OnClick.Add([this](UI::EventParams &e) {
			const bool onSwitch = lastTouchX_ >= bounds_.x2() - 76.0f;
			if (onOpen && !onSwitch) {
				onOpen(e);
			} else if (value_) {
				*value_ = !*value_;
			}
			lastTouchX_ = -1.0f;
		});
	}
	std::function<void(UI::EventParams &)> onOpen;

	bool Touch(const TouchInput &input) override {
		if (input.flags & (TouchInputFlags::DOWN | TouchInputFlags::UP))
			lastTouchX_ = input.x;
		return UI::Clickable::Touch(input);
	}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 280.0f;
		h = 72.0f;
	}
	void Draw(UIContext &dc) override {
		const UI::Theme &t = dc.GetTheme();
		const bool on = value_ && *value_;
		const float r = std::clamp((float)g_Config.iTryledCornerRadius, 0.0f, 22.0f);
		const Bounds b = bounds_.Inset(4.0f, 4.0f);
		uint32_t bg = t.itemStyle.background.color;
		if (down_)
			bg = t.itemDownStyle.background.color;
		TryledDraw::FillRoundRect(dc, b, r, bg);
		const uint32_t accent = t.itemFocusedStyle.background.color | 0xFF000000;
		if (HasFocus())
			TryledDraw::StrokeRoundRect(dc, b, r, 2.0f, accent);
		else if (on)
			TryledDraw::StrokeRoundRect(dc, b, r, 1.0f, colorAlpha(accent & 0xFFFFFF, 0.45f));

		const uint32_t fg = t.itemStyle.fgColor;
		const uint32_t symbol = on ? fg : colorAlpha(fg & 0xFFFFFF, 0.35f);
		// Symbol in a round badge on the left.
		const float cx = b.x + 34.0f, cy = b.centerY();
		TryledDraw::FillRoundRect(dc, Bounds(cx - 22.0f, cy - 22.0f, 44.0f, 44.0f), 22.0f, colorAlpha(fg & 0xFFFFFF, 0.08f));
		if (img_.isValid()) {
			const AtlasImage *ai = dc.Draw()->GetAtlas()->getImage(img_);
			const float scale = ai ? std::min(1.0f, 30.0f / std::max(1.0f, (float)std::max(ai->w, ai->h))) : 1.0f;
			dc.Draw()->DrawImage(img_, cx, cy, scale, symbol, ALIGN_CENTER);
		} else {
			dc.SetFontStyle(t.uiFont);
			dc.SetFontScale(0.8f, 0.8f);
			dc.DrawTextRectSqueeze(glyph_, Bounds(cx - 22.0f, cy - 22.0f, 44.0f, 44.0f), symbol, ALIGN_CENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}

		// Name and note.
		const float tx = b.x + 66.0f, tw = b.x2() - 76.0f - tx;
		dc.SetFontStyle(t.uiFont);
		if (sub_.empty()) {
			dc.DrawTextRectSqueeze(name_, Bounds(tx, b.y, tw, b.h), on ? fg : symbol, ALIGN_LEFT | ALIGN_VCENTER);
		} else {
			dc.DrawTextRectSqueeze(name_, Bounds(tx, b.y + 6.0f, tw, b.h * 0.5f), on ? fg : symbol, ALIGN_LEFT | ALIGN_VCENTER);
			dc.SetFontScale(0.7f, 0.7f);
			dc.DrawTextRectSqueeze(sub_, Bounds(tx, b.centerY(), tw, b.h * 0.5f - 6.0f), colorAlpha(fg & 0xFFFFFF, 0.6f), ALIGN_LEFT | ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}

		// Switch.
		if (value_) {
			const Bounds sw(b.x2() - 62.0f, cy - 13.0f, 46.0f, 26.0f);
			TryledDraw::FillRoundRect(dc, sw, 13.0f, on ? accent : colorAlpha(fg & 0xFFFFFF, 0.2f));
			const float kx = on ? sw.x2() - 13.0f : sw.x + 13.0f;
			TryledDraw::FillRoundRect(dc, Bounds(kx - 10.0f, cy - 10.0f, 20.0f, 20.0f), 10.0f, on ? (t.itemFocusedStyle.fgColor | 0xFF000000) : 0xFFFFFFFF);
		}
	}

private:
	std::string name_, sub_;
	ImageID img_;
	std::string glyph_;
	bool *value_;
	float lastTouchX_ = -1.0f;
};

// Names of what a combined button presses, e.g. "Equis + Cuadrado".
std::string ComboDescription(uint64_t mask) {
	using namespace CustomKeyData;
	static const char *const names[] = { "Cuadrado", "Triángulo", "Círculo", "Equis", "Arriba", "Abajo", "Izquierda", "Derecha", "Start", "Select", "L", "R" };
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
	std::string out;
	for (int j = 0; j < (int)ARRAY_SIZE(g_customKeyList); j++) {
		if (!(mask & (1ULL << j)))
			continue;
		if (!out.empty())
			out += " + ";
		if (j < (int)ARRAY_SIZE(names))
			out += names[j];
		else
			out += std::string(mc->T(KeyMap::GetPspButtonName(g_customKeyList[j].c)));
	}
	return out;
}

}  // namespace

std::string_view TouchControlVisibilityScreen::GetTitle() const {
	return "Botones en pantalla";
}

void TouchControlVisibilityScreen::CreateContextMenu(UI::ViewGroup *parent) {
	using namespace UI;

	Choice *toggleAll = parent->Add(new Choice("Mostrar u ocultar todos"));
	toggleAll->OnClick.Add([this](UI::EventParams &e) {
		for (auto toggle : toggles_) {
			*toggle.show = nextToggleAll_;
		}
		nextToggleAll_ = !nextToggleAll_;
	});
}

void TouchControlVisibilityScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	UISimpleBaseDialogScreen::dialogFinished(dialog, result);
	RecreateViews();  // A combined button may have new keys.
}

void TouchControlVisibilityScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;
	const int cellSize = portrait ? std::min((g_display.dp_xres / 2 - 10), 290) : 300;
	UI::GridLayoutSettings gridsettings(cellSize, 76, 4);
	gridsettings.fillCells = true;

	TouchControlConfig &touch = g_Config.GetTouchControlsConfig(GetDeviceOrientation());

	struct Item {
		const char *name;
		bool *show;
		ImageID img;
		const char *glyph;
	};
	const Item items[] = {
		{ "Equis", &touch.bShowTouchCross, ImageID("I_CROSS"), "" },
		{ "Círculo", &touch.bShowTouchCircle, ImageID("I_CIRCLE"), "" },
		{ "Cuadrado", &touch.bShowTouchSquare, ImageID("I_SQUARE"), "" },
		{ "Triángulo", &touch.bShowTouchTriangle, ImageID("I_TRIANGLE"), "" },
		{ "Cruceta", &touch.touchDpad.show, ImageID("I_ARROW"), "" },
		{ "Joystick", &touch.touchAnalogStick.show, ImageID("I_STICK_LINE"), "" },
		{ "L", &touch.touchLKey.show, ImageID("I_L"), "" },
		{ "R", &touch.touchRKey.show, ImageID("I_R"), "" },
		{ "Start", &touch.touchStartKey.show, ImageID::invalid(), "START" },
		{ "Select", &touch.touchSelectKey.show, ImageID::invalid(), "SEL" },
		{ "Avance rápido", &touch.touchFastForwardKey.show, ImageID("I_FAST_FORWARD_LINE"), "" },
		{ "Menú (pausa)", &touch.touchPauseKey.show, ImageID("I_HAMBURGER"), "" },
	};

	toggles_.clear();
	parent->Add(new ItemHeader("Botones"));
	GridLayout *grid = parent->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	for (const Item &it : items) {
		toggles_.push_back({ it.name, it.show, it.img, nullptr });
		TryledButtonTile *tile = grid->Add(new TryledButtonTile(it.name, "", it.img, it.glyph, it.show, new LinearLayoutParams(FILL_PARENT, 76.0f)));
		// Cannot hide the menu button if the system doesn't have a built-in back button.
		if (it.show == &touch.touchPauseKey.show && !System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON))
			tile->SetEnabled(false);
	}
	toggles_.push_back({ "Right Analog Stick", &touch.touchRightAnalogStick.show, ImageID::invalid(), nullptr });
	TryledButtonTile *right = grid->Add(new TryledButtonTile("Joystick derecho", "Toca para elegir qué hace", ImageID("I_STICK_LINE"), "", &touch.touchRightAnalogStick.show, new LinearLayoutParams(FILL_PARENT, 76.0f)));
	right->onOpen = [this](EventParams &e) {
		screenManager()->push(new RightAnalogMappingScreen(gamePath_));
	};

	// Combined buttons: PPSSPP's custom buttons, which can press several buttons at once.
	parent->Add(new ItemHeader("Botones combinados"));
	TextView *about = parent->Add(new TextView("Un botón que pulsa varios a la vez, por ejemplo Equis + Cuadrado. Toca uno para elegir qué pulsa, su forma y su icono; el interruptor lo muestra u oculta.", FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 4, 12, 8))));
	about->SetTextColor(0xB0FFFFFF);
	GridLayout *combos = parent->Add(new GridLayoutList(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	int freeSlot = -1;
	for (int i = 0; i < TouchControlConfig::CUSTOM_BUTTON_COUNT; i++) {
		const bool used = touch.touchCustom[i].show || g_Config.CustomButton[i].key != 0;
		toggles_.push_back({ StringFromFormat("Custom %d", i + 1), &touch.touchCustom[i].show, ImageID::invalid(), nullptr });
		if (!used) {
			if (freeSlot < 0)
				freeSlot = i;
			continue;
		}
		std::string keys = ComboDescription(g_Config.CustomButton[i].key);
		if (keys.empty())
			keys = "Sin asignar";
		TryledButtonTile *tile = combos->Add(new TryledButtonTile(StringFromFormat("Combinado %d", i + 1), keys,
			CustomKeyData::customKeyImages[std::min(g_Config.CustomButton[i].image, (int)ARRAY_SIZE(CustomKeyData::customKeyImages) - 1)].i, "", &touch.touchCustom[i].show,
			new LinearLayoutParams(FILL_PARENT, 76.0f)));
		tile->onOpen = [this, i](EventParams &e) {
			screenManager()->push(new CustomButtonMappingScreen(GetDeviceOrientation(), gamePath_, i));
		};
	}
	if (freeSlot >= 0) {
		Choice *add = combos->Add(new Choice("+ Nuevo combinado", new LinearLayoutParams(FILL_PARENT, 68.0f, Margins(4))));
		add->SetCentered(true);
		add->OnClick.Add([this, freeSlot, &touch](EventParams &e) {
			touch.touchCustom[freeSlot].show = true;
			screenManager()->push(new CustomButtonMappingScreen(GetDeviceOrientation(), gamePath_, freeSlot));
		});
	}
}

void TouchControlVisibilityScreen::onFinish(DialogResult result) {
	g_Config.Save("TouchControlVisibilityScreen::onFinish");
}

std::string_view RightAnalogMappingScreen::GetTitle() const {
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);
	return mc->T("Right Analog Stick");
}

void RightAnalogMappingScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	TouchControlConfig &touch = g_Config.GetTouchControlsConfig(GetDeviceOrientation());

	static const char *rightAnalogButton[] = {"None", "L", "R", "Square", "Triangle", "Circle", "Cross", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Select", "RightAn.Up", "RightAn.Down", "RightAn.Left", "RightAn.Right", "An.Up", "An.Down", "An.Left", "An.Right"};

	parent->Add(new ItemHeader(co->T("Analog Style")));
	parent->Add(new CheckBox(&touch.touchRightAnalogStick.show, co->T("Visible")));
	parent->Add(new CheckBox(&g_Config.bRightAnalogCustom, co->T("Use custom right analog")));
	parent->Add(new CheckBox(&g_Config.bRightAnalogDisableDiagonal, co->T("Disable diagonal input")))->SetEnabledPtr(&g_Config.bRightAnalogCustom);

	parent->Add(new ItemHeader(co->T("Analog Binding")));
	parent->Add(new PopupMultiChoice(&g_Config.iRightAnalogUp, mc->T("RightAn.Up"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	parent->Add(new PopupMultiChoice(&g_Config.iRightAnalogDown, mc->T("RightAn.Down"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	parent->Add(new PopupMultiChoice(&g_Config.iRightAnalogLeft, mc->T("RightAn.Left"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	parent->Add(new PopupMultiChoice(&g_Config.iRightAnalogRight, mc->T("RightAn.Right"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bRightAnalogCustom);
	parent->Add(new PopupMultiChoice(&g_Config.iRightAnalogPress, co->T("Keep this button pressed when right analog is pressed"), rightAnalogButton, 0, ARRAY_SIZE(rightAnalogButton) - 8, I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&g_Config.bRightAnalogCustom);
}

void CheckBoxChoice::HandleClick(UI::EventParams &e) {
	checkbox_->Toggle();
};
