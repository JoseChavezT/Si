// Tryled: minimal libretro frontend. See RetroCore.h.

#include "ppsspp_config.h"

#include <cstring>
#include <cstdarg>
#include <algorithm>

#if !PPSSPP_PLATFORM(WINDOWS)
#include <dlfcn.h>
#endif

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Core/Util/PathUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "UI/libretro.h"
#include "UI/RetroCore.h"

static const RetroCoreInfo g_retroCores[] = {
	{ "GBA", "libmgba_libretro.so", "mGBA" },
	{ "GB", "libmgba_libretro.so", "mGBA" },
	{ "NES", "libfceumm_libretro.so", "FCEUmm" },
	{ "SNES", "libsnes9x_libretro.so", "Snes9x" },
	{ "PS1", "libpcsx_rearmed_libretro.so", "PCSX ReARMed" },
	{ "Mega Drive", "libgenesis_plus_gx_libretro.so", "Genesis Plus GX" },
	{ "Master System", "libgenesis_plus_gx_libretro.so", "Genesis Plus GX" },
	{ "Game Gear", "libgenesis_plus_gx_libretro.so", "Genesis Plus GX" },
};

const RetroCoreInfo *RetroCoreForConsole(const std::string &console) {
	for (const auto &core : g_retroCores) {
		if (console == core.console)
			return &core;
	}
	return nullptr;
}

struct RetroCore::Api {
	void (*retro_set_environment)(retro_environment_t);
	void (*retro_set_video_refresh)(retro_video_refresh_t);
	void (*retro_set_audio_sample)(retro_audio_sample_t);
	void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*retro_set_input_poll)(retro_input_poll_t);
	void (*retro_set_input_state)(retro_input_state_t);
	void (*retro_init)(void);
	void (*retro_deinit)(void);
	void (*retro_get_system_info)(struct retro_system_info *);
	void (*retro_get_system_av_info)(struct retro_system_av_info *);
	void (*retro_set_controller_port_device)(unsigned, unsigned);
	bool (*retro_load_game)(const struct retro_game_info *);
	void (*retro_unload_game)(void);
	void (*retro_run)(void);
	void (*retro_reset)(void);
	size_t (*retro_serialize_size)(void);
	bool (*retro_serialize)(void *, size_t);
	bool (*retro_unserialize)(const void *, size_t);
	void *(*retro_get_memory_data)(unsigned);
	size_t (*retro_get_memory_size)(unsigned);
};

static RetroCore *g_activeCore = nullptr;
// Guards g_activeCore against the audio thread while a core is loaded or unloaded.
static std::mutex g_activeLock;

void RetroCore::MixActive(int16_t *out, int frames, int outRate) {
	std::lock_guard<std::mutex> guard(g_activeLock);
	if (g_activeCore && g_activeCore->handle_)
		g_activeCore->MixAudio(out, frames, outRate);
}

RetroCore::~RetroCore() {
	Unload();
}

static void RetroLog(enum retro_log_level level, const char *fmt, ...) {
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (level >= RETRO_LOG_WARN)
		WARN_LOG(Log::System, "[core] %s", buf);
	else
		INFO_LOG(Log::System, "[core] %s", buf);
}

