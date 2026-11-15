/* s3air-driver.cpp - Sonic 3 A.I.R. as a machine that is stepped.
 *
 * Upstream's main() makes the engine (EngineMain, with S3AIR's
 * EngineDelegate), starts it, and enters rmxmedia's loop, which reads
 * events, updates the application (the menus, the simulation that runs the
 * game's scripts against the ROM, the sound), draws it, and waits for the
 * next sixtieth of a second. The core does the same start-up and then runs
 * one pass of that loop a step (EngineMain::chimeraStart and
 * SystemManager::mainLoop, patches/0003):
 *
 *   - A step is a sixtieth of a second of the machine's time, and the
 *     simulation runs exactly one frame in it (patches/0003); a menu, a
 *     pause or a loading screen runs no frame of the game but still reads
 *     the controls. The machine's clock is the number of steps run
 *     (seams.c answers SDL's counters from it).
 *   - The controls are the engine's own controller, set before each step by
 *     an input feeder (upstream's way for anything that is not a key or a
 *     game controller) from the two pads.
 *   - The picture is the window surface upstream's software drawer
 *     presents, 400x224, read after the step.
 *   - The sound is rmxmedia's mixer, run at the end of each step for
 *     exactly 800 stereo samples at 48000 Hz, by the driver, in place of the
 *     sound device's thread.
 *   - Every file is in the machine's memory (memfs.cpp): the data the core
 *     carries, the ROM, the project's mods, soundtrack and save data, and
 *     whatever the game writes.
 */
#include "oxygen/pch.h"
#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/engine/EngineMain.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/Simulation.h"

#include "oxygen/file/FilePackage.h"

#include "sonic3air/ConfigurationImpl.h"
#include "sonic3air/EngineDelegate.h"
#include "sonic3air/GameArgumentsReader.h"
#include "sonic3air/version.inc"

#include <emulibc.h>
#include <waterbox_slots.h>

#include "memfs.h"
#include "s3air-driver.h"
#include "s3air-files.h"

extern "C" FILE *__real_fopen(const char *path, const char *mode);

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). */
#define TEXT_ARG "\x1f"

namespace
{
	/* the engine's controller for one pad's buttons, in the wire's order,
	 * as upstream maps controller to pad (ControlsIn.cpp) */
	InputManager::Control *control_for(InputManager::ControllerScheme &c, int button)
	{
		switch (button)
		{
			case S3_BTN_UP: return &c.Up;
			case S3_BTN_DOWN: return &c.Down;
			case S3_BTN_LEFT: return &c.Left;
			case S3_BTN_RIGHT: return &c.Right;
			case S3_BTN_A: return &c.A;
			case S3_BTN_B: return &c.B;
			case S3_BTN_C: return &c.X;
			case S3_BTN_X: return &c.L;
			case S3_BTN_Y: return &c.Y;
			case S3_BTN_Z: return &c.R;
			case S3_BTN_START: return &c.Start;
			case S3_BTN_MODE: return &c.Back;
		}
		return nullptr;
	}

	struct Feeder : public InputFeeder
	{
		uint8_t held[S3_BTN_COUNT] = { 0 };
		void updateControls() override
		{
			for (int p = 0; p < S3_PLAYERS; ++p)
			{
				InputManager::ControllerScheme &c = mInputManager->accessController(p);
				for (int b = 0; b < S3_BTN_PER_PLAYER; ++b)
					mInputManager->setControlState(*control_for(c, b), held[p * S3_BTN_PER_PLAYER + b] != 0);
			}
		}
	};

	struct Driver
	{
		EngineDelegate *delegate = nullptr;
		GameArgumentsReader *arguments = nullptr;
		EngineMain *main = nullptr;
		Feeder *feeder = nullptr;
		bool ready = false;

		uint64_t frames = 0;
		int read = 0;

		s3drv_audio_callback audio_cb = nullptr;
		void *audio_user = nullptr;
		int16_t audio[S3_AUDIO_SAMPLES * 2];
		int audio_frames = 0;

