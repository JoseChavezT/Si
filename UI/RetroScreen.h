#pragma once

// Tryled: game screen for non-PSP consoles, running a libretro core (see RetroCore.h).
// Uses PPSSPP's touch controls and gamepad mapping: the PSP buttons are translated to a RetroPad.

#include <vector>

#include "Common/UI/UIScreen.h"
#include "Core/ControlMapper.h"
#include "UI/RetroCore.h"

namespace Draw {
class Texture;
class SamplerState;
}

class RetroScreen : public UIScreen, public ControlListener {
public:
	RetroScreen(const Path &game, const RetroCoreInfo &core);
	~RetroScreen();

	const char *tag() const override { return "Retro"; }

	void update() override;
	// Game input goes straight to the control mapper, like in EmuScreen (otherwise the UI
	// would swallow the first key press to turn on menu navigation).
	bool UnsyncKey(const KeyInput &key) override;
	void UnsyncAxis(const AxisInput *axes, size_t count) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	// ControlListener
	void OnVKey(VirtKey vkey, bool down) override;
	void UpdatePSPButtons(uint32_t buttonMask, uint32_t changedMask) override;
	void SetPSPAnalog(int rotation, int stick, float x, float y) override;

protected:
	void CreateViews() override;
	void DrawBackground(UIContext &dc) override;

private:
	void OpenPauseMenu();
	void UpdateJoypad();
	void UploadFrame(Draw::DrawContext *draw);

	Path game_;
	RetroCoreInfo info_;
	RetroCore core_;
	std::string error_;
	bool loaded_ = false;

	double nextFrameTime_ = 0.0;
	bool paused_ = false;
	bool pauseTrigger_ = false;
	bool pauseFromButton_ = false;  // Opened with keyboard/gamepad: start with button navigation on.
	bool exitRequested_ = false;
	bool closing_ = false;  // Set from the pause menu; acted on once the menu has closed.

	uint32_t pspButtons_ = 0;
	float stickX_ = 0.0f, stickY_ = 0.0f;

	std::vector<uint32_t> frame_;
	int frameW_ = 0, frameH_ = 0;
	bool frameDirty_ = false;
	Draw::Texture *textures_[4]{};
	int texW_ = 0, texH_ = 0;
	int texIndex_ = 0;
	Draw::SamplerState *nearest_ = nullptr;
};
