#pragma once

// Tryled: a small libretro frontend, so Tryled can run emulators for other consoles
// (GBA, SNES...) built as libretro cores, next to PPSSPP's own PSP emulation.
// The core is a shared library packaged in the APK and loaded with dlopen().

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Common/File/Path.h"

struct RetroCoreInfo {
	const char *console;   // "GBA", "SNES"...
	const char *library;   // Shared library name, e.g. "libmgba_libretro.so".
	const char *name;      // Human readable emulator name.
};

// Id for the per-game settings of a game of another console (PSP games use their disc id).
std::string TryledRetroGameId(const Path &game);

// Returns the core to use for a console, or nullptr if Tryled has none yet.
const RetroCoreInfo *RetroCoreForConsole(const std::string &console);

class RetroCore {
public:
	~RetroCore();

	bool Load(const RetroCoreInfo &info, const Path &game, std::string *error);
	void Unload();
	bool IsLoaded() const { return handle_ != nullptr; }

	// Runs one frame of emulation.
	void RunFrame();
	void Reset();

	double Fps() const { return fps_; }
	double SampleRate() const { return sampleRate_; }
	float AspectRatio() const { return aspect_; }

	// The latest frame, converted to RGBA8888. Returns false if there is no new frame since the last call.
	bool TakeFrame(std::vector<uint32_t> *rgba, int *w, int *h);

	// Joypad state, as a RETRO_DEVICE_ID_JOYPAD_* bitmask.
	void SetJoypad(uint32_t mask) { joypad_ = mask; }

	// Mixes (adds) the core's audio into a stereo int16 buffer at the given output rate.
	void MixAudio(int16_t *out, int frames, int outRate);

	bool SaveState(int slot);
	bool HasState(int slot) const;
	bool LoadState(int slot);
	void SaveSRAM();

	// Mixes the running core's audio, if any (called from the audio thread).
	static void MixActive(int16_t *out, int frames, int outRate);

	// True while a game of another console is running (the PSP core is then powered down).
	static bool AnyActive();

private:
	void LoadSRAM();
	Path StatePath(int slot) const;
	Path SRAMPath() const;

	// Callbacks from the core (static trampolines into the active instance).
	static bool Environment(unsigned cmd, void *data);
	static void VideoRefresh(const void *data, unsigned width, unsigned height, size_t pitch);
	static void AudioSample(int16_t left, int16_t right);
	static size_t AudioSampleBatch(const int16_t *data, size_t frames);
	static void InputPoll();
	static int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id);

	void *handle_ = nullptr;
	RetroCoreInfo info_{};
	Path game_;
	std::string gameData_;

	int pixelFormat_ = 0;  // RETRO_PIXEL_FORMAT_*
	double fps_ = 60.0;
	double sampleRate_ = 44100.0;
	float aspect_ = 4.0f / 3.0f;

	std::mutex frameLock_;
	std::vector<uint32_t> frame_;
	int frameW_ = 0, frameH_ = 0;
	bool frameNew_ = false;

	std::mutex audioLock_;
	std::vector<int16_t> audio_;  // Interleaved stereo ring buffer.
	size_t audioRead_ = 0, audioWrite_ = 0, audioCount_ = 0;
	double resamplePos_ = 0.0;

	uint32_t joypad_ = 0;

	std::string systemDir_, saveDir_;

	// Resolved core functions.
	struct Api;
	Api *api_ = nullptr;
};
