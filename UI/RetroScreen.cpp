// Tryled: game screen for non-PSP consoles. See RetroScreen.h.

#include <algorithm>

#include "Common/GPU/thin3d.h"
#include "Common/UI/Context.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ViewGroup.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"
#include "UI/GamepadEmu.h"
#include "UI/RetroScreen.h"
#include "UI/libretro.h"

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
}

void RetroScreen::update() {
	UIScreen::update();
	if (!loaded_) {
		screenManager()->finishDialog(this, DR_CANCEL);
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
	if (stick == 0) {
		stickX_ = x;
		stickY_ = y;
	}
}

void RetroScreen::OnVKey(VirtKey vkey, bool down) {
	if (!down)
		return;
	switch (vkey) {
	case VIRTKEY_PAUSE:
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

bool RetroScreen::key(const KeyInput &key) {
	if ((key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		OpenPauseMenu();
		return true;
	}
	bool pause = false;
	const bool handled = g_controlMapper.Key(key, &pause);
	if (pause)
		pauseTrigger_ = true;
	return handled || UIScreen::key(key);
}

void RetroScreen::axis(const AxisInput &axis) {
	g_controlMapper.Axis(&axis, 1);
}

void RetroScreen::OpenPauseMenu() {
	if (paused_)
		return;
	paused_ = true;
	core_.SaveSRAM();
	std::vector<std::string> items = { "Continuar", "Guardar estado", "Cargar estado", "Reiniciar", "Salir al inicio" };
	auto *menu = new UI::ListPopupScreen(game_.WithReplacedExtension("").GetFilename(), items, 0, [this](int choice) {
		paused_ = false;
		nextFrameTime_ = time_now_d();
		switch (choice) {
		case 1:
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, core_.SaveState(0) ? "Estado guardado" : "No se pudo guardar el estado", 2.0f);
			break;
		case 2:
			g_OSD.Show(OSDType::MESSAGE_INFO, core_.LoadState(0) ? "Estado cargado" : "No hay estado guardado", 2.0f);
			break;
		case 3:
			core_.Reset();
			break;
		case 4:
			core_.Unload();
			screenManager()->finishDialog(this, DR_OK);
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
