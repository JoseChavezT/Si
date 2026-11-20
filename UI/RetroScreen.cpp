// Tryled: game screen for non-PSP consoles. See RetroScreen.h.

#include <algorithm>

#include "Common/GPU/thin3d.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Root.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"
#include "UI/GamepadEmu.h"
#include "UI/RetroScreen.h"
#include "UI/libretro.h"
#include "UI/TryledDraw.h"
#include "UI/GameSettingsScreen.h"
#include "UI/TouchControlLayoutScreen.h"
#include "Common/StringUtils.h"

RetroScreen::RetroScreen(const Path &game, const RetroCoreInfo &core) : game_(game), info_(core) {
	loaded_ = core_.Load(info_, game_, &error_);
	nextFrameTime_ = time_now_d();
	g_controlMapper.AddListener(this);
	if (!loaded_) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, error_, 6.0f);
	}
}

RetroScreen::~RetroScreen() {
	g_controlMapper.RemoveListener(this);
	core_.Unload();
	for (auto *&tex : textures_) {
		if (tex) {
			tex->Release();
			tex = nullptr;
		}
	}
	if (nearest_) {
		nearest_->Release();
		nearest_ = nullptr;
	}
}

void RetroScreen::CreateViews() {
	// Same touch controls as for PSP games (layout, opacity, custom buttons...).
	const DeviceOrientation orientation = GetDeviceOrientation();
	TouchControlConfig &touch = g_Config.GetTouchControlsConfig(orientation);
	const Bounds &bounds = GetLayoutBounds(*screenManager()->getUIContext());
	InitPadLayout(&touch, orientation, bounds.w, bounds.h);
	root_ = CreatePadLayout(touch, bounds.w, bounds.h, &pauseTrigger_, &g_controlMapper);
	GamepadTouch();  // Show the controls when the game starts; they fade out later if set to.
}

void RetroScreen::update() {
	UIScreen::update();
	if (!loaded_ || exitRequested_) {
		if (!closing_ && screenManager()->topScreen() == this) {
			closing_ = true;
			core_.Unload();
			loaded_ = false;
			screenManager()->finishDialog(this, exitRequested_ ? DR_OK : DR_CANCEL);
		}
		return;
	}
	if (pauseTrigger_) {
		pauseTrigger_ = false;
		OpenPauseMenu();
		return;
	}
	if (paused_)
		return;

	// Run as many frames as the core's own frame rate asks for (at most a few, so a hiccup doesn't snowball).
	const double now = time_now_d();
	const double frameTime = 1.0 / core_.Fps();
	if (now - nextFrameTime_ > 0.25)
		nextFrameTime_ = now;
	int frames = 0;
	while (nextFrameTime_ <= now && frames < 4) {
		UpdateJoypad();
		core_.RunFrame();
		nextFrameTime_ += frameTime;
		frames++;
	}
	if (frames > 0 && core_.TakeFrame(&frame_, &frameW_, &frameH_))
		frameDirty_ = true;
}

void RetroScreen::UpdateJoypad() {
	// PSP button -> RetroPad. Nintendo-style layout: A is the right face button, B the bottom one.
	uint32_t pad = 0;
	auto map = [&](uint32_t pspBit, int retroId) {
		if (pspButtons_ & pspBit)
			pad |= 1u << retroId;
	};
	map(CTRL_CIRCLE, RETRO_DEVICE_ID_JOYPAD_A);
	map(CTRL_CROSS, RETRO_DEVICE_ID_JOYPAD_B);
	map(CTRL_TRIANGLE, RETRO_DEVICE_ID_JOYPAD_X);
	map(CTRL_SQUARE, RETRO_DEVICE_ID_JOYPAD_Y);
	map(CTRL_LTRIGGER, RETRO_DEVICE_ID_JOYPAD_L);
	map(CTRL_RTRIGGER, RETRO_DEVICE_ID_JOYPAD_R);
	map(CTRL_START, RETRO_DEVICE_ID_JOYPAD_START);
	map(CTRL_SELECT, RETRO_DEVICE_ID_JOYPAD_SELECT);
	map(CTRL_UP, RETRO_DEVICE_ID_JOYPAD_UP);
	map(CTRL_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN);
	map(CTRL_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT);
	map(CTRL_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
	// The analog stick also works as a d-pad.
	const float dead = 0.5f;
	// The stick, from a gamepad, the keyboard or the touch controls.
	float stickX_, stickY_;
	GamepadGetRetroAnalog(0, &stickX_, &stickY_);
	if (stickY_ > dead) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
	if (stickY_ < -dead) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (stickX_ < -dead) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (stickX_ > dead) pad |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	core_.SetJoypad(pad);
}

void RetroScreen::UpdatePSPButtons(uint32_t buttonMask, uint32_t changedMask) {
	pspButtons_ = buttonMask;
}

void RetroScreen::SetPSPAnalog(int rotation, int stick, float x, float y) {
	GamepadSetRetroAnalog(stick, x, y);
}

void RetroScreen::OnVKey(VirtKey vkey, bool down) {
	if (!down)
		return;
	switch (vkey) {
	case VIRTKEY_PAUSE:
		pauseFromButton_ = true;
		pauseTrigger_ = true;
		break;
	case VIRTKEY_SAVE_STATE:
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, core_.SaveState(0) ? "Estado guardado" : "No se pudo guardar el estado", 2.0f);
		break;
	case VIRTKEY_LOAD_STATE:
		g_OSD.Show(OSDType::MESSAGE_INFO, core_.LoadState(0) ? "Estado cargado" : "No hay estado guardado", 2.0f);
		break;
	default:
		break;
	}
}

