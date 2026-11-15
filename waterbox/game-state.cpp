/* game-state.cpp - the memory domains and the property table
 * (chimera docs/game-cores.md, "Properties").
 *
 * Sonic 3 A.I.R. runs Sonic 3 & Knuckles' logic, rewritten as scripts,
 * against the Mega Drive memory it emulates: the 68000's 64 KiB of RAM
 * (EmulatorInterface's RAM), laid out as the original game lays it out. That
 * RAM is exposed as it is, as the domain "68K RAM" (offset = address &
 * 0xFFFF, big-endian as on the console), and the properties name the
 * original game's variables in it: the addresses are the ones A.I.R.'s own
 * scripts define (scripts/.../definitions, e.g. ring_counter =
 * u16[0xfffffe20], timer.minutes = u8[0xfffffe23], objA0.position.x =
 * u32[A0 + 0x10]), player 1's object at 0xffffb000 and player 2's
 * (Tails) at 0xffffb04a. A poke is a write into that RAM between steps,
 * which the game's next frame reads.
 *
 * Beside it, "Game State" is a small block of the core's own, written after
 * every step: the in-game time in milliseconds (from the game's timer) and
 * the steps run. Nothing reads it back.
 */
#include "oxygen/pch.h"
#include "oxygen/application/Application.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"

#include "s3air-driver.h"

namespace
{
#pragma pack(push, 1)
	struct GameState
	{
		uint32_t igtMs;         /* 0: the game's timer, in milliseconds */
		uint32_t simFrame;      /* 4: frames the simulation has run */
		uint64_t steps;         /* 8: steps the machine has run */
	};
#pragma pack(pop)

	GameState g_state;

	uint8_t *ram()
	{
		return Application::hasInstance() ? EmulatorInterface::instance().getRam() : nullptr;
	}
}

void s3state_update(void)
{
	const uint8_t *r = ram();
	if (!r) return;
	const uint32_t minutes = r[0xFE23], seconds = r[0xFE24], frames = r[0xFE25];
	g_state.igtMs = (minutes * 60 + seconds) * 1000 + frames * 1000 / 60;
	g_state.simFrame = Application::instance().getSimulation().getFrameNumber();
	g_state.steps = s3drv_frames();
}

