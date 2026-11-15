/* seams.c - what the engine reaches through SDL and the C library, answered
 * by the core.
 *
 * Linked with -Wl,--wrap=<name> for each __wrap_ below, in both builds
 * (sources.mk). SDL is upstream's copy of SDL 2, built in its minimal
 * configuration (the dummy video driver, no threads, no timers, no CPU
 * feature dispatch); the engine uses it as it is, but for:
 *
 *   - time: SDL's tick counters are the machine's clock, the number of
 *     steps run (a sixtieth of a second each), which only a step moves.
 *     time() is the second the machine's clock started at (the project's
 *     clock start, which the sandbox keeps) plus the steps run; localtime()
 *     reads it as UTC, in both builds, whatever the host's time zone.
 *   - rand and srand: one generator, its state in the machine's memory, the
 *     same in both builds (glibc's and musl's rand are other sequences).
 *     The engine seeds it from time() at start-up.
 *   - threads: there are none. SDL_CreateThread fails, so the engine's job
 *     manager runs every job where it is asked for (it is also told to keep
 *     no worker threads, s3air-driver.cpp).
 *   - SDL_Init: what a build without threads cannot give is left out; what is
 *     left is the video driver (dummy) and events.
 *   - the sound device: never opened. The engine's mixer is called by the
 *     driver for exactly one step's samples at the end of each step.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "s3air-driver.h"

/* ----------------------------------------------------------------- time */

Uint64 __wrap_SDL_GetPerformanceCounter(void) { return s3drv_clock_us(); }
Uint64 __wrap_SDL_GetPerformanceFrequency(void) { return 1000000; }
Uint32 __wrap_SDL_GetTicks(void) { return (Uint32)(s3drv_clock_us() / 1000); }
Uint64 __wrap_SDL_GetTicks64(void) { return s3drv_clock_us() / 1000; }
void __wrap_SDL_Delay(Uint32 ms) { (void)ms; }

time_t __real_time(time_t *t);

/* The second the machine's clock started at. The sandbox answers the
 * project's clock start (Chimera's wbx_set_clock_start, 2017-05-27 12:44:28
 * UTC when the project names none), and the clock it keeps moves only when
 * the guest sleeps, which nothing here does, so this is read at Init. The
 * native reference has no sandbox: it takes the same default, or
 * S3AIR_CLOCK_START (seconds since 1970) to match a project that names
 * another. */
static time_t g_clock_start;
void s3drv_read_clock_start(void)
{
#ifdef S3AIR_NATIVE
	const char *env = getenv("S3AIR_CLOCK_START");
	g_clock_start = env && *env ? (time_t)strtoll(env, NULL, 10) : (time_t)1495889068;
#else
	g_clock_start = __real_time(NULL);
#endif
}

time_t __wrap_time(time_t *t)
{
	const time_t v = g_clock_start + (time_t)(s3drv_clock_us() / 1000000);
	if (t) *t = v;
	return v;
}

struct tm *__wrap_localtime(const time_t *t)
{
	static struct tm out;
	return gmtime_r(t, &out);
}

static uint64_t g_rand_seed;
void __wrap_srand(unsigned s) { g_rand_seed = s - 1; }
int __wrap_rand(void)
{
	g_rand_seed = 6364136223846793005ULL * g_rand_seed + 1;
	return (int)(g_rand_seed >> 33);
}

/* ------------------------------------------------------------------- init */

int __real_SDL_Init(Uint32 flags);
int __real_SDL_InitSubSystem(Uint32 flags);
static const Uint32 k_unavailable = SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
	SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR;

int __wrap_SDL_Init(Uint32 flags)
{
	/* the dummy video driver, over whatever the environment says, so a host's
	 * SDL_VIDEODRIVER never reaches the native reference either */
	SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "dummy", SDL_HINT_OVERRIDE);
	/* and no handler of the host's signals: a Ctrl-C to the native reference
	 * must not reach the machine as a quit */
	SDL_SetHintWithPriority(SDL_HINT_NO_SIGNAL_HANDLERS, "1", SDL_HINT_OVERRIDE);
	return __real_SDL_Init(flags & ~k_unavailable);
}

int __wrap_SDL_InitSubSystem(Uint32 flags)
{
	if (flags & k_unavailable)
	{
		/* the sound: the driver mixes it itself (below); the engine only
		 * needs to be told yes */
		if (flags == SDL_INIT_AUDIO) return 0;
		return SDL_SetError("not in this machine");
	}
	return __real_SDL_InitSubSystem(flags);
}

/* ------------------------------------------------------------------ audio */

/* rmxmedia's AudioManager asks for 16-bit stereo at the engine's rate; the
 * driver calls its mixer at the end of every step */
SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                             SDL_AudioSpec *obtained, int allowed_changes)
{
	(void)device; (void)iscapture; (void)allowed_changes;
	if (!desired || desired->freq != S3_AUDIO_RATE || desired->format != AUDIO_S16LSB || desired->channels != 2 || !desired->callback)
	{
		SDL_SetError("the core's sound device is 48000 Hz, 16-bit, stereo");
		return 0;
	}
	if (obtained)
	{
		*obtained = *desired;
		obtained->silence = 0;
		obtained->size = (Uint32)desired->samples * 4;
	}
	s3drv_audio_open(desired->callback, desired->userdata);
	return 1;
}

int __wrap_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	return __wrap_SDL_OpenAudioDevice(NULL, 0, desired, obtained, 0) ? 0 : -1;
}

void __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev; (void)pause_on; }
void __wrap_SDL_PauseAudio(int pause_on) { (void)pause_on; }
void __wrap_SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_CloseAudio(void) {}
void __wrap_SDL_LockAudio(void) {}
void __wrap_SDL_UnlockAudio(void) {}
void __wrap_SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

/* --------------------------------------------------------------- threads */

SDL_Thread *__wrap_SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
	(void)fn; (void)name; (void)data;
	SDL_SetError("no threads in this machine");
	return NULL;
}

SDL_Thread *__wrap_SDL_CreateThreadWithStackSize(SDL_ThreadFunction fn, const char *name, const size_t stacksize, void *data)
{
	(void)stacksize;
	return __wrap_SDL_CreateThread(fn, name, data);
}