void RetroScreen::touch(const TouchInput &input) {
	// Touching the screen brings back the touch controls after they faded out.
	if (input.flags & TouchInputFlags::DOWN)
		GamepadTouch();
	UIScreen::touch(input);
}

bool RetroScreen::UnsyncKey(const KeyInput &key) {
	// Called from the input thread: only set flags here, update() acts on them.
	if ((key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		pauseFromButton_ = true;
		pauseTrigger_ = true;
		return true;
	}
	return g_controlMapper.Key(key, &pauseTrigger_);
}

void RetroScreen::UnsyncAxis(const AxisInput *axes, size_t count) {
	g_controlMapper.Axis(axes, count);
}

namespace {

static int s_retroSlot = 0;

// Tryled pause for non-PSP games: the frozen game on the left with the save slot picker,
// the actions on the right. Same layout as the approved preview.
class RetroPauseScreen : public UIDialogScreen {
public:
	RetroPauseScreen(const std::string &title, Draw::Texture *frame, float aspect, RetroCore *core, std::function<void(int)> onAction)
		: title_(title), frame_(frame), aspect_(aspect), core_(core), onAction_(onAction) {}
	const char *tag() const override { return "RetroPause"; }

	enum Action { CONTINUE = 0, SAVE, LOAD, RESET, EXIT };

	void touch(const TouchInput &input) override {
		// A touch switches from button navigation to touch: do it before the views see the touch,
		// otherwise the first tap after opening the menu with a button is lost.
		if (input.flags & TouchInputFlags::DOWN)
			UI::EnableFocusMovement(false);
		UIDialogScreen::touch(input);
	}

protected:
	void CreateViews() override {
		using namespace UI;
		LinearLayout *root = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		root->SetSpacing(20.0f);
		root_ = root;

		// Left: the game picture and the slots.
		LinearLayout *left = root->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(0.0f, FILL_PARENT, 1.0f, Margins(24, 24, 0, 24))));
		left->Add(new FrameView(this, new LinearLayoutParams(FILL_PARENT, 0.0f, 1.0f)));
		left->Add(new TextView("Ranura de guardado", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(4, 10, 4, 2))));
		LinearLayout *slots = left->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		slots->SetSpacing(6.0f);
		for (int i = 0; i < 5; i++) {
			std::string label = StringFromFormat("%d", i + 1);
			if (core_->HasState(i))
				label += " *";
			Choice *slot = slots->Add(new Choice(label, new LinearLayoutParams(0.0f, 48.0f, 1.0f)));
			slot->SetCentered(true);
			slot->SetSelectedIndicator(false);
			if (i == s_retroSlot)
				slot->SetSelectedIndicator(true);
			slot->OnClick.Add([this, i](EventParams &) {
				s_retroSlot = i;
				RecreateViews();
			});
		}

		// Right: name and actions.
		ScrollView *scroll = root->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(320.0f, FILL_PARENT, Margins(0, 16, 20, 12))));
		LinearLayout *menu = scroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		menu->SetSpacing(2.0f);
		TextView *titleView = menu->Add(new TextView(title_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 0))));
		titleView->SetBig(true);
		menu->Add(new TextView("En pausa", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 10))));
		Choice *cont = menu->Add(new TryledPrimaryChoice("Continuar", ImageID("I_PLAY"), new LinearLayoutParams(FILL_PARENT, 42.0f)));
		cont->OnClick.Add([this](EventParams &) { Done(CONTINUE); });
		menu->Add(new Spacer(8.0f));
		menu->Add(new Choice(StringFromFormat("Guardar (ranura %d)", s_retroSlot + 1), ImageID("I_FILE_SAVE"), new LinearLayoutParams(FILL_PARENT, 42.0f)))->OnClick.Add([this](EventParams &) { Done(SAVE); });
		Choice *load = menu->Add(new Choice(StringFromFormat("Cargar (ranura %d)", s_retroSlot + 1), ImageID("I_FOLDER_OPEN"), new LinearLayoutParams(FILL_PARENT, 42.0f)));
		load->SetEnabled(core_->HasState(s_retroSlot));
		load->OnClick.Add([this](EventParams &) { Done(LOAD); });
		menu->Add(new Choice("Reiniciar", ImageID("I_ROTATE_LEFT"), new LinearLayoutParams(FILL_PARENT, 42.0f)))->OnClick.Add([this](EventParams &) { Done(RESET); });
		menu->Add(new Choice("Editar controles", ImageID("I_CONTROLLER"), new LinearLayoutParams(FILL_PARENT, 42.0f)))->OnClick.Add([this](EventParams &) {
			screenManager()->push(new TouchControlLayoutScreen(Path()));
		});
		menu->Add(new Choice("Ajustes", ImageID("I_GEAR"), new LinearLayoutParams(FILL_PARENT, 42.0f)))->OnClick.Add([this](EventParams &) {
			screenManager()->push(new GameSettingsScreen(Path()));
		});
		menu->Add(new Spacer(8.0f));
		menu->Add(new Choice("Salir al inicio", ImageID("I_EXIT"), new LinearLayoutParams(FILL_PARENT, 42.0f)))->OnClick.Add([this](EventParams &) { Done(EXIT); });
		root_->SetDefaultFocusView(cont);
	}

	void DrawBackground(UIContext &dc) override {
		// The frozen game, filling the screen, darkened.
		const Bounds &b = dc.GetBounds();
		dc.FillRect(UI::Drawable(dc.GetTheme().backgroundColor | 0xFF000000), b);
		if (frame_) {
			float u1, v1, u2, v2;
			TryledDraw::CoverUV(aspect_, b.w / b.h, &u1, &v1, &u2, &v2);
			TryledDraw::TextureRoundRect(dc, frame_, b, 0.0f, 0xFF606060, u1, v1, u2, v2);
		}
		dc.FillRect(UI::Drawable(colorAlpha(dc.GetTheme().backgroundColor & 0xFFFFFF, 0.72f)), b);
	}

