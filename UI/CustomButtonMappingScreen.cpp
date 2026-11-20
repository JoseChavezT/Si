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
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/PopupScreens.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Math/curves.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"

#include "UI/CustomButtonMappingScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TryledDraw.h"
#include "Core/KeyMap.h"
#include <functional>

class ButtonShapeScreen : public UI::PopupScreen {
public:
	ButtonShapeScreen(std::string_view title, int *setting) : PopupScreen(title), setting_(setting) {}

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		using namespace CustomKeyData;

		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
		LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);

		for (int i = 0; i < ARRAY_SIZE(customKeyShapes); ++i) {
			Choice *c = items->Add(new Choice(ImageID(customKeyShapes[i].l), 0.6f, customKeyShapes[i].r*PI/180, customKeyShapes[i].f));
			c->OnClick.Add([=](UI::EventParams &e) {
				*setting_ = i;
				TriggerFinish(DR_OK);
			});
		}

		scroll->Add(items);
		parent->Add(scroll);
	}

	const char *tag() const override { return "ButtonShape"; }

private:
	int *setting_;
};

class ButtonIconScreen : public UI::PopupScreen {
public:
	ButtonIconScreen(std::string_view title, int *setting) : PopupScreen(title), setting_(setting) {}

	void CreatePopupContents(UI::ViewGroup *parent) override {
		using namespace UI;
		using namespace CustomKeyData;

		ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
		LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);

		for (int i = 0; i < ARRAY_SIZE(customKeyImages); ++i) {
			Choice *c = items->Add(new Choice(ImageID(customKeyImages[i].i), 1.0f, customKeyImages[i].r*PI/180));
			c->OnClick.Add([=](UI::EventParams &e) {
				*setting_ = i;
				TriggerFinish(DR_OK);
			});
		}

		scroll->Add(items);
		parent->Add(scroll);
	}

	const char *tag() const override { return "ButtonIcon"; }

private:
	int *setting_;
};

class ButtonPreview : public UI::View {
public:
	ButtonPreview(ImageID bgImg, ImageID img, float rotationIcon, bool flipShape, float rotationShape, int x, int y)
		: View(new UI::AnchorLayoutParams(x, y, UI::NONE, UI::NONE, UI::Centering::Both)), bgImg_(bgImg), img_(img), rotI_(rotationIcon),
		flipS_(flipShape), rotS_(rotationShape), x_(x), y_(y) {}

	void Draw(UIContext &dc) override {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		dc.Draw()->DrawImageRotated(bgImg_, x_, y_, 1.0f, rotS_*PI/180, colorBg, flipS_);
		dc.Draw()->DrawImageRotated(img_, x_, y_, 1.0f, rotI_*PI/180, color, false);
	}
private:
	int x_;
	int y_;
	float rotI_;
	float rotS_;
	bool flipS_;
	ImageID bgImg_;
	ImageID img_;
};

std::string_view CustomButtonMappingScreen::GetTitle() const {
	return title_;
}

static uint64_t arrayToInt(const bool ary[ARRAY_SIZE(CustomKeyData::g_customKeyList)]);

