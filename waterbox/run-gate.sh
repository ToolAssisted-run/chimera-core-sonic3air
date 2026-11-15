#!/bin/sh
# run-gate.sh - the Sonic 3 A.I.R. core's tests: the native reference and
# core.wbx are the same machine.
#
# The game's code, scripts and data are compiled into the core, but the game
# reads its levels, graphics and music from the Sonic 3 & Knuckles ROM, which
# may not be distributed: what needs the ROM runs with -g and is skipped
# without it. Every test says what it compared. The tests:
#
# Without the ROM (anywhere, CI included):
#   refusals     no ROM, a ROM of the wrong size, a 4 MiB file that is not
#                the ROM: each says why, in both builds (with the ROM also a
#                mod that is not a zip, a zip with no mod in it, a soundtrack
#                file that is not one)
#   clock        time(), localtime(), SDL's counters, rand and srand are the
#                core's in the guest (the machine's clock start and its steps)
#   no network   the guest links no socket, connect, sendto or getaddrinfo
#   data         the data the core carries has the game's data and scripts,
#                and not the remastered soundtrack
#   firmware     waterbox.config's firmware is the file the core opens
#   texts        every text key the code and the declarations use is in
#                every one of the 26 language tables
#
# With the ROM (-g <Sonic_Knuckles_wSonic3.bin>), on a route from power-on:
# the disclaimer, the SEGA logo, Start on the title, A on Normal Game, A on
# save slot 1 (a new game), the opening, then right through Angel Island act
# 1 with two jumps (4000 frames):
#   equivalence  run-native and run-wbx: every frame's picture, sound, length
#                and lag, the frame count and the memory domains
#   rerecord     the sandbox saved and loaded before every frame = without
#   session      saved in the main menu and again in play, a new host,
#                loaded, finished = without
#   turbo        the first half not drawn: the rest the same, and the first
#                half's pictures really not drawn
#   input        Start on the title opens the main menu; Right held in play
#                moves Sonic right, and without it he stays
#   clock start  another clock start plays differently; native = sandbox at
#                the same start
#   pokes        lives and rings written before a frame are the game's from
#                then on, in both builds
#   mods         a mod (ours, made here) sets the lives: with it 50, without
#                3; with two mods the one loaded last wins (77 for A then B,
#                50 for B then A); native = sandbox with mods
#   soundtrack   the remastered soundtrack (upstream's files, copied from the
#                submodule) changes the sound and nothing else; native =
#                sandbox with it; with the game's Soundtrack option on
#                Original (from save data) the files are not used, and with
#                no files the default (Remastered) plays exactly the original;
#                upstream's audioremaster.bin (made here from the same files
#                by upstream's own packer) sounds as the loose files
#   save data    the new game's save slot exported, given back as a zip and
#                read (the data select shows slot 1 in Angel Island); a zip
#                holding a file that is not save data is refused
#   teeth        the equivalence comparison sees a one-frame difference
#
# usage: run-gate.sh [-m <miniBox dir>] [-g <Sonic_Knuckles_wSonic3.bin>]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
game=""
while getopts "m:g:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		g) game="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
[ -n "$mb" ] || { echo "miniBox not given: pass -m <path> or set MINIBOX_DIR" >&2; exit 2; }
mb="$(cd "$mb" && pwd)"
jobs="$(nproc)"
[ "$jobs" -le 32 ] || jobs=32

echo "== build (miniBox $mb)"
mkdir -p "$root/build"
make -C "$here" -f native.mk MB="$mb" -j"$jobs" > "$root/build/gate-native.log" 2>&1 || {
	tail -20 "$root/build/gate-native.log"; echo "native build failed"; exit 1; }
make -C "$here" -f guest.mk MB="$mb" -j"$jobs" > "$root/build/gate-guest.log" 2>&1 || {
	tail -20 "$root/build/gate-guest.log"; echo "guest build failed"; exit 1; }
