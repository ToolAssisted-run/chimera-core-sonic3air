/* s3air-driver.h - Sonic 3 A.I.R. (the Oxygen engine, with its Sonic 3 &
 * Knuckles scripts) as a machine that is stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): two Mega Drive
 * six-button pads, each Up, Down, Left, Right, A, B, C, X, Y, Z, Start and
 * Mode. They reach the game as upstream maps its controller to the pad it
 * emulates (ControlsIn.cpp): the controller's A, B and X are the pad's A, B
 * and C, its Y is Y, its L and R are X and Z, Start is Start and Back is
 * Mode; the menus read the same controller.
 */
#ifndef S3AIR_DRIVER_H
#define S3AIR_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum S3Button
{
	S3_BTN_UP,
	S3_BTN_DOWN,
	S3_BTN_LEFT,
	S3_BTN_RIGHT,
	S3_BTN_A,
	S3_BTN_B,
	S3_BTN_C,
	S3_BTN_X,
	S3_BTN_Y,
	S3_BTN_Z,
	S3_BTN_START,
	S3_BTN_MODE,
	S3_BTN_PER_PLAYER
};
#define S3_PLAYERS 2
#define S3_BTN_COUNT (S3_BTN_PER_PLAYER * S3_PLAYERS)

/* the picture: the game's screen as the engine draws it into its window -
 * 400x224, the width A.I.R. plays at, at one pixel a pixel */
#define S3_VIDEO_WIDTH 400
#define S3_VIDEO_HEIGHT 224

/* stereo at the engine's own rate, 48000 Hz; one step is one sixtieth of a
 * second, 800 samples */
#define S3_AUDIO_RATE 48000
#define S3_FRAME_RATE 60
#define S3_AUDIO_SAMPLES (S3_AUDIO_RATE / S3_FRAME_RATE)

/* 0 on failure, with the reason in err */
int s3drv_init(char *err, int errsize);
void s3drv_set_button(int index, int down);
void s3drv_frame(int render);
const uint32_t *s3drv_video(int *w, int *h);
const int16_t *s3drv_audio(int *samples);
int s3drv_input_was_read(void);
uint64_t s3drv_frames(void);

/* the memory domains and the property table */
int s3drv_domain_count(void);
const char *s3drv_domain_name(int i);
uint8_t *s3drv_domain_ptr(int i);
int64_t s3drv_domain_size(int i);
int s3drv_domain_writable(int i);
const char *s3drv_game_properties(void);

/* Export Save Data: what the game wrote in its save data folder */
int s3drv_saves_count(void);
const char *s3drv_saves_name(int index);
int64_t s3drv_saves_size(int index);
const uint8_t *s3drv_saves_data(int index);

/* seams.c */
uint64_t s3drv_clock_us(void);
void s3drv_read_clock_start(void);
#ifdef __cplusplus
}
#endif

/* SDL's audio callback type, without SDL's header */
typedef void (*s3drv_audio_callback)(void *userdata, uint8_t *stream, int len);
#ifdef __cplusplus
extern "C"
#endif
void s3drv_audio_open(s3drv_audio_callback cb, void *user);

#endif