extern "C" {

int s3drv_domain_count(void) { return 2; }
const char *s3drv_domain_name(int i) { return i == 0 ? "68K RAM" : i == 1 ? "Game State" : ""; }
uint8_t *s3drv_domain_ptr(int i) { return i == 0 ? ram() : i == 1 ? (uint8_t *)&g_state : nullptr; }
int64_t s3drv_domain_size(int i) { return i == 0 ? 0x10000 : i == 1 ? (int64_t)sizeof g_state : 0; }
int s3drv_domain_writable(int i) { return i == 0; }

const char *s3drv_game_properties(void)
{
	return
	"{\n"
	"  \"gameTimer\": \"Time.IGT Ms\",\n"
	"  \"properties\": [\n"
	"    { \"name\": \"Time.IGT Ms\", \"domain\": \"Game State\", \"offset\": 0, \"type\": \"u32\", \"group\": \"Time\", \"writable\": false, \"description\": \"prop.igt_ms\" },\n"
	"    { \"name\": \"Time.Simulation Frame\", \"domain\": \"Game State\", \"offset\": 4, \"type\": \"u32\", \"group\": \"Time\", \"writable\": false, \"description\": \"prop.sim_frame\" },\n"
	"    { \"name\": \"Time.Steps\", \"domain\": \"Game State\", \"offset\": 8, \"type\": \"u64\", \"group\": \"Time\", \"writable\": false, \"description\": \"prop.steps\" },\n"
	"    { \"name\": \"Timer.Minutes\", \"domain\": \"68K RAM\", \"offset\": 65059, \"type\": \"u8\", \"group\": \"Time\", \"description\": \"prop.timer_minutes\" },\n"
	"    { \"name\": \"Timer.Seconds\", \"domain\": \"68K RAM\", \"offset\": 65060, \"type\": \"u8\", \"group\": \"Time\", \"description\": \"prop.timer_seconds\" },\n"
	"    { \"name\": \"Timer.Frames\", \"domain\": \"68K RAM\", \"offset\": 65061, \"type\": \"u8\", \"group\": \"Time\", \"description\": \"prop.timer_frames\" },\n"
	"    { \"name\": \"Game.Mode\", \"domain\": \"68K RAM\", \"offset\": 62976, \"type\": \"u8\", \"group\": \"Game\", \"writable\": false, \"description\": \"prop.game_mode\" },\n"
	"    { \"name\": \"Game.Zone\", \"domain\": \"68K RAM\", \"offset\": 65040, \"type\": \"u8\", \"group\": \"Game\", \"writable\": false, \"description\": \"prop.zone\" },\n"
	"    { \"name\": \"Game.Act\", \"domain\": \"68K RAM\", \"offset\": 65041, \"type\": \"u8\", \"group\": \"Game\", \"writable\": false, \"description\": \"prop.act\" },\n"
	"    { \"name\": \"Game.Rings\", \"domain\": \"68K RAM\", \"offset\": 65056, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Game\", \"description\": \"prop.rings\" },\n"
	"    { \"name\": \"Game.Lives\", \"domain\": \"68K RAM\", \"offset\": 65042, \"type\": \"u8\", \"group\": \"Game\", \"description\": \"prop.lives\" },\n"
	"    { \"name\": \"Game.Score\", \"domain\": \"68K RAM\", \"offset\": 65062, \"type\": \"u32\", \"endian\": \"big\", \"group\": \"Game\", \"description\": \"prop.score\" },\n"
	"    { \"name\": \"Player 1.X\", \"domain\": \"68K RAM\", \"offset\": 45072, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.x\" },\n"
	"    { \"name\": \"Player 1.X Sub\", \"domain\": \"68K RAM\", \"offset\": 45074, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.x_sub\" },\n"
	"    { \"name\": \"Player 1.Y\", \"domain\": \"68K RAM\", \"offset\": 45076, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.y\" },\n"
	"    { \"name\": \"Player 1.Y Sub\", \"domain\": \"68K RAM\", \"offset\": 45078, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.y_sub\" },\n"
	"    { \"name\": \"Player 1.X Speed\", \"domain\": \"68K RAM\", \"offset\": 45080, \"type\": \"s16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.x_speed\" },\n"
	"    { \"name\": \"Player 1.Y Speed\", \"domain\": \"68K RAM\", \"offset\": 45082, \"type\": \"s16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.y_speed\" },\n"
	"    { \"name\": \"Player 1.Ground Speed\", \"domain\": \"68K RAM\", \"offset\": 45084, \"type\": \"s16\", \"endian\": \"big\", \"group\": \"Player 1\", \"description\": \"prop.ground_speed\" },\n"
	"    { \"name\": \"Player 2.X\", \"domain\": \"68K RAM\", \"offset\": 45146, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 2\", \"description\": \"prop.x\" },\n"
	"    { \"name\": \"Player 2.Y\", \"domain\": \"68K RAM\", \"offset\": 45150, \"type\": \"u16\", \"endian\": \"big\", \"group\": \"Player 2\", \"description\": \"prop.y\" },\n"
	"    { \"name\": \"Player 2.X Speed\", \"domain\": \"68K RAM\", \"offset\": 45154, \"type\": \"s16\", \"endian\": \"big\", \"group\": \"Player 2\", \"description\": \"prop.x_speed\" },\n"
	"    { \"name\": \"Player 2.Y Speed\", \"domain\": \"68K RAM\", \"offset\": 45156, \"type\": \"s16\", \"endian\": \"big\", \"group\": \"Player 2\", \"description\": \"prop.y_speed\" }\n"
	"  ]\n"
	"}\n";
}

} // extern "C"