native="$root/build/native/run-native"
wbx="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"
ROM=Sonic_Knuckles_wSonic3.bin

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# the digests only (miniBox's and the engine's own log lines go to the same stdout)
digests() { grep -E '^[A-Za-z]+(\[[^]]*\])?=' "$1" || true; }
same() { # same <test> <a> <b> <what>
	digests "$2" > "$2.d"
	digests "$3" > "$3.d"
	if grep -q '^frames=' "$2.d" && cmp -s "$2.d" "$3.d"; then
		pass "$1: $4"
	else
		fail "$1: $4"; diff "$2.d" "$3.d" | head -20
	fi
}
value() { digests "$1" | sed -n "s/^$2=//p"; }
# a traced property's value at a frame (a trace line: frame rate read values...)
at() { awk -v f="$2" -v col="$3" '$1 == f { print $(3 + col) }' "$1"; }
# a work dir with the ROM in it (a copy, never a link)
case_dir() { mkdir -p "$work/$1"; [ -z "$game" ] || cp "$game" "$work/$1/$ROM"; }

echo "== refusals"
refuse() { # refuse <dir> <expected key> <what>
	out="$("$native" "$work/$1" --frames 1 2>&1 || true)"
	outw="$("$wbx" "$core" "$work/$1" --frames 1 2>&1 || true)"
	if echo "$out" | grep -q "loadError=$2" && echo "$outw" | grep -q "loadError=$2"; then
		pass "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p' | tr '\037' ' ')"
	else
		fail "refusals: $3"; echo "$out" | tail -2; echo "$outw" | grep loadError || true
	fi
}
mkdir -p "$work/none" "$work/short" "$work/zeros" "$work/notzip" "$work/nomod" "$work/badst"
head -c 100000 /dev/zero > "$work/short/$ROM"
head -c 4194304 /dev/zero > "$work/zeros/$ROM"
refuse none "error.rom_missing" "no ROM"
refuse short "error.rom_wrong_size" "a ROM of 100000 bytes"
refuse zeros "error.rom_not_s3k" "4 MiB of zeros"
if [ -n "$game" ]; then
	for d in notzip nomod badst; do cp "$game" "$work/$d/$ROM"; done
	printf 'not a zip' > "$work/notzip/m.zip"
	printf '{"mods":["m.zip"]}' > "$work/notzip/slots"
	python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.writestr('readme.txt','no mod here'); z.close()" "$work/nomod/m.zip"
	printf '{"mods":["m.zip"]}' > "$work/nomod/slots"
	printf 'x' > "$work/badst/track.wav"
	printf '{"soundtrack":["track.wav"]}' > "$work/badst/slots"
	refuse notzip "error.mod_not_a_zip" "a mod that is not a zip"
	refuse nomod "error.mod_has_no_mod_json" "a zip with no mod.json in it"
	refuse badst "error.soundtrack_not_one" "a soundtrack file that is not one"
else
	echo "SKIP refusals of mods and the soundtrack: they are checked after the ROM, which needs -g"
fi

echo "== clock"
# The engine reads the time through SDL's counters and time(); the link wraps
# them (sources.mk), and the C library's time() is called once, at Init, for
# the second the machine's clock starts at (the sandbox's, which the project
# sets). With the ROM, the clock start is shown to reach the game below.
missing=""
for w in __wrap_time __wrap_localtime __wrap_SDL_GetTicks __wrap_SDL_GetTicks64 __wrap_SDL_GetPerformanceCounter __wrap_rand __wrap_srand; do
	nm "$core" | grep -qE " T $w\$" || missing="$missing $w"
done
if [ -z "$missing" ]; then
	pass "clock: time(), localtime(), SDL's counters, rand and srand are the core's in the guest (the machine's clock start and its steps)"
else
	fail "clock: not wrapped in the guest:$missing"
fi

