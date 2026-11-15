# Building and testing the Sonic 3 A.I.R. core

## Requirements

Linux (CI uses Ubuntu 24.04, x86_64 and aarch64). gcc and g++, make, python3,
binutils (`nm`), and a Chimera checkout with miniBox built with its C++ guest
toolchain:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
mb=<chimera>/extern/chimera-common-minibox
meson setup "$mb/build/meson-linux" "$mb" && meson compile -C "$mb/build/meson-linux"
meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true && meson compile -C "$mb/build/meson-cpp"
```

For the frontend tests, Chimera built with its window (its README,
"Building"; `tools/ci-chimera.sh` does it). The window runs headless and needs
no display.

## Sources

- `extern/sonic3air`: Sonic 3 A.I.R. (https://github.com/Eukaryot/sonic3air),
  pinned. It carries everything the build uses: librmx, lemonscript, the
  Oxygen engine, the game's code, scripts and data, and copies of SDL 2,
  zlib, libogg and libvorbis (`framework/external`). Never edited in place:
  `patches/` is applied by `waterbox/apply-patches.sh`, which every build
  runs first.

```
git submodule update --init --recursive
```

The patches (each file of upstream is in one patch only):

- `0001` the software renderer only (`RMX_NO_OPENGL` turns rmxmedia's OpenGL
  switch off) and no downloader (no libcurl).
- `0002` nothing of the host: rmxbase's file functions ask the core's
  `memfs.cpp`; no host folder, Steam folder, clipboard, URL or program is
  looked for or opened.
- `0003` one step of the machine is one pass of the engine's main loop and
  one frame of the game, on the machine's clock: a fixed sixtieth of a
  second, no waiting, the timers and the network code's clock the
  machine's; and a start-up the core can call (`EngineMain::chimeraStart`)
  and a configuration hook (`chimera_configure`).

## Building

```
cd waterbox
make -f native.mk -j32 MB=<miniBox>   # build/native/run-native, run-wbx
make -f guest.mk -j32 MB=<miniBox>    # build/guest/core.wbx
./build-package.sh -m <miniBox>       # build/package/sonic3air.chimeraCore
./build-package.sh -r <chimera>       # into <chimera>/build/Cores
```

The scripts find miniBox from `-m` (or `MB=` for make), else `MINIBOX_DIR`,
else (`build-package.sh -r`) the Chimera checkout's; they stop when none
is given. `data-pack.py` packs upstream's
data and scripts into `build/data/s3air-data.bin`, which `data.S` links into
both builds. The package is about 8 MB (24 MB unpacked), without the
remastered soundtrack.

## Testing

```
./waterbox/run-gate.sh -m <miniBox>                                    # the core's tests
./waterbox/run-gate.sh -m <miniBox> -g <Sonic_Knuckles_wSonic3.bin>    # and on the game
./waterbox/tests/run-frontend.sh --chimera-root <chimera> -r <Sonic_Knuckles_wSonic3.bin>
```

Then Chimera's contract tests against the package:

```
<chimera>/build/meson-linux/source/ui/test-packages <folder with the package>
```

The game reads its levels, graphics and music from the Sonic 3 & Knuckles
ROM, which may not be distributed. Without `-g`, `run-gate.sh` runs only the
tests that need no ROM (the refusals of a missing or wrong ROM, the clock, no
network, the data the core carries, the firmware name, the texts), and
`run-frontend.sh` without the ROM only builds the package. Keep the ROM
outside the repository, or in `tests/roms-local/` (`run-frontend.sh` looks
there), which git ignores. The tests make their own test mods
(`tests/make-test-mod.py`) and copy the remastered soundtrack from the
submodule into `build/`, and remove those copies when done.

The native reference's tools for a person debugging:

- `S3AIR_DEBUG=1`: a line a second on stderr (the game running, its frame)
  and the engine's log after Init.
- `S3AIR_CLOCK_START=<seconds since 1970>`: the clock start, as a project
  that names one gives the sandbox (`run-wbx --clock-start`).
- `S3AIR_PACK_REMASTERED=<path>`: the soundtrack files given in the work
  dir's slots packed into an `audioremaster.bin` by upstream's own packer.

## What CI does

`.github/workflows/chimera.yml`: builds Chimera and miniBox, runs
`run-gate.sh` and `run-frontend.sh` (without the ROM), builds the package
stamped with the commit, runs the contract tests, and publishes through
Chimera's `publish-core.yml`. Every test on the game runs where the ROM is.

## Troubleshooting

- "This ROM is not the one Sonic 3 A.I.R. accepts": the file is 4 MiB but
  fails upstream's check (MurmurHash64A of the ROM with byte 0x2001F0 set to
  0x4A, 0x344983FFCFEFF8CB). Only the Steam Sega Classics file works.
- "The game did not start": the scripts did not compile; `S3AIR_DEBUG=1` on
  the native reference prints the engine's log.
- A change to a patched upstream file: make the change, then regenerate that
  file's patch from `git -C extern/sonic3air diff -- <its files>`. Upstream's
  files end lines with CRLF; keep them so, or the patch rewrites every line.
- Native and sandbox differ: something reads the host (a clock, a file, a
  random number). The wraps are in `sources.mk` (`WRAPS`).

## Two CPUs

CI builds and tests the core twice, once for each CPU, on a runner of that
CPU (x86_64 on ubuntu-24.04, aarch64 on ubuntu-24.04-arm): a package is
machine code for one CPU. The linked `core.wbx` goes through miniBox's
`wbx-tp-rewrite.py` on both: on aarch64 its thread-pointer reads are pointed
at the machine's slot, and on x86_64 they are listed for an Intel Mac to
patch as it loads. Only x86_64 has been built and run here.