		uint32_t video[S3_VIDEO_WIDTH * S3_VIDEO_HEIGHT];
		int video_w = S3_VIDEO_WIDTH, video_h = S3_VIDEO_HEIGHT;
	};

	Driver g;

	/* the window surface upstream's software drawer presents: the game's
	 * screen and the menus over it */
	void read_screen()
	{
		if (!g.main) return;
		SDL_Surface *s = SDL_GetWindowSurface(&g.main->getSDLWindow());
		if (!s || !s->pixels) return;
		const int w = std::min(s->w, S3_VIDEO_WIDTH), h = std::min(s->h, S3_VIDEO_HEIGHT);
		g.video_w = w;
		g.video_h = h;
		const SDL_PixelFormat *f = s->format;
		for (int y = 0; y < h; ++y)
		{
			const uint8_t *row = (const uint8_t *)s->pixels + (size_t)y * s->pitch;
			for (int x = 0; x < w; ++x)
			{
				uint32_t px;
				memcpy(&px, row + (size_t)x * 4, 4);
				const uint32_t r = (px & f->Rmask) >> f->Rshift, gg = (px & f->Gmask) >> f->Gshift, b = (px & f->Bmask) >> f->Bshift;
				g.video[y * S3_VIDEO_WIDTH + x] = 0xFF000000u | (r << 16) | (gg << 8) | b;
			}
		}
	}
}

void s3state_update(void);   /* game-state.cpp */

/* ------------------------------------------------------------ seams.c */

extern "C" uint64_t s3drv_clock_us(void) { return g.frames * 1000000ull / S3_FRAME_RATE; }
extern "C" void s3drv_audio_open(s3drv_audio_callback cb, void *user)
{
	g.audio_cb = cb;
	g.audio_user = user;
}

/* ------------------------------------------------- the engine's settings */

/* EngineMain::initConfigAndSettings calls this once the configuration and
 * the settings file are read (patches/0003): what the machine is, whatever
 * a settings file a project brings says. Only what the picture, the timing
 * and the host depend on is set here; the game's own options are the
 * game's, chosen in its menus and kept in its settings file like any save. */
void chimera_configure(Configuration &config)
{
	config.mFailSafeMode = false;
	config.mRenderMethod = Configuration::RenderMethod::SOFTWARE;
	config.mAutoDetectRenderMethod = false;
	config.mWindowMode = Configuration::WindowMode::WINDOWED;
	config.mWindowSize = Vec2i(S3_VIDEO_WIDTH, S3_VIDEO_HEIGHT);
	config.mGameScreen = Vec2i(S3_VIDEO_WIDTH, S3_VIDEO_HEIGHT);
	config.mDisplayIndex = 0;
	config.mFrameSync = Configuration::FrameSyncType::VSYNC_OFF;
	config.mUpscaling = 0;
	config.mBackdrop = 0;
	config.mPerformanceDisplay = 0;
	config.mScreenFilter = Configuration::ScreenFilter();
	config.mSimulationFrequency = 60;
	config.mAudio.mSampleRate = S3_AUDIO_RATE;
	config.mAudio.mUseAudioThreading = false;
	config.mGameRecorder.mRecordingMode = 0;
	config.mGameRecorder.mEnablePlayback = false;
	config.mDevMode.mEnabled = false;
	config.mDevMode.mEnableAtStartup = false;
	config.mStartPhase = 0;
	config.mLoadLevel = -1;
	config.mLoadSaveState.clear();
	config.mInputRecorderInput.clear();
	config.mInputRecorderOutput.clear();
	ConfigurationImpl &impl = static_cast<ConfigurationImpl &>(config);
	impl.mGameServerImpl.mGhostSync.mEnabled = false;
}

/* ------------------------------------------------------------ the exports */