echo "== no network"
net="$(nm "$core" | grep -E ' [TtWw] (socket|connect|sendto|recvfrom|getaddrinfo|gethostbyname|bind)$' || true)"
if [ -z "$net" ] && nm -C "$core" | grep -q 'UDPSocket::bindToAnyPort'; then
	pass "no network: the guest links no socket, connect, sendto, recvfrom, bind or getaddrinfo; the engine's sockets are the core's (none opens)"
else
	fail "no network: $(echo "$net" | tr '\n' ' ')"
fi

echo "== data"
blob="$root/build/data/s3air-data.bin"
if python3 - "$blob" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
assert d[:8] == b"S3AIRDAT"
n = struct.unpack_from("<I", d, 8)[0]; p = 12; names = []
for _ in range(n):
    l = struct.unpack_from("<I", d, p)[0]; names.append(d[p + 4:p + 4 + l].decode()); p += 4 + l + 16
need = ["oxygenproject.json", "scripts/main.lemon", "data/metadata.json", "data/images/menu/disclaimer.png", "data/audio/original/audio_default.json"]
missing = [x for x in need if x not in names]
remastered = [x for x in names if x.startswith("data/audio/remastered/")]
print(len(names), "files;", "missing", missing, "remastered", len(remastered))
sys.exit(1 if missing or remastered else 0)
PY
then
	pass "data: the core carries the game's data and scripts ($(stat -c %s "$blob") bytes), without the remastered soundtrack"
else
	fail "data: the blob is not what upstream's packages hold"
fi

echo "== firmware"
fw="$(python3 -c "import json, sys; print(' '.join(f['id'] for f in json.load(open(sys.argv[1]))['firmware']))" "$here/waterbox.config")"
if [ "$fw" = "$ROM" ] && grep -q "#define S3_ROM_ID \"$ROM\"" "$here/s3air-files.h"; then
	pass "firmware: waterbox.config's one firmware, $ROM, is the name the core opens"
else
	fail "firmware: waterbox.config declares '$fw'"
fi

echo "== texts"
if python3 - "$here" <<'PY'
import glob, json, os, re, sys
here = sys.argv[1]
used = set()
for f in glob.glob(os.path.join(here, "*.c")) + glob.glob(os.path.join(here, "*.cpp")):
    used |= set(re.findall(r'"((?:error|prop)\.[a-z0-9_]+)"', open(f).read()))
def walk(v):
    if isinstance(v, dict):
        for k, x in v.items():
            if k in ("display", "description", "title", "help") and isinstance(x, str) and "." in x and " " not in x:
                used.add(x)
            walk(x)
    elif isinstance(v, list):
        for x in v: walk(x)
walk(json.load(open(os.path.join(here, "waterbox.config"))))
walk(json.load(open(os.path.join(here, "file_slots.json"))))
tables = sorted(glob.glob(os.path.join(here, "languages", "*.json")))
bad = 0
for t in tables:
    keys = set(json.load(open(t))["texts"])
    miss = sorted(used - keys)
    if miss:
        print(os.path.basename(t), "lacks", miss); bad += 1
print(len(used), "keys used,", len(tables), "tables")
sys.exit(1 if bad or len(tables) != 26 else 0)
PY
then
	pass "texts: every key the code and the declarations use is in all 26 language tables"
else
	fail "texts"
fi