bool RetroCore::Load(const RetroCoreInfo &info, const Path &game, std::string *error) {
	Unload();
	info_ = info;
	game_ = game;

#if PPSSPP_PLATFORM(WINDOWS)
	*error = "No disponible en Windows";
	return false;
#else
	handle_ = dlopen(info.library, RTLD_NOW | RTLD_LOCAL);
	if (!handle_) {
		*error = std::string("No se pudo cargar el emulador ") + info.name + ": " + dlerror();
		return false;
	}
	api_ = new Api();
	bool ok = true;
#define LOAD_SYM(name) do { *(void **)&api_->name = dlsym(handle_, #name); if (!api_->name) { ok = false; ERROR_LOG(Log::System, "Missing libretro symbol %s", #name); } } while (0)
	LOAD_SYM(retro_set_environment);
	LOAD_SYM(retro_set_video_refresh);
	LOAD_SYM(retro_set_audio_sample);
	LOAD_SYM(retro_set_audio_sample_batch);
	LOAD_SYM(retro_set_input_poll);
	LOAD_SYM(retro_set_input_state);
	LOAD_SYM(retro_init);
	LOAD_SYM(retro_deinit);
	LOAD_SYM(retro_get_system_info);
	LOAD_SYM(retro_get_system_av_info);
	LOAD_SYM(retro_set_controller_port_device);
	LOAD_SYM(retro_load_game);
	LOAD_SYM(retro_unload_game);
	LOAD_SYM(retro_run);
	LOAD_SYM(retro_reset);
	LOAD_SYM(retro_serialize_size);
	LOAD_SYM(retro_serialize);
	LOAD_SYM(retro_unserialize);
	LOAD_SYM(retro_get_memory_data);
	LOAD_SYM(retro_get_memory_size);
#undef LOAD_SYM
	if (!ok) {
		*error = std::string("El emulador ") + info.name + " está incompleto.";
		dlclose(handle_);
		handle_ = nullptr;
		delete api_;
		api_ = nullptr;
		return false;
	}

	// Folders: BIOS files go in PSP/TRYLED/BIOS, saves and states in PSP/TRYLED/SAVES and STATES.
	Path base = GetSysDirectory(DIRECTORY_SYSTEM) / "TRYLED";
	systemDir_ = (base / "BIOS").ToString();
	saveDir_ = (base / "SAVES").ToString();
	File::CreateFullPath(base / "BIOS");
	File::CreateFullPath(base / "SAVES");
	File::CreateFullPath(base / "STATES");

	{
		std::lock_guard<std::mutex> guard(g_activeLock);
		g_activeCore = this;
	}
	api_->retro_set_environment(&RetroCore::Environment);
	api_->retro_init();
	api_->retro_set_video_refresh(&RetroCore::VideoRefresh);
	api_->retro_set_audio_sample(&RetroCore::AudioSample);
	api_->retro_set_audio_sample_batch(&RetroCore::AudioSampleBatch);
	api_->retro_set_input_poll(&RetroCore::InputPoll);
	api_->retro_set_input_state(&RetroCore::InputState);

	retro_system_info sysInfo{};
	api_->retro_get_system_info(&sysInfo);

	// Content may be behind an Android content:// URI, so we always read it ourselves when the core allows it.
	retro_game_info gameInfo{};
	std::string pathStr = game.ToString();
	if (sysInfo.need_fullpath && game.Type() == PathType::CONTENT_URI) {
		// This core opens the file itself, and can't open Android content:// URIs.
		// Give it a copy in the app's cache folder.
		Path cacheDir = GetSysDirectory(DIRECTORY_APP_CACHE) / "TRYLED";
		File::CreateFullPath(cacheDir);
		Path copy = cacheDir / game.GetFilename();
		File::FileInfo src, dst;
		if (!File::GetFileInfo(copy, &dst) || !File::GetFileInfo(game, &src) || src.size != dst.size) {
			if (!File::Copy(game, copy)) {
				*error = "No se pudo preparar el juego.";
				Unload();
				return false;
			}
		}
		pathStr = copy.ToString();
	}
	gameInfo.path = pathStr.c_str();
	if (!sysInfo.need_fullpath) {
		if (!File::ReadBinaryFileToString(game, &gameData_)) {
			*error = "No se pudo leer el juego.";
			Unload();
			return false;
		}
		gameInfo.data = gameData_.data();
		gameInfo.size = gameData_.size();
	}
	if (!api_->retro_load_game(&gameInfo)) {
		*error = std::string(info.name) + " no pudo abrir este juego.";
		Unload();
		return false;
	}
	api_->retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

	retro_system_av_info av{};
	api_->retro_get_system_av_info(&av);
	fps_ = av.timing.fps > 1.0 ? av.timing.fps : 60.0;
	sampleRate_ = av.timing.sample_rate > 1000.0 ? av.timing.sample_rate : 44100.0;
	aspect_ = av.geometry.aspect_ratio > 0.0f ? av.geometry.aspect_ratio : (float)av.geometry.base_width / (float)std::max(1u, av.geometry.base_height);

	{
		std::lock_guard<std::mutex> guard(audioLock_);
		audio_.assign(2 * (size_t)sampleRate_ / 2, 0);  // Half a second of stereo audio.
		audioRead_ = audioWrite_ = audioCount_ = 0;
		resamplePos_ = 0.0;
	}
	LoadSRAM();
	INFO_LOG(Log::System, "Loaded %s with %s: %.2f fps, %.0f Hz", game.c_str(), info.name, fps_, sampleRate_);
	return true;
#endif
}

