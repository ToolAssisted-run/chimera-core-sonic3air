# Plan and design log - Sonic 3 A.I.R. core

Dated entries; the newest decisions are at the end of each section.

## The request (2026-11-15)

Chimera issue 594 asked for Sonic 3 A.I.R. (Sonic 3 - Angel Island
Revisited, https://github.com/Eukaryot/sonic3air), Eukaryot's remaster of
Sonic 3 & Knuckles, as a game core. The owner decided to build it, with
mods, and to keep the work local: nothing pushed, no comment on the issue.
The core was built by an agent working for the owner; its choices that the
owner did not make are said to be its own below.

## The owner's decisions (owner, 2026-11-15)

- **Licence: accepted as upstream ships it.** Upstream is GPL-3.0, but two
  files of its engine, the Mega Drive sound chips (`sn76489` and `ym2612` in
  `Oxygen/oxygenengine/source/oxygen/simulation/sound/`), are derived from
  Genesis Plus GX and carry its terms: no sale, no use in a commercial
  product or activity. The GPL forbids added restrictions, so strictly read
  a binary of both cannot meet both licences; upstream distributes exactly
  that binary itself. The owner accepted the combination; the package is
  declared non-commercial (`"commercialUse": "prohibited"`, as the gpgx core
  declares its Genesis Plus GX code), and `LICENSE` carries the GPL-3.0, the
  Genesis Plus GX terms and a plain note on the clash. The other options put
  to the owner were replacing the two chips with code under GPL-compatible
  licences (the sound would differ from A.I.R.'s), asking Eukaryot first, or
  not building it.
- **The remastered soundtrack: left out of the package**, and a project can
  add it in a slot of its own, from the user's own A.I.R. installation.
- **A.I.R.'s own data** (its sprites, images, fonts, raw data, binary data
  and scripts): shipped in the package, as upstream publishes them.
- Left out: the Discord SDK (a proprietary library), the network (the
  update check, the game server's features, ghost sync, netplay, Crowd
  Control, downloads), and the OpenGL renderer.

## Feasibility (2026-11-15)

- **What it is.** The Oxygen engine runs the game's logic, rewritten by hand
  as lemonscript scripts, against the original ROM, from which it reads the
  levels, graphics and music; it emulates the parts of the Mega Drive the
  scripts need (68000 RAM, the VDP's layers and sprites, the sound chips).
  The menus, options and the mod manager are C++ (librmx's GUI). About
  260,000 lines of C++.
- **The ROM.** Upstream accepts exactly one: `Sonic_Knuckles_wSonic3.bin`,
  Sonic & Knuckles with Sonic 3 locked on, 0x400000 bytes, as Steam's Sega
  Classics has it. It sets byte 0x2001F0 to 0x4A and requires the file's
  MurmurHash64A (seed 0) to be 0x344983FFCFEFF8CB (`ResourcesCache.cpp`,
  `ConfigurationImpl.cpp`). The owner's file passes: 4,194,304 bytes, SHA-1
  B711A909CCE238CA4AF3E517A2EDCA306228EFA5. It is the core's one firmware.
- **One thread on one clock.** The engine runs one loop a frame
  (`SystemManager::mainLoop`: events, update, render); its worker threads
  (the job manager, for audio streaming) are optional and the configuration
  turns them off; the sound device's thread is SDL's.
- **The picture.** Upstream has a software renderer and a software drawer
  for its menus, which present into SDL's window surface. No GPU.
- **What a runner can test.** Nothing that plays: every frame of the game
  needs the ROM. The runner proves the build, the declarations, the
  refusals and the contract tests (new-core.md allows this).

## How the core is built (2026-11-15)

Upstream's own CMake build for Linux, as make files of our own
(`waterbox/sources.mk`), with upstream's copies of SDL 2 (the minimal,
dummy-driver configuration the RSDK cores use), zlib, libogg and libvorbis,
and without ImGui, Discord, libcurl, GLEW and OpenGL. Three patches
(docs/BUILDING.md). The core's own files:

- **A step** (`s3air-driver.cpp`): the engine is started as upstream's
  `main()` starts it, through `EngineMain::chimeraStart` (patch 0003), and
  each step runs one pass of rmxmedia's main loop. The pass's time is a
  sixtieth of a second exactly, and the simulation runs exactly one frame in
  a step whenever it runs at all (patch 0003): upstream accumulates host time
  and runs as many frames as it owes. The first pass, which loads the game
  (the ROM checked, the mods found, the scripts compiled), is part of Init.
- **The configuration** (`chimera_configure`, called by patch 0003 after the
  settings are read): the software renderer, a 400x224 window (A.I.R.'s
  game screen, one pixel a pixel), no frame cap or VSync, 48000 Hz sound
  without audio threads, no game recording, no dev mode, the start at the
  disclaimer as a fresh installation starts. The game's own options are the
  game's: chosen in its menus, kept in its settings file like a save.
- **Time** (`seams.c`): SDL's counters are the steps run; `time()` is the
  machine's clock start plus the steps, where the clock start is read once at
  Init from the sandbox (the project's clock start, Chimera's
  `wbx_set_clock_start`; 2017-05-27 12:44:28 by default) and the native
  reference takes the same default or `S3AIR_CLOCK_START`; `localtime` is
  UTC; `rand` and `srand` one generator in the machine's memory. The engine
  seeds its random numbers from `time()`, and the game takes a level's
  random base from them, so the clock start is part of the machine.
  rmxmedia's high resolution timer and the network code's clock read the
  machine's clock too (patch 0003).
- **Files** (`memfs.cpp`, patch 0002): rmxbase's `FileIO` and the C
  library's `fopen` both come to one file system in the machine's memory, in
  path order. At Init it holds upstream's data and scripts (linked into the
  core as a blob, read where they are), the ROM, the mods' zips, the
  soundtrack files (in sealed memory: read-only and in no state) and the
  save data; whatever the game writes is heap memory a savestate carries.
  No host folder is asked for (patch 0002 answers upstream's app data, Steam
  and home lookups with nothing).
- **The controls**: an `InputFeeder` (upstream's own way for input that is
  not a key or a game controller) sets the engine's controllers from the two
  pads before each step; upstream maps them onto the Mega Drive pad it
  emulates (ControlsIn.cpp): the controller's A, B and X are the pad's A, B
  and C, Y is Y, L and R are X and Z, Back is Mode. The menus read the same
  controllers. No key, mouse or touch reaches the engine.
- **The sound**: rmxmedia's mixer, called at the end of every step for 800
  stereo samples, as SDL's audio thread would call it.
- **The picture**: the window surface upstream's software drawer presents,
  converted to BGRA.
- **No network** (`no-network.cpp`): oxygen_netcore's sockets are the
  core's, and none opens; upstream's downloader is not built (patch 0001).
  The engine sees a computer with no connection.

## Mods (2026-11-15)

The owner's brief: mods as a project slot, as the Cemu core's graphic packs,
zips of upstream's mod folders loaded at power-on with upstream's own mod
loading, in an order the project records.

**The slot.** `mods`, "Mods", any number of zips (`min` 0, `max` -1). A zip
holds one or more mod folders, each with its `mod.json`, at any depth (as
mods are shared; a `mod.json` at the zip's root is a mod too). A project
records each zip by its name and SHA-1, in the slot's order.

**How they are loaded.** At Init each zip is put, as it is, in upstream's
mods folder (`savedata/mods/<name>.zip`, sealed memory), and the core writes
upstream's list of active mods (`savedata/mods/active-mods.json`) naming
every mod folder it found in the zips, zip by zip in the slot's order and
in path order within a zip (a folder inside another mod, or under a folder
whose name starts with `#`, is not a mod, as upstream's scan has it).
Upstream's own ModManager then mounts the zips, finds the mods and turns on
those the list names, in that order, as if a person had turned them on in
the game's Mods menu in that order; the mods' scripts, sprites, palettes,
audio and settings load as upstream loads them. With two mods that change
the same thing, the one later in the list has the last word.

A zip that is not a zip, or holds no mod, is refused with the reason. The
Mods menu stays in the game and can change the list while the game runs, as
in upstream; such a change is made with the controls, so a movie records
it.

## The remastered soundtrack (owner, 2026-11-15)

**The slot.** `soundtrack`, "Remastered soundtrack", any number of files:
either upstream's package `audioremaster.bin`, as an installation has it in
its data folder (or in its save data folder once the game downloaded it),
or the files of a `data/audio/remastered` folder (its Ogg files and
`audio_replacements.json`), which is what upstream's source has. Choosing
the one file is simplest for a person with an installation; the loose files
are there for a build from source. They are mounted where upstream looks:
`data/audioremaster.bin`, loaded by upstream's package loader as an optional
package, or `data/audio/remastered/<name>`.

**Without it**, the game plays the original music, from the ROM through the
emulated sound chips. Upstream's Soundtrack option (Remastered, the default,
or Original) still shows; with Remastered chosen and no files, the game
plays exactly what Original plays (measured: the same sound, sample for
sample). Its option to download the soundtrack finds no downloader in this
build. The soundtrack changes the sound only: the pictures, the memory and
the frames are the same with it and without it (measured).

## Save data (2026-11-15)

Emulator > Export Save Data gives what the game wrote in its save data
folder: `settings.json` (its options), `settings_input.json`,
`storage/s3air_saveslots.bin` (its save slots) and anything else it keeps
there, such as its unlocks and the mods' settings; not the ROM's copy, its
log or the mods. A project takes them back in its `savedata` slot, as the
zip the export writes, unpacked into the save data folder before the
machine is sealed. A zip that holds the ROM or a mod is refused. This is
also how a project starts with options other than the defaults: options
change play, so they are part of the machine, and they are chosen in the
game's menus (recorded) or given as save data (recorded by SHA-1).

## Properties (2026-11-15)

The domain "68K RAM" is the 64 KiB of RAM the engine emulates, big-endian
as on the console; the properties name Sonic 3 & Knuckles' variables in it,
at the addresses A.I.R.'s own scripts define: the zone and act, the game
mode, rings, lives, score, the timer, player 1's position, sub-pixel and
speeds and player 2's position and speeds. A poke is a write into that RAM
between steps. "Game State" is a small block of the core's own: the game's
timer in milliseconds (the table's `gameTimer`), the frames the game has
run and the steps the machine has run.

## What was left out, and why

- The network: the update check, the game server (ghost sync and the other
  online features), netplay, Crowd Control, the soundtrack's download. None
  can be deterministic, and Chimera reaches for nothing over the network.
- Discord: a proprietary library, and the network.
- The OpenGL renderer and its upscalers and shaders: the software renderer
  draws the same picture on every machine. What the game's Display options
  do when one asks for OpenGL was not tried; the core sets the software
  renderer at every start.
- Dev mode and ImGui: dev mode is off and ImGui is not built.
- Lag frames: every step reads the controls once the game has loaded (the
  menus read them when the game does not), so no step is counted as lag.

## Measured (2026-11-15)

On jaffanator01, in the Ubuntu 24.04 container CI uses (GCC 13), x86_64,
with the owner's ROM (SHA-1 B711A909...). Chimera at a0b446f8.

- **Played**: power-on, the disclaimer (frames 0 to about 480), the SEGA
  logo (about 700), the title (850 on), Start (frame 1000) to the main menu,
  A on Normal Game (1250) to the data select, A on save slot 1 (1450) for a
  new game, the opening (the Tornado over the sea, Super Sonic, the beach),
  Angel Island act 1's title card (about 2600), and play from 2800 with
  Right held: Sonic runs, collects rings, is hit by an enemy (rings to 0 at
  about 3500), jumps. Pictured frame by frame at 300, 500-2700 every 200,
  2700-4000 every 100, and the last frame of the frontend's runs (3200,
  with and without mods: the HUD's lives read 3 and 77).
- **Core tests** (`run-gate.sh -g`): 26 passed, 0 failed. Without the ROM
  (as on CI) 8 of them run: three refusals, the clock, no network, the
  data, the firmware name and the texts.
  - native = sandbox over 4000 frames: every picture, every sample, the
    frames, lag and both memory domains; the game ran 2734 frames of its
    own in them (the menus run none).
  - a save and a load around every one of the 4000 frames = straight (a
    state is 36.7 MB, 31.6 MB in Chimera); states taken at frames 1300 (the
    main menu) and 3100 (in play) and loaded in a new host = straight.
  - mods: lives 3 without, 50 with mod A, 77 with A then B, 50 with B then
    A; native = sandbox with two mods.
  - the remastered soundtrack: upstream's 107 Ogg files and
    audio_replacements.json, and an audioremaster.bin made from them by
    upstream's own packer (131,677,405 bytes): the sound changes from the
    title's music on (second 8), and nothing else; native = sandbox.
  - save data: 3 files exported after a new game (settings.json,
    settings_input.json, storage/s3air_saveslots.bin), given back as a zip:
    the data select shows slot 1 in zone 1 with 3 lives.
  - the clock start: 2030-01-02 03:04:05 plays the route differently from
    the default (the 68K RAM at frame 3000 differs); native = sandbox at the
    same start.
- **Frontend tests** (`run-frontend.sh -r`): 10 passed, 0 failed. Through
  chimera-run: a movie made into a project = the native reference (68K RAM
  and the last picture, 3200 frames); a project with two mods in its slot
  records livesA.zip and livesB.zip in that order by SHA-1, ends with 77
  lives (50 with the zips the other way round) and = the reference; that
  movie replays the same in another process, with a save and load around
  every frame, and from a state of frame 1600 (31.6 MB) loaded in another
  process. Through Chimera's window, headless: the mods project played to
  3200 frames = the reference; the game's timer (00:11.116) in the saved
  project's headers; game.get/set by name; the core's refusal of a file of
  zeros in the ROM's place shown by Chimera; the package's keybinds the
  defaults; nothing written under the Chimera checkout.
- **Contract tests** (`test-packages`): 24 passed, 0 failed.
- **Windows**: Windows 11 Home in a KVM VM on jaffanator01, with
  chimera-run.exe built from Chimera at 3060b792: the mods project made on
  Linux, played to 3200 frames, ends with the 68K RAM and the last picture
  of the Linux runs, byte for byte; with a save and load around every frame
  too. Chimera's window was not tried on Windows. aarch64 was not built.
- **Speed**: 2800 frames in 10.2 s natively and 10.6 s in the sandbox,
  with the machine shared with other builds; a save and a load around every
  frame make 4000 frames take 78 s. Start-up, the scripts compiled from
  source, takes well under a second.
- **Package**: 8.3 MB (24 MB unpacked), of it 12.6 MB of upstream's data
  and scripts (742 files) inside core.wbx.

## Each test seen to fail (2026-11-15)

Every test of this repository was run against a copy of it broken on
purpose, and seen to fail (a copy, so nothing broken was ever committed).
Breakages, each with the tests it failed:

- The ROM's checksum not checked: refusals "4 MiB of zeros".
- srand left unwrapped: clock.
- The remastered soundtrack put in the core's data: data.
- waterbox.config's firmware renamed: firmware.
- A key taken out of the German table: texts.
- The machine's clock start not the sandbox's: clock start.
- The "68K RAM" domain a copy of the RAM, not the RAM: pokes.
- The active mods written in reverse: mods (lives 50 50 77 for A, A then
  B, B then A).
- The soundtrack's files not mounted: soundtrack, soundtrack option. The
  test "soundtrack package" passed with that breakage (with no soundtrack at
  all, the package and the loose files sound the same); it now also asks
  that the package sound otherwise than no soundtrack.
- The controls never reaching the engine: input (and teeth: a route one
  frame different then digests the same).
- Upstream's sockets in place of the core's: no network (socket, connect,
  bind, sendto, recvfrom and getaddrinfo linked in).
- The step count kept in memory no state carries: session (both).
- SetRenderingEnabled ignored: turbo.
- audioremaster.bin not mounted: soundtrack package.
- A write into the machine made by the process's own count of steps, which
  no state carries: engine:replay (the state carried to another process
  ends otherwise), engine:mods and gui:game-properties (lives 78).
- The native reference's clock a second later than the sandbox's:
  equivalence, clock start, mods, soundtrack, soundtrack package (their
  native = sandbox parts), engine:movie, engine:mods, gui:project.
- The ROM's checksum not checked: gui:refusal (Chimera then starts the
  game).
- The game's timer named wrong in the table: gui:game-time.

Not seen to fail: rerecord (no breakage was found that a save and load in
the same process notices and a straight run does not), gui:keybinds (a key
name Chimera never sends, added to the package's bindings, still passed:
the check compares what the window adopted with the package, and the
window kept the name), gui:writes-nothing-home (not broken). The last
three come from the RSDK cores' frontend tests.

## The owner's answers before publishing (owner, 2026-11-15)

- Publish it.
- A.I.R.'s options stay in the game's own Options menu, recorded as presses,
  or in the save data; none becomes a project setting.
- No lag frames, as the engine reads the controls on every frame; the README
  says so.
- x86_64 only: Chimera's owner decided on 2026-11-07 that game cores get no
  aarch64 package, so the workflow builds and publishes x86_64 alone.
