# Sonic 3 A.I.R. for Chimera

This repository builds [Sonic 3 A.I.R.](https://github.com/Eukaryot/sonic3air)
(Sonic 3 - Angel Island Revisited, Eukaryot's remaster of Sonic 3 & Knuckles)
as a game core for [Chimera](https://github.com/ToolAssisted-run/chimera), a
frontend for tool-assisted speedruns. The core plays the game frame by frame,
the same every time, with savestates, rewinding and movies, its menus, its
options and its mods included.

The build makes one file, `sonic3air.chimeraCore`. Put it in Chimera's `Cores`
folder.

## What you bring

The Sonic 3 & Knuckles ROM, as the project's firmware: Sonic & Knuckles with
Sonic 3 locked on, as one 4 MiB file, `Sonic_Knuckles_wSonic3.bin`, as Steam's
Sega Classics has it. It is the only ROM Sonic 3 A.I.R. accepts, and the core
checks it as the game does. The checked file has the SHA-1
`B711A909CCE238CA4AF3E517A2EDCA306228EFA5`.

The package carries Sonic 3 A.I.R.'s own code, scripts and data, as upstream
publishes them, and none of SEGA's game.

## A project's files

- **Mods**: any number of zips of Sonic 3 A.I.R. mod folders (each with its
  `mod.json`), loaded when the game starts, in the project's order, by the
  game's own mod loader. They are part of the machine: the project records
  each zip by its SHA-1, and a movie needs the same zips.
- **Remastered soundtrack**: the remastered music from your own Sonic 3 A.I.R.
  installation, its `audioremaster.bin` or the files of a
  `data/audio/remastered` folder. Without it the game plays the original
  music from the ROM.
- **Save data**: the game's options, save slots and unlocks to start with, as
  Emulator > Export Save Data wrote them.

Everything the game writes (its settings, its saves, the mods' own settings)
is kept in the machine, never on your computer.

## What it does not do

No network: no update check, no ghost sync or other online features, no
netplay, no downloads. No Discord, no OpenGL renderer (the game draws with
its software renderer). No lag frames: A.I.R.'s engine reads the controls
on every frame, so Chimera's lag counter never counts one. No aarch64
package: game cores are published for x86_64 only (an aarch64 Mac runs it
under Rosetta 2). [docs/PLAN.md](docs/PLAN.md) says what was decided
and measured, [docs/BUILDING.md](docs/BUILDING.md) how to build and test it.

## Licence

This repository's own files are GPL-3.0-or-later. Sonic 3 A.I.R. is GPL-3.0;
two of its files, the Mega Drive sound chips, carry the Genesis Plus GX
terms, which forbid commercial use, so a package built from here may not be
sold or used commercially. [LICENSE](LICENSE) says what each part is under,
and how the two licences meet. Sonic the Hedgehog is a trademark of SEGA;
Sonic 3 A.I.R. is a non-profit fan project, not affiliated with SEGA or Sonic
Team.