if [ -n "$game" ]; then
	echo "== Sonic 3 A.I.R. ($game)"
	case_dir game
	base="--press 1000:S:5 --press 1250:A:5 --press 1450:A:5"
	route="$base --press 2800:R:1200 --press 3300:A:10 --press 3600:A:10"
	n=4000
	props="Game.Mode,Game.Zone,Player 1.X,Game.Lives,Game.Rings,Time.Simulation Frame"
	"$native" "$work/game" --frames $n $route --trace "$work/g.t" --trace-props "$props" > "$work/g.n" 2>/dev/null || true
	"$wbx" "$core" "$work/game" --frames $n $route > "$work/g.w" 2>/dev/null || true
	same equivalence "$work/g.w" "$work/g.n" "native = sandbox, $n frames (zone $(at "$work/g.t" $((n - 1)) 2), Sonic at x $(at "$work/g.t" $((n - 1)) 3), $(at "$work/g.t" $((n - 1)) 5) rings, $(at "$work/g.t" $((n - 1)) 6) frames of the game run)"
	"$wbx" "$core" "$work/game" --frames $n $route --rerecord > "$work/g.r" 2> "$work/g.r.err" || true
	same rerecord "$work/g.r" "$work/g.w" "save+load before every frame = straight ($(sed -n 's/stateBytes=//p' "$work/g.r.err") state bytes)"
	for s in 1300 3100; do
		"$wbx" "$core" "$work/game" --frames $n $route --session-at $s > "$work/g.s$s" 2>/dev/null || true
		same session "$work/g.s$s" "$work/g.w" "saved at frame $s, loaded into a new host = straight"
	done

	"$native" "$work/game" --frames $n $route --turbo > "$work/g.turbo" 2>/dev/null || true
	digests "$work/g.turbo" | grep -v '^videoHash=' > "$work/turbo.d"
	digests "$work/g.n" | grep -v '^videoHash=' > "$work/n.d"
	if grep -q '^frames=' "$work/n.d" && cmp -s "$work/turbo.d" "$work/n.d" && [ "$(value "$work/g.turbo" videoHash)" != "$(value "$work/g.n" videoHash)" ]; then
		pass "turbo: first half undrawn - the second half's pictures, the sound, the frames and memory the same; the first half's pictures not"
	else
		fail "turbo"; diff "$work/turbo.d" "$work/n.d" | head
	fi

	# Start on the title (frame 1000) opens the main menu, whose background
	# starts the game anew: the game's own frame count begins again from 0;
	# without Start the title and its demo run on
	"$native" "$work/game" --frames 1200 --trace "$work/still.t" --trace-props "$props" > /dev/null 2>&1 || true
	m0="$(at "$work/g.t" 1050 6)"; s0="$(at "$work/still.t" 1050 6)"
	# Right held from frame 2800 in play: Sonic's x grows; without it, it does not
	"$native" "$work/game" --frames 3000 $base --trace "$work/noright.t" --trace-props "$props" > /dev/null 2>&1 || true
	xr="$(at "$work/g.t" 2999 3)"; x0="$(at "$work/noright.t" 2999 3)"; xs="$(at "$work/g.t" 2799 3)"
	if [ -n "$m0" ] && [ -n "$s0" ] && [ "$m0" -lt 100 ] && [ "$s0" -gt 500 ] && [ -n "$xr" ] && [ "$xr" -gt $((xs + 100)) ] && [ "$x0" -le $((xs + 8)) ]; then
		pass "input: Start on the title opens the main menu, which starts the game anew (its frame count $m0 at frame 1050, against $s0 without Start). Right held from frame 2800 moves Sonic from x $xs to $xr; without it he is at $x0"
	else
		fail "input: game frames at 1050 with Start $m0, without $s0; x at 2800 $xs, at 3000 with Right $xr, without $x0"
	fi

	# The machine's clock start (the project's, which the sandbox keeps):
	# the game takes random numbers from it (the engine seeds them with
	# time() at start-up). Another start plays the route differently, and
	# the same start gives the same machine in both builds.
	S3AIR_CLOCK_START=1893553445 "$native" "$work/game" --frames 3000 $base > "$work/c2030.n" 2>/dev/null || true
	"$wbx" "$core" "$work/game" --frames 3000 $base --clock-start 1893553445 > "$work/c2030.w" 2>/dev/null || true
	"$native" "$work/game" --frames 3000 $base > "$work/c2017.n" 2>/dev/null || true
	if [ -n "$(value "$work/c2030.n" 'domain\[68K RAM\]')" ] && [ "$(value "$work/c2030.n" 'domain\[68K RAM\]')" != "$(value "$work/c2017.n" 'domain\[68K RAM\]')" ]; then
		same "clock start" "$work/c2030.w" "$work/c2030.n" "a clock starting at 2030-01-02 03:04:05 plays the route differently from 2017-05-27 12:44:28 (the default); native = sandbox at the same start"
	else
		fail "clock start: 2030 and 2017 play the same"
	fi

	vp="Game.Lives,Game.Rings"
	"$native" "$work/game" --frames 3041 $route --poke "3000:Game.Lives=9" --poke "3000:Game.Rings=42" --trace "$work/v.t" --trace-props "$vp" > /dev/null 2>&1 || true
	"$wbx" "$core" "$work/game" --frames 3041 $route --poke "3000:Game.Lives=9" --poke "3000:Game.Rings=42" --trace "$work/v.w" --trace-props "$vp" > /dev/null 2>&1 || true
	plain="$(at "$work/g.t" 2999 4)"; poked="$(at "$work/v.t" 3000 1)"; lives40="$(at "$work/v.t" 3040 1)"; rings40="$(at "$work/v.t" 3040 2)"
	if [ "$plain" = 3 ] && [ "$poked" = 9 ] && [ "$lives40" = 9 ] && [ -n "$rings40" ] && [ "$rings40" -ge 42 ] && cmp -s "$work/v.t" "$work/v.w"; then
		pass "pokes: lives 3; lives 9 and rings 42 written before frame 3000 are the game's 40 frames on (lives $lives40, rings $rings40); both builds the same"
	else
		fail "pokes: lives before $plain, after $poked, 40 frames on lives $lives40 rings $rings40"
	fi

	# Mods: zips of our own test mods (tests/make-test-mod.py), in the slot's order
	mods() { # mods <dir> <zip>... (the zips are in $work/modzips)
		d="$1"; shift
		case_dir "$d"
		list=""
		for z in "$@"; do cp "$work/modzips/$z" "$work/$d/"; list="$list${list:+,}\"$z\""; done
		printf '{"mods":[%s]}' "$list" > "$work/$d/slots"
	}
	mkdir -p "$work/modzips"
	python3 "$here/tests/make-test-mod.py" "$work/modzips/livesA.zip" a 50 --folder "Lives A"
	python3 "$here/tests/make-test-mod.py" "$work/modzips/livesB.zip" b 77
	mods modA livesA.zip; mods modAB livesA.zip livesB.zip; mods modBA livesB.zip livesA.zip
	ml=""
	for d in modA modAB modBA; do
		"$native" "$work/$d" --frames 3000 $base --trace "$work/$d.t" --trace-props "$vp" > "$work/$d.n" 2>/dev/null || true
		ml="$ml $(at "$work/$d.t" 2999 1)"
	done
	"$wbx" "$core" "$work/modAB" --frames 3000 $base > "$work/modAB.w" 2>/dev/null || true
	if [ "$ml" = " 50 77 50" ] && [ "$(at "$work/g.t" 2999 4)" = 3 ]; then
		same mods "$work/modAB.w" "$work/modAB.n" "lives with no mod 3; with mod A 50; with A then B 77; with B then A 50 (the one loaded last wins); native = sandbox with two mods"
	else
		fail "mods: lives with A, A then B, B then A:$ml (without: $(at "$work/g.t" 2999 4))"
	fi

	# The remastered soundtrack: upstream's own files, copied from the submodule
	remastered="$root/extern/sonic3air/Oxygen/sonic3air/data/audio/remastered"
	case_dir st
	cp "$remastered"/* "$work/st/"
	python3 -c "import json,os,sys; d=sys.argv[1]; json.dump({'soundtrack': sorted(n for n in os.listdir(d) if n.endswith(('.ogg','.json')))}, open(os.path.join(d,'slots'),'w'))" "$work/st"
	"$native" "$work/st" --frames 3000 $base > "$work/st.n" 2>/dev/null || true
	"$wbx" "$core" "$work/st" --frames 3000 $base > "$work/st.w" 2>/dev/null || true
	"$native" "$work/game" --frames 3000 $base > "$work/nost.n" 2>/dev/null || true
	digests "$work/st.n" | grep -v '^audioHash=' > "$work/st.na"
	digests "$work/nost.n" | grep -v '^audioHash=' > "$work/nost.na"
	if grep -q '^frames=' "$work/st.na" && cmp -s "$work/st.na" "$work/nost.na" && [ "$(value "$work/st.n" audioHash)" != "$(value "$work/nost.n" audioHash)" ]; then
		same soundtrack "$work/st.w" "$work/st.n" "the remastered soundtrack ($(ls "$work/st" | grep -c '\.ogg$') Ogg files) changes the sound and nothing else (pictures, memory, frames the same); native = sandbox with it"
	else
		fail "soundtrack: with the files and without, the sound or the rest"; diff "$work/st.na" "$work/nost.na" | head -5
	fi

	# The soundtrack as an installation has it: audioremaster.bin, made here
	# from the same files by upstream's own packer (the native reference's
	# S3AIR_PACK_REMASTERED), sounds as the loose files do
	case_dir stbin
	S3AIR_PACK_REMASTERED="$work/stbin/audioremaster.bin" "$native" "$work/st" --frames 1 > "$work/stbin.pack" 2>&1 || true
	printf '{"soundtrack":["audioremaster.bin"]}' > "$work/stbin/slots"
	"$native" "$work/stbin" --frames 3000 $base > "$work/stbin.n" 2>/dev/null || true
	"$wbx" "$core" "$work/stbin" --frames 3000 $base > "$work/stbin.w" 2>/dev/null || true
	if [ -s "$work/stbin/audioremaster.bin" ] && [ "$(value "$work/stbin.n" audioHash)" = "$(value "$work/st.n" audioHash)" ] &&
	   [ "$(value "$work/stbin.n" audioHash)" != "$(value "$work/nost.n" audioHash)" ]; then
		same "soundtrack package" "$work/stbin.w" "$work/stbin.n" "audioremaster.bin ($(stat -c %s "$work/stbin/audioremaster.bin") bytes, upstream's package format) sounds as the loose files, and not as no soundtrack; native = sandbox with it"
	else
		fail "soundtrack package: $(grep -o 'packed.*' "$work/stbin.pack"); sound $(value "$work/stbin.n" audioHash), loose files $(value "$work/st.n" audioHash), none $(value "$work/nost.n" audioHash)"
	fi

	# Save data: the new game's save slot out, and back in
	case_dir sd
	mkdir -p "$work/sd.out"
	"$native" "$work/game" --frames 3000 $base --savedata-out "$work/sd.out" > "$work/sd.n" 2>/dev/null || true
	zipit() { python3 - "$@" <<'PY'
import json, os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
original = len(sys.argv) > 3 and sys.argv[3] == "original"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for r, _, names in os.walk(src):
        for n in sorted(names):
            p = os.path.join(r, n); rel = os.path.relpath(p, src).replace(os.sep, "/"); data = open(p, "rb").read()
            if original and rel == "settings.json":
                j = json.loads(data); j.setdefault("Audio", {})["ActiveSoundtrack"] = 0; data = json.dumps(j, indent=1).encode()
            z.writestr(rel, data)
PY
	}
	zipit "$work/sd.out" "$work/sd/saves.zip"
	printf '{"savedata":["saves.zip"]}' > "$work/sd/slots"
	sel="--press 1000:S:5 --press 1250:A:5"
	"$native" "$work/game" --frames 1400 $sel > "$work/fresh.n" 2>/dev/null || true
	"$native" "$work/sd" --frames 1400 $sel > "$work/seeded.n" 2>/dev/null || true
	"$wbx" "$core" "$work/sd" --frames 1400 $sel > "$work/seeded.w" 2>/dev/null || true
	mkdir -p "$work/sd.back"
	"$native" "$work/sd" --frames 1 --savedata-out "$work/sd.back" > /dev/null 2>&1 || true
	case_dir sdbad
	python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.writestr('mods/x.zip','x'); z.close()" "$work/sdbad/saves.zip"
	printf '{"savedata":["saves.zip"]}' > "$work/sdbad/slots"
	"$native" "$work/sdbad" --frames 1 > "$work/sdbad.n" 2>&1 || true
	if [ ! -s "$work/sd.out/storage/s3air_saveslots.bin" ]; then
		fail "save data: the new game wrote no storage/s3air_saveslots.bin to export ($(ls -R "$work/sd.out" | tr '\n' ' '))"
	elif ! cmp -s "$work/sd.out/storage/s3air_saveslots.bin" "$work/sd.back/storage/s3air_saveslots.bin"; then
		fail "save data: a project given the save slots does not start with those bytes"
	elif [ "$(value "$work/fresh.n" tailVideoHash)" = "$(value "$work/seeded.n" tailVideoHash)" ]; then
		fail "save data: the data select with the save given is the fresh game's: the game did not read it"
	elif ! grep -q "loadError=error.savedata_not_one_this" "$work/sdbad.n"; then
		fail "save data: a zip with mods/x.zip in it was not refused ($(grep loadError "$work/sdbad.n" || true))"
	else
		pass "save data: $(find "$work/sd.out" -type f | wc -l) files exported (settings, input settings, save slots), given back as a zip and read (the data select differs from a fresh game's), a zip holding mods/x.zip refused"
		same "save data" "$work/seeded.w" "$work/seeded.n" "save data given: native = sandbox, 1400 frames"
	fi

	# The game's Soundtrack option (in its settings, given as save data): on
	# Original the files are not used; with no files, the default (Remastered)
	# plays exactly what Original plays
	case_dir orig; case_dir origst
	zipit "$work/sd.out" "$work/orig/saves.zip" original
	cp "$work/orig/saves.zip" "$work/origst/"
	cp "$remastered"/* "$work/origst/"
	printf '{"savedata":["saves.zip"]}' > "$work/orig/slots"
	python3 -c "import json,os,sys; d=sys.argv[1]; json.dump({'savedata':['saves.zip'],'soundtrack': sorted(n for n in os.listdir(d) if n.endswith(('.ogg','.json')))}, open(os.path.join(d,'slots'),'w'))" "$work/origst"
	"$native" "$work/orig" --frames 3000 $base > "$work/orig.n" 2>/dev/null || true
	"$native" "$work/origst" --frames 3000 $base > "$work/origst.n" 2>/dev/null || true
	a_orig="$(value "$work/orig.n" audioHash)"; a_origst="$(value "$work/origst.n" audioHash)"; a_def="$(value "$work/nost.n" audioHash)"
	if [ -n "$a_orig" ] && [ "$a_orig" = "$a_origst" ] && [ "$a_orig" = "$a_def" ] && [ "$a_def" != "$(value "$work/st.n" audioHash)" ]; then
		pass "soundtrack option: Original with the files sounds as Original without them; Remastered (the default) with no files sounds exactly as Original"
	else
		fail "soundtrack option: Original $a_orig, Original with files $a_origst, default without files $a_def, default with files $(value "$work/st.n" audioHash)"
	fi

	# a jump at frame 3500, running in Angel Island
	"$native" "$work/game" --frames $n $route --press 3500:A:1 > "$work/teeth" 2>/dev/null || true
	if digests "$work/teeth" | cmp -s - "$work/g.n.d"; then
		fail "teeth: a route one frame different digests the same"
	else
		pass "teeth: a route one frame different digests differently"
	fi
	# the gate's copies of the remastered soundtrack (130 MB) go when done
	rm -rf "$work/st" "$work/origst" "$work/stbin"
else
	echo "SKIP game: no Sonic 3 & Knuckles ROM given (-g <$ROM>); it is never in the repository"
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all tests passed"
else
	echo "gate: $fails test(s) failed"
	exit 1
fi
