/* wbx-entry.c - the chimera guest ABI over s3air-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h), which is what makes the equivalence test
 * a real proof: the same driver, the same exports, one in the sandbox and one
 * out of it.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "s3air-driver.h"

static char g_load_error[1024];

/* buttons set one at a time (SetButton) and the packed mask of FrameAdvance:
 * a step sees their union */
static uint8_t g_set_buttons[S3_BTN_COUNT];

/* Turbo: the picture is not read back. Only that - the engine draws its
 * screen whatever happens. */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return s3drv_init(g_load_error, (int)sizeof g_load_error);
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < S3_BTN_COUNT) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < S3_BTN_COUNT; i++)
		s3drv_set_button(i, g_set_buttons[i] | (int)((packed >> i) & 1));
	s3drv_frame(chimera_render_enabled);
}

/* Emulator > Export Save Data: the files the game wrote in its save data
 * folder; the Save data slot takes them back as a zip */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return s3drv_saves_count(); }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return s3drv_saves_name(i); }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return s3drv_saves_size(i); }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return s3drv_saves_data(i); }

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)s3drv_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	s3drv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	s3drv_video(&w, &h);
	return h;
}
/* square pixels: 400x224 */
ECL_EXPORT int GetDisplayAspectX(void) { return S3_VIDEO_WIDTH; }
ECL_EXPORT int GetDisplayAspectY(void) { return S3_VIDEO_HEIGHT; }

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)s3drv_audio(&n);
}

ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	s3drv_audio(&n);
	return n;
}

ECL_EXPORT int GetVsyncNumerator(void) { return S3_FRAME_RATE; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int InputWasRead(void) { return s3drv_input_was_read(); }

ECL_EXPORT int GetMemoryDomainCount(void) { return s3drv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return s3drv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return s3drv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return s3drv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return s3drv_domain_writable(i); }

/* chimera docs/game-cores.md: the property table */
ECL_EXPORT const char *GetGameProperties(void) { return s3drv_game_properties(); }

/* the machine's own clock, in steps, for harnesses that compare machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return s3drv_frames(); }