void RetroCore::Unload() {
	if (!handle_)
		return;
	{
		// Stop the audio thread from touching this core from now on.
		std::lock_guard<std::mutex> guard(g_activeLock);
		if (g_activeCore == this)
			g_activeCore = nullptr;
	}
#if !PPSSPP_PLATFORM(WINDOWS)
	if (api_) {
		SaveSRAM();
		api_->retro_unload_game();
		api_->retro_deinit();
	}
	dlclose(handle_);
#endif
	handle_ = nullptr;
	delete api_;
	api_ = nullptr;
	gameData_.clear();
}

void RetroCore::RunFrame() {
	if (api_)
		api_->retro_run();
}

void RetroCore::Reset() {
	if (api_)
		api_->retro_reset();
}

bool RetroCore::TakeFrame(std::vector<uint32_t> *rgba, int *w, int *h) {
	std::lock_guard<std::mutex> guard(frameLock_);
	if (!frameNew_)
		return false;
	rgba->swap(frame_);
	*w = frameW_;
	*h = frameH_;
	frameNew_ = false;
	return true;
}

void RetroCore::MixAudio(int16_t *out, int frames, int outRate) {
	std::lock_guard<std::mutex> guard(audioLock_);
	if (audio_.empty())
		return;
	const size_t cap = audio_.size() / 2;
	const double step = sampleRate_ / (double)outRate;
	for (int i = 0; i < frames; i++) {
		if (audioCount_ < 2)
			break;  // Underrun: leave silence.
		const size_t i0 = audioRead_;
		const size_t i1 = (audioRead_ + 1) % cap;
		const float t = (float)resamplePos_;
		const int l = (int)(audio_[i0 * 2] * (1.0f - t) + audio_[i1 * 2] * t);
		const int r = (int)(audio_[i0 * 2 + 1] * (1.0f - t) + audio_[i1 * 2 + 1] * t);
		out[i * 2] = (int16_t)std::clamp(out[i * 2] + l, -32768, 32767);
		out[i * 2 + 1] = (int16_t)std::clamp(out[i * 2 + 1] + r, -32768, 32767);
		resamplePos_ += step;
		while (resamplePos_ >= 1.0 && audioCount_ > 1) {
			resamplePos_ -= 1.0;
			audioRead_ = (audioRead_ + 1) % cap;
			audioCount_--;
		}
	}
}

Path RetroCore::SRAMPath() const {
	return Path(saveDir_) / (game_.WithReplacedExtension("").GetFilename() + ".srm");
}

Path RetroCore::StatePath(int slot) const {
	return GetSysDirectory(DIRECTORY_SYSTEM) / "TRYLED" / "STATES" / StringFromFormat("%s.state%d", game_.WithReplacedExtension("").GetFilename().c_str(), slot);
}

void RetroCore::LoadSRAM() {
	void *mem = api_->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = api_->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (!mem || !size)
		return;
	std::string data;
	if (File::ReadBinaryFileToString(SRAMPath(), &data))
		memcpy(mem, data.data(), std::min(size, data.size()));
}

void RetroCore::SaveSRAM() {
	if (!api_)
		return;
	void *mem = api_->retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = api_->retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (mem && size)
		File::WriteDataToFile(false, mem, size, SRAMPath());
}

bool RetroCore::SaveState(int slot) {
	if (!api_)
		return false;
	size_t size = api_->retro_serialize_size();
	if (!size)
		return false;
	std::string data(size, '\0');
	if (!api_->retro_serialize(&data[0], size))
		return false;
	return File::WriteDataToFile(false, data.data(), data.size(), StatePath(slot));
}

