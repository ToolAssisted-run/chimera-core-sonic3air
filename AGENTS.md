# AGENTS.md - Sonic 3 A.I.R. core for Chimera

This repository builds Sonic 3 A.I.R. (Sonic 3 - Angel Island Revisited, by
Eukaryot) as a core for Chimera (https://github.com/ToolAssisted-run/chimera).
It is a GAME core (`"kind": "game"`). It produces one file,
`sonic3air.chimeraCore`. The Sonic 3 & Knuckles ROM is never in the
repository or the package: the user provides it as firmware.

## Layout

- `extern/sonic3air`: upstream, a submodule. Never edited in place. It holds
  the engine (Oxygen), librmx, lemonscript, the game's code, scripts and
  data, and copies of SDL 2, zlib, libogg and libvorbis.
- `patches/`: numbered patches for `extern/sonic3air`, applied by
  `waterbox/apply-patches.sh`. Each upstream file is in one patch only.
- `waterbox/sources.mk`: the sources, defines and link wraps both builds
  share. `native.mk`, `guest.mk`: the two builds.
- `waterbox/s3air-driver.cpp`: the machine (start-up, a step, the picture,
  the sound, the controls, the engine's configuration). `s3air-files.cpp`:
  the machine's files at power-on (the data, the ROM, the mods, the
  soundtrack, the save data) and the save data out. `memfs.cpp`: every file
  in the machine's memory. `seams.c`: what SDL and the C library are
  answered with (the clock, rand, threads, the sound device).
  `no-network.cpp`: the engine's sockets, none of which opens.
  `game-state.cpp`: the memory domains and properties. `detmath.c`: the math
  functions. `wbx-entry.c`: the guest's exports. `data-pack.py`, `data.S`:
  upstream's data and scripts linked into the core.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json`, `languages/`: the declarations and the texts.
- `waterbox/run-gate.sh`: the core's tests. `waterbox/tests/`: the frontend
  tests and the test mods' maker.
- `pictures/`, `licenses/`: the pad's picture and the Genesis Plus GX terms.
- `build/`: every output. `tests/roms-local/`: the ROM, ignored by git.

## Commands, in order

```
git submodule update --init --recursive
cd waterbox
./run-gate.sh -m <miniBox> [-g <Sonic_Knuckles_wSonic3.bin>]
./tests/run-frontend.sh --chimera-root <chimera> [-r <Sonic_Knuckles_wSonic3.bin>]
./build-package.sh -m <miniBox>
```

`docs/BUILDING.md` has the requirements and the contract tests.

## Rules

- Upstream is changed only by a numbered patch, its CRLF line endings kept.
- Native and sandbox must stay the same machine: anything that reads a
  clock, a thread, a host file, the network, the C library's math or rand is
  answered by the core.
- Nothing the game writes reaches the host: it is all in `memfs`.
- Every new test is broken on purpose once and seen to fail.
- No ROM, no remastered soundtrack and no mod of anybody else's in the
  repository, in CI or in an issue. The tests make their own mods.
- The package is non-commercial (the Genesis Plus GX terms of the sound
  chips; LICENSE). Keep the licence texts in the package.
- Plain language and ASCII in documents. Commits: `type(scope): what`, a
  short body with the numbers, explicit paths staged.