extern "C" {

int s3drv_init(char *err, int errsize)
{
	for (uint32_t &px : g.video) px = 0xFF000000u;
	s3drv_read_clock_start();
	if (!s3files_load(err, errsize)) return 0;

	/* upstream's main(): early set-up, the random seed, the engine */
	EngineMain::earlySetup();
#ifdef S3AIR_NATIVE
	/* a tool of the native reference's, for the tests: the soundtrack files
	 * given loose (the soundtrack slot) packed into an audioremaster.bin by
	 * upstream's own packer (FilePackage, as its PackageBuilder makes the
	 * one a release downloads), written to the path S3AIR_PACK_REMASTERED
	 * names */
	if (const char *pack = getenv("S3AIR_PACK_REMASTERED"))
	{
		FilePackage::createFilePackage(L"made/audioremaster.bin", L"", { L"data/audio/remastered/" }, {}, L"", BUILD_NUMBER, true);
		size_t n = 0;
		const uint8_t *bytes = memfs::data(L"made/audioremaster.bin", &n);
		FILE *f = bytes ? __real_fopen(pack, "wb") : nullptr;
		if (f) { fwrite(bytes, 1, n, f); fclose(f); }
		snprintf(err, (size_t)errsize, "packed %zu bytes into %s", n, pack);
		return 0;
	}
#endif
	randomize();
	FTX::JobManager->setMaxThreads(0);
	g.arguments = new GameArgumentsReader();
	g.delegate = new EngineDelegate();
	g.main = new EngineMain(*g.delegate, *g.arguments);
	if (!g.main->chimeraStart())
	{
		snprintf(err, (size_t)errsize, "error.engine_did_not_start");
		return 0;
	}
	if (!g.audio_cb)
	{
		snprintf(err, (size_t)errsize, "error.engine_did_not_open_its_sound");
		return 0;
	}
	g.feeder = new Feeder();
	g.feeder->registerAtInputManager(InputManager::instance());

	/* the first pass loads the game: the ROM is checked, the mods found, the
	 * scripts compiled, the simulation set up. It is part of Init, before
	 * the machine is sealed, and no step. */
	FTX::System->mainLoop();
	if (!EmulatorInterface::hasInstance())
	{
		snprintf(err, (size_t)errsize, "error.game_did_not_start");
		return 0;
	}
	g.ready = true;
	if (getenv("S3AIR_DEBUG"))
	{
		size_t n = 0;
		const uint8_t *log = memfs::data(L"savedata/logfile.txt", &n);
		if (log) fprintf(stderr, "[s3air] logfile.txt:\n%.*s\n", (int)n, (const char *)log);
	}
	read_screen();
	s3state_update();
	return 1;
}

void s3drv_set_button(int index, int down)
{
	if (g.feeder && index >= 0 && index < S3_BTN_COUNT) g.feeder->held[index] = down ? 1 : 0;
}

void s3drv_frame(int render)
{
	g.audio_frames = 0;
	if (!g.ready) return;
	FTX::System->mainLoop();
	g.frames++;
	g.read = 1;
	if (getenv("S3AIR_DEBUG") && g.frames % 60 == 0)
	{
		Simulation &sim = Application::instance().getSimulation();
		fprintf(stderr, "[s3air] step %llu: running %d speed %g sim frame %u exec %d\n", (unsigned long long)g.frames,
		        (int)sim.isRunning(), sim.getSpeed(), sim.getFrameNumber(), (int)sim.getCodeExec().isCodeExecutionPossible());
	}

	/* the sound of the step: the mixer, called as the sound device would
	 * call it, for exactly a sixtieth of a second */
	memset(g.audio, 0, sizeof g.audio);
	if (g.audio_cb) g.audio_cb(g.audio_user, (uint8_t *)g.audio, (int)sizeof g.audio);
	g.audio_frames = S3_AUDIO_SAMPLES;
	if (render) read_screen();
	s3state_update();
}

const uint32_t *s3drv_video(int *w, int *h)
{
	*w = g.video_w;
	*h = g.video_h;
	return g.video;
}

const int16_t *s3drv_audio(int *samples)
{
	*samples = g.audio_frames;
	return g.audio;
}

int s3drv_input_was_read(void) { return g.ready ? g.read : 0; }
uint64_t s3drv_frames(void) { return g.frames; }

} // extern "C"