private:
	class FrameView : public UI::InertView {
	public:
		FrameView(RetroPauseScreen *owner, UI::LayoutParams *lp) : UI::InertView(lp), owner_(owner) {}
		void Draw(UIContext &dc) override {
			if (!owner_->frame_)
				return;
			float w = bounds_.w, h = bounds_.w / owner_->aspect_;
			if (h > bounds_.h) {
				h = bounds_.h;
				w = h * owner_->aspect_;
			}
			const Bounds pic = Bounds::FromCenterWH(bounds_.centerX(), bounds_.centerY(), w, h);
			const float r = std::clamp(g_Config.iTryledCornerRadius, 0, 30);
			TryledDraw::Glow(dc, pic, r, 18.0f, 0xB0000000);
			TryledDraw::TextureRoundRect(dc, owner_->frame_, pic, r, 0xFFFFFFFF);
			TryledDraw::StrokeRoundRect(dc, pic, r, 1.0f, colorAlpha(dc.GetTheme().infoStyle.fgColor & 0xFFFFFF, 0.15f));
		}
	private:
		RetroPauseScreen *owner_;
	};

	void Done(int action) {
		if (onAction_)
			onAction_(action);
		TriggerFinish(DR_OK);
	}

	std::string title_;
	Draw::Texture *frame_;
	float aspect_;
	RetroCore *core_;
	std::function<void(int)> onAction_;
};

}  // namespace