namespace {

// Tryled: a chip for one button a combined button can press. Lit up in the accent color when chosen.
class TryledKeyChip : public UI::Clickable {
public:
	TryledKeyChip(bool *value, ImageID img, float angle, const std::string &text, UI::LayoutParams *lp)
		: UI::Clickable(lp), value_(value), img_(img), angle_(angle), text_(text) {
		OnClick.Add([this](UI::EventParams &) {
			*value_ = !*value_;
			if (onChange)
				onChange();
		});
	}
	std::function<void()> onChange;

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 90.0f;
		h = 52.0f;
	}
	void Draw(UIContext &dc) override {
		const UI::Theme &t = dc.GetTheme();
		const bool on = *value_;
		const Bounds b = bounds_.Inset(3.0f, 3.0f);
		const float r = std::clamp((float)g_Config.iTryledCornerRadius * 0.8f, 0.0f, 18.0f);
		uint32_t bg = on ? (t.itemFocusedStyle.background.color | 0xFF000000) : t.itemStyle.background.color;
		uint32_t fg = on ? (t.itemFocusedStyle.fgColor | 0xFF000000) : t.itemStyle.fgColor;
		if (down_)
			bg = t.itemDownStyle.background.color;
		if (on)
			TryledDraw::Glow(dc, b, r, 8.0f, (bg & 0xFFFFFF) | 0x60000000);
		TryledDraw::FillRoundRect(dc, b, r, bg);
		if (HasFocus())
			TryledDraw::StrokeRoundRect(dc, b.Expand(3.0f), r + 3.0f, 2.0f, t.infoStyle.fgColor);
		if (img_.isValid()) {
			const AtlasImage *ai = dc.Draw()->GetAtlas()->getImage(img_);
			const float scale = ai ? std::min(1.0f, b.h * 0.55f / std::max(1.0f, (float)std::max(ai->w, ai->h))) : 1.0f;
			dc.Draw()->DrawImageRotated(img_, b.centerX(), b.centerY(), scale, angle_, fg);
		} else {
			dc.SetFontStyle(t.uiFont);
			dc.SetFontScale(0.75f, 0.75f);
			dc.DrawTextRectSqueeze(text_, b.Inset(6.0f, 0.0f), fg, ALIGN_CENTER | FLAG_WRAP_TEXT);
			dc.SetFontScale(1.0f, 1.0f);
		}
	}

private:
	bool *value_;
	ImageID img_;
	float angle_;
	std::string text_;
};

}  // namespace

void CustomButtonMappingScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;
	using namespace CustomKeyData;
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	LinearLayout *root__ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0));
	parent->Add(root__);
	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(170, FILL_PARENT));

	TouchControlConfig &touch = g_Config.GetTouchControlsConfig(deviceOrientation_);

	ConfigCustomButton *cfg = &g_Config.CustomButton[id_];
	bool *show = &touch.touchCustom[id_].show;
	memset(array, 0, sizeof(array));
	for (int i = 0; i < ARRAY_SIZE(g_customKeyList); i++)
		array[i] = (0x01 == ((cfg->key >> i) & 0x01));

	// Left: how the button looks and what it presses.
	const Bounds layoutBounds = GetLayoutBounds(*screenManager()->getUIContext());
	leftColumn->Add(new Spacer(130.0f));
	leftColumn->Add(new ButtonPreview(g_Config.iTouchButtonStyle == 0 ? customKeyShapes[cfg->shape].i : customKeyShapes[cfg->shape].l,
		customKeyImages[cfg->image].i, customKeyImages[cfg->image].r, customKeyShapes[cfg->shape].f, customKeyShapes[cfg->shape].r, layoutBounds.x + 85, layoutBounds.y + 110));
	summary_ = leftColumn->Add(new TextView("", ALIGN_HCENTER | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10, 0))));
	UpdateSummary();
	root__->Add(leftColumn);

	ScrollView *rightScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f));
	root__->Add(rightScroll);
	LinearLayout *vertLayout = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 0, 12, 12)));
	rightScroll->Add(vertLayout);

	auto addChips = [&](int from, int to) {
		UI::GridLayoutSettings gridsettings(100, 56, 4);
		gridsettings.fillCells = true;
		GridLayout *grid = vertLayout->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
		// Up, down, left, right: the d-pad arrow, turned.
		static const float arrowAngles[] = { (float)M_PI * 0.5f, (float)-M_PI * 0.5f, 0.0f, (float)M_PI };
		for (int i = from; i < to && i < (int)ARRAY_SIZE(g_customKeyList); ++i) {
			std::string text;
			ImageID img = g_customKeyList[i].i;
			float angle = 0.0f;
			if (i >= 4 && i < 8) {
				img = ImageID("I_ARROW");
				angle = arrowAngles[i - 4];
			} else if (g_customKeyList[i].c == CTRL_START || g_customKeyList[i].c == CTRL_SELECT) {
				img = ImageID::invalid();
				text = g_customKeyList[i].c == CTRL_START ? "START" : "SELECT";
			} else if (!img.isValid()) {
				text = mc->T(KeyMap::GetPspButtonNameCharPointer(g_customKeyList[i].c));
			}
			TryledKeyChip *chip = grid->Add(new TryledKeyChip(&array[i], img, angle, text, new LinearLayoutParams(FILL_PARENT, 56.0f)));
			chip->onChange = [this]() {
				saveArray();
				UpdateSummary();
			};
		}
	};

	vertLayout->Add(new ItemHeader("Qué pulsa (elige uno o varios)"));
	addChips(0, 12);
	vertLayout->Add(new ItemHeader("Otras acciones"));
	addChips(12, (int)ARRAY_SIZE(g_customKeyList));

	vertLayout->Add(new ItemHeader("Aspecto"));
	vertLayout->Add(new CheckBox(show, "Mostrar en pantalla"));
	Choice *icon = vertLayout->Add(new Choice("Icono"));
	icon->SetIconRight(ImageID(customKeyImages[cfg->image].i), 1.0f, customKeyImages[cfg->image].r*PI/180, false, false);
	icon->OnClick.Add([=](UI::EventParams &e) {
		auto iconScreen = new ButtonIconScreen("Icono", &(cfg->image));
		if (e.v)
			iconScreen->SetPopupOrigin(e.v);
		screenManager()->push(iconScreen);
	});
	Choice *shape = vertLayout->Add(new Choice("Forma"));
	shape->SetIconRight(ImageID(customKeyShapes[cfg->shape].l), 0.6f, customKeyShapes[cfg->shape].r*PI/180, customKeyShapes[cfg->shape].f, false);
	shape->OnClick.Add([=](UI::EventParams &e) {
		auto shapeScreen = new ButtonShapeScreen("Forma", &(cfg->shape));
		if (e.v)
			shapeScreen->SetPopupOrigin(e.v);
		screenManager()->push(shapeScreen);
	});

	vertLayout->Add(new ItemHeader("Cómo funciona"));
	vertLayout->Add(new CheckBox(&(cfg->toggle), "Interruptor: un toque lo deja pulsado, otro lo suelta"));
	vertLayout->Add(new CheckBox(&(cfg->repeat), "Repetir rápido mientras lo mantienes"));
}

void CustomButtonMappingScreen::UpdateSummary() {
	if (!summary_)
		return;
	std::string keys = TryledComboDescription(arrayToIntTryled());
	summary_->SetText(keys.empty() ? "Elige qué botones pulsa" : "Pulsa: " + keys);
}

static uint64_t arrayToInt(const bool ary[ARRAY_SIZE(CustomKeyData::g_customKeyList)]) {
	uint64_t value = 0;
	for (int i = ARRAY_SIZE(CustomKeyData::g_customKeyList)-1; i >= 0; i--) {
		value |= ary[i] ? 1 : 0;
		if (i > 0) {
			value = value << 1;
		}
	}
	return value;
}

uint64_t CustomButtonMappingScreen::arrayToIntTryled() const {
	return arrayToInt(array);
}

void CustomButtonMappingScreen::saveArray() {
	if (id_ >= 0 && id_ < TouchControlConfig::CUSTOM_BUTTON_COUNT) {
		g_Config.CustomButton[id_].key = arrayToInt(array);
	}
}

void CustomButtonMappingScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	saveArray();
	RecreateViews();
}

void CustomButtonMappingScreen::onFinish(DialogResult result) {
	saveArray();
	g_Config.Save("CustomButtonMappingScreen::onFinish");
}

void CustomButtonMappingScreen::ChoiceEventHandler::onChoiceClick(UI::EventParams &e){
	checkbox_->Toggle();
};