bool RetroCore::LoadState(int slot) {
	if (!api_)
		return false;
	std::string data;
	if (!File::ReadBinaryFileToString(StatePath(slot), &data))
		return false;
	return api_->retro_unserialize(data.data(), data.size());
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

bool RetroCore::Environment(unsigned cmd, void *data) {
	RetroCore *core = g_activeCore;
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		const auto fmt = *(const enum retro_pixel_format *)data;
		if (fmt != RETRO_PIXEL_FORMAT_RGB565 && fmt != RETRO_PIXEL_FORMAT_XRGB8888 && fmt != RETRO_PIXEL_FORMAT_0RGB1555)
			return false;
		if (core)
			core->pixelFormat_ = (int)fmt;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = core ? core->systemDir_.c_str() : nullptr;
		return core != nullptr;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = core ? core->saveDir_.c_str() : nullptr;
		return core != nullptr;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((retro_log_callback *)data)->log = &RetroLog;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
		// We use every core option's default value.
		((retro_variable *)data)->value = nullptr;
		return false;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = false;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 0;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
	case RETRO_ENVIRONMENT_SET_GEOMETRY: {
		if (!core)
			return false;
		if (cmd == RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO) {
			const auto *av = (const retro_system_av_info *)data;
			core->fps_ = av->timing.fps > 1.0 ? av->timing.fps : core->fps_;
			if (av->geometry.aspect_ratio > 0.0f)
				core->aspect_ = av->geometry.aspect_ratio;
		} else {
			const auto *geom = (const retro_game_geometry *)data;
			if (geom->aspect_ratio > 0.0f)
				core->aspect_ = geom->aspect_ratio;
		}
		return true;
	}
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE:
		*(unsigned *)data = RETRO_LANGUAGE_SPANISH;
		return true;
	default:
		return false;
	}
}

void RetroCore::VideoRefresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	RetroCore *core = g_activeCore;
	if (!core || !data)
		return;  // NULL data means "duplicate the previous frame".
	std::lock_guard<std::mutex> guard(core->frameLock_);
	core->frame_.resize((size_t)width * height);
	uint32_t *dst = core->frame_.data();
	for (unsigned y = 0; y < height; y++) {
		const uint8_t *row = (const uint8_t *)data + y * pitch;
		if (core->pixelFormat_ == RETRO_PIXEL_FORMAT_XRGB8888) {
			const uint32_t *src = (const uint32_t *)row;
			for (unsigned x = 0; x < width; x++) {
				const uint32_t c = src[x];
				// XRGB8888 -> RGBA8888 (in memory: R, G, B, A)
				dst[x] = 0xFF000000 | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
			}
		} else if (core->pixelFormat_ == RETRO_PIXEL_FORMAT_RGB565) {
			const uint16_t *src = (const uint16_t *)row;
			for (unsigned x = 0; x < width; x++) {
				const uint16_t c = src[x];
				const uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
				const uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
				const uint32_t b = (c & 0x1F) * 255 / 31;
				dst[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
			}
		} else {  // 0RGB1555
			const uint16_t *src = (const uint16_t *)row;
			for (unsigned x = 0; x < width; x++) {
				const uint16_t c = src[x];
				const uint32_t r = ((c >> 10) & 0x1F) * 255 / 31;
				const uint32_t g = ((c >> 5) & 0x1F) * 255 / 31;
				const uint32_t b = (c & 0x1F) * 255 / 31;
				dst[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
			}
		}
		dst += width;
	}
	core->frameW_ = (int)width;
	core->frameH_ = (int)height;
	core->frameNew_ = true;
}

void RetroCore::AudioSample(int16_t left, int16_t right) {
	int16_t frame[2] = { left, right };
	AudioSampleBatch(frame, 1);
}

size_t RetroCore::AudioSampleBatch(const int16_t *data, size_t frames) {
	RetroCore *core = g_activeCore;
	if (!core)
		return frames;
	std::lock_guard<std::mutex> guard(core->audioLock_);
	const size_t cap = core->audio_.size() / 2;
	if (!cap)
		return frames;
	for (size_t i = 0; i < frames; i++) {
		if (core->audioCount_ >= cap) {
			// Overrun: drop the oldest sample.
			core->audioRead_ = (core->audioRead_ + 1) % cap;
			core->audioCount_--;
		}
		core->audio_[core->audioWrite_ * 2] = data[i * 2];
		core->audio_[core->audioWrite_ * 2 + 1] = data[i * 2 + 1];
		core->audioWrite_ = (core->audioWrite_ + 1) % cap;
		core->audioCount_++;
	}
	return frames;
}

void RetroCore::InputPoll() {}

int16_t RetroCore::InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	RetroCore *core = g_activeCore;
	if (!core || port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD)
		return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return (int16_t)core->joypad_;
	return (core->joypad_ >> id) & 1;
}