void RetroScreen::OpenPauseMenu() {
	if (paused_)
		return;
	paused_ = true;
	core_.SaveSRAM();
	// With a gamepad or keyboard, every press should move through the menu right away.
	UI::EnableFocusMovement(pauseFromButton_);
	pauseFromButton_ = false;
	auto *menu = new RetroPauseScreen(game_.WithReplacedExtension("").GetFilename(), textures_[texIndex_], core_.AspectRatio(), &core_, [this](int action) {
		switch (action) {
		case RetroPauseScreen::SAVE:
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, core_.SaveState(s_retroSlot) ? StringFromFormat("Estado guardado en la ranura %d", s_retroSlot + 1) : std::string("No se pudo guardar el estado"), 2.0f);
			break;
		case RetroPauseScreen::LOAD:
			g_OSD.Show(OSDType::MESSAGE_INFO, core_.LoadState(s_retroSlot) ? StringFromFormat("Estado de la ranura %d cargado", s_retroSlot + 1) : std::string("No hay estado guardado"), 2.0f);
			break;
		case RetroPauseScreen::RESET:
			core_.Reset();
			break;
		case RetroPauseScreen::EXIT:
			// The menu is still on top of us here; close this screen from update() once it's gone.
			exitRequested_ = true;
			break;
		default:
			break;
		}
	});
	screenManager()->push(menu);
}

void RetroScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Closing the pause menu with back/cancel resumes the game.
	if (paused_) {
		paused_ = false;
		nextFrameTime_ = time_now_d();
	}
}

void RetroScreen::UploadFrame(Draw::DrawContext *draw) {
	if (!frameDirty_ || frameW_ <= 0 || frameH_ <= 0)
		return;
	frameDirty_ = false;
	if (frameW_ != texW_ || frameH_ != texH_) {
		for (auto *&tex : textures_) {
			if (tex) {
				tex->Release();
				tex = nullptr;
			}
		}
		texW_ = frameW_;
		texH_ = frameH_;
	}
	// Rotate through a few textures so we never overwrite one the GPU may still be reading.
	texIndex_ = (texIndex_ + 1) % 4;
	Draw::Texture *&tex = textures_[texIndex_];
	const uint8_t *data = (const uint8_t *)frame_.data();
	if (!tex) {
		Draw::TextureDesc desc{};
		desc.type = Draw::TextureType::LINEAR2D;
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
		desc.width = texW_;
		desc.height = texH_;
		desc.depth = 1;
		desc.mipLevels = 1;
		desc.generateMips = false;
		desc.swizzle = Draw::TextureSwizzle::DEFAULT;
		desc.tag = "RetroFrame";
		desc.initData.push_back(data);
		tex = draw->CreateTexture(desc);
	} else {
		draw->UpdateTextureLevels(tex, &data, nullptr, 1);
	}
}

void RetroScreen::DrawBackground(UIContext &dc) {
	Draw::DrawContext *draw = dc.GetDrawContext();
	const Bounds &b = dc.GetBounds();
	dc.FillRect(UI::Drawable(0xFF000000), b);
	if (!loaded_)
		return;

	UploadFrame(draw);
	Draw::Texture *tex = textures_[texIndex_];
	if (!tex)
		return;

	// Fit the picture to the screen, keeping the console's aspect ratio.
	const float aspect = core_.AspectRatio();
	float w = b.w, h = b.w / aspect;
	if (h > b.h) {
		h = b.h;
		w = b.h * aspect;
	}
	const Bounds dst = Bounds::FromCenterWH(b.centerX(), b.centerY(), w, h);

	if (!nearest_) {
		// Sharp pixels: these consoles have very low resolutions.
		nearest_ = draw->CreateSamplerState({ Draw::TextureFilter::NEAREST, Draw::TextureFilter::NEAREST, Draw::TextureFilter::NEAREST, 0.0f,
			Draw::TextureAddressMode::CLAMP_TO_EDGE, Draw::TextureAddressMode::CLAMP_TO_EDGE, Draw::TextureAddressMode::CLAMP_TO_EDGE });
	}
	dc.Flush();
	draw->BindSamplerStates(0, 1, &nearest_);
	draw->BindTexture(0, tex);
	dc.Draw()->DrawTexRect(dst, 0, 0, 1, 1, 0xFFFFFFFF);
	dc.Flush();
	dc.Begin();  // Back to the UI texture and sampler.
}
