#!/bin/bash
# The frontend half of the tests: the package, as Chimera loads it, playing
# Sonic 3 A.I.R. with the Sonic 3 & Knuckles ROM (-r, or
# tests/roms-local/Sonic_Knuckles_wSonic3.bin; never in the repository):
# power-on, the title, Normal Game, a new game in slot 1 and into Angel
# Island act 1, 3200 frames. Without the ROM only the package is built.
#
# - chimera-run (the engine, no frontend):
#   engine:movie   the route as a movie made into a project, the ROM its
#                  firmware: the 68K RAM and the last picture are the native
#                  reference's
#   engine:mods    a project with two mods of our own in its Mods slot (made
#                  here): the project names both zips, in their order, by
#                  SHA-1; the lives are the last mod's; the 68K RAM is the
#                  native reference's with the same zips; the same project
#                  with the zips the other way round plays otherwise
#   engine:replay  the mods project's movie played again in a new process,
#                  with a save and load around every frame, and from a state
#                  saved half way and loaded into another process: the same
#                  machine at the end
# - Chimera's window, run headless (it needs no display):
#   gui:project    the mods project opened and played through Lua, its
#                  domains compared with the native reference; the game's
#                  timer in the saved project's headers
#   gui:game-properties  game.list/get/set against the same bytes
#   gui:refusal    the core's own refusal of a file that is not the ROM
#                  reaching the frontend
#   gui:keybinds   the package's keybinds becoming the defaults
#   gui:writes-nothing-home
#
# Nothing is written under the Chimera checkout: the config, the data home,
# the Base path and the sandbox's log are all in build/frontend.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [-m <miniBox dir>] [-r <Sonic_Knuckles_wSonic3.bin>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
chimera_root=""
mb="${MINIBOX_DIR:-}"
ROM=Sonic_Knuckles_wSonic3.bin
rom="$root/tests/roms-local/$ROM"
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		-m) mb="$2"; shift ;;
		-r) rom="$2"; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "chimera checkout not found; pass --chimera-root" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"
[ -n "$mb" ] || mb="$chimera_root/extern/chimera-common-minibox"

chimera_exe="$chimera_root/build/meson-linux/source/ui/chimera"
crun="$chimera_root/build/dll/chimera-run"
rn="$root/build/native/run-native"

work="$root/build/frontend"
rm -rf "$work"
mkdir -p "$work/data"

ok=0
failed=0
skipped=0
report() {
	printf "%-26s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-26s %-6s %s\n" "Check" "Result" "Detail"
printf "%-26s %-6s %s\n" "-----" "------" "------"

[ -x "$rn" ] || { echo "native reference not built (make -f native.mk)" >&2; exit 1; }

# ---------------------------------------------------------------- the package
if sh "$wb/build-package.sh" -m "$mb" -o "$work/pkg" > "$work/package.log" 2>&1; then
	report "package" PASS "$(grep 'package sha1' "$work/package.log")"
else
	report "package" FAIL "see build/frontend/package.log"; echo; echo "$ok ok, $failed failed"; exit 1
fi
pkg="$work/pkg/sonic3air.chimeraCore"

if [ ! -f "$rom" ]; then
	report "game" SKIP "no Sonic 3 & Knuckles ROM at $rom (-r); it is never in the repository"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; exit 0
fi

frames=3200
presses="1000:S:5 1250:A:5 1450:A:5 2800:R:400 3000:A:10"
python3 "$here/make-movie.py" "$pkg" "$work/route.movie" "$frames" $presses
args=""; for p in $presses; do args="$args --press $p"; done
python3 "$here/make-test-mod.py" "$work/livesA.zip" a 50 --folder "Lives A"
python3 "$here/make-test-mod.py" "$work/livesB.zip" b 77

# the native references: the route alone, and with the mods A then B
mkdir -p "$work/native" "$work/nativemods"
cp "$rom" "$work/native/$ROM"; cp "$rom" "$work/nativemods/$ROM"
cp "$work/livesA.zip" "$work/livesB.zip" "$work/nativemods/"
printf '{"mods":["livesA.zip","livesB.zip"]}' > "$work/nativemods/slots"
rom_copy="$work/native/$ROM"
# shellcheck disable=SC2086
"$rn" "$work/native" --frames "$frames" $args --dump-domain "68K RAM" "$work/native.ram.bin" \
	--screenshot "$((frames - 1)):$work/native.last.tga" > /dev/null 2>&1
# shellcheck disable=SC2086
"$rn" "$work/nativemods" --frames "$frames" $args --dump-domain "68K RAM" "$work/nativemods.ram.bin" \
	--screenshot "$((frames - 1)):$work/nativemods.last.tga" > /dev/null 2>&1
lives_of() { python3 -c "import sys; print(open(sys.argv[1],'rb').read()[0xFE12])" "$1" 2>/dev/null; }
meta() { sed -n "s/^$2=//p" "$1" 2>/dev/null; }

# ---------------------------------------------------------------- chimera-run
if [ ! -x "$crun" ]; then
	report "engine" SKIP "no chimera-run at $crun"
else
	newproj() { # newproj <name> [--file ...]: a project of the route, the ROM its firmware
		local name="$1"; shift
		( cd "$work" && "$crun" --new-project "$work/$name.chimeraProject" "$pkg" --movie "$work/route.movie" \
			--firmware "$ROM=$rom_copy" "$@" > "$work/$name.new.log" 2>&1 )
	}
	runproj() { # runproj <project> <tag> [options]: run it, dumps and meta under the tag
		local p="$1" tag="$2"; shift 2
		( cd "$work" && "$crun" --project "$work/$p.chimeraProject" "$pkg" --firmware "$ROM=$rom_copy" \
			--meta "$work/$tag.meta" "$@" > "$work/$tag.log" 2>&1 )
	}

	newproj movie
	runproj movie run --dump "68K RAM=$work/run.ram.bin" --screenshot "$((frames - 1))=$work/run.last.tga"
	if grep -q '^status=OK' "$work/run.meta" 2>/dev/null && cmp -s "$work/native.ram.bin" "$work/run.ram.bin" &&
	   python3 "$here/compare-pictures.py" "$work/native.last.tga" "$work/run.last.tga" > "$work/run.pic" 2>&1; then
		report "engine:movie" PASS "$frames frames from a movie made into a project: the 68K RAM and the last picture are the reference's (lives $(lives_of "$work/run.ram.bin"))"
	else
		report "engine:movie" FAIL "$(meta "$work/run.meta" status) $(meta "$work/run.meta" detail) $(cat "$work/run.pic" 2>/dev/null) (build/frontend/run.log)"
	fi

	# Mods: the project records each zip, in order, by its SHA-1
	newproj mods --file "mods=$work/livesA.zip" --file "mods=$work/livesB.zip"
	newproj modsrev --file "mods=$work/livesB.zip" --file "mods=$work/livesA.zip"
	runproj mods m1 --dump "68K RAM=$work/m1.ram.bin" --screenshot "$((frames - 1))=$work/m1.last.tga"
	runproj modsrev mr --dump "68K RAM=$work/mr.ram.bin"
	named="$(python3 - "$work/mods.chimeraProject" "$(sha1sum "$work/livesA.zip" | cut -c1-40)" "$(sha1sum "$work/livesB.zip" | cut -c1-40)" <<'PY' 2>&1
import json, sys, zipfile
files = json.loads(zipfile.ZipFile(sys.argv[1]).read("project.json"))["files"]
mods = [(f["name"], f["sha1"].lower()) for f in files if f.get("slot") == "mods"]
print("yes" if mods == [("livesA.zip", sys.argv[2]), ("livesB.zip", sys.argv[3])] else "no: %s" % mods)
PY
)"
	if grep -q '^status=OK' "$work/m1.meta" 2>/dev/null && [ "$named" = yes ] && [ "$(lives_of "$work/m1.ram.bin")" = 77 ] &&
	   [ "$(lives_of "$work/mr.ram.bin")" = 50 ] && cmp -s "$work/nativemods.ram.bin" "$work/m1.ram.bin" &&
	   python3 "$here/compare-pictures.py" "$work/nativemods.last.tga" "$work/m1.last.tga" > "$work/m1.pic" 2>&1; then
		report "engine:mods" PASS "the project names livesA.zip then livesB.zip by SHA-1; lives 77 (B loaded last), 50 with the zips the other way round; the 68K RAM and the last picture are the reference's with the same zips"
	else
		report "engine:mods" FAIL "named $named; lives $(lives_of "$work/m1.ram.bin") / reversed $(lives_of "$work/mr.ram.bin"); RAM $(cmp "$work/nativemods.ram.bin" "$work/m1.ram.bin" 2>&1 | head -1); $(cat "$work/m1.pic" 2>/dev/null); $(meta "$work/m1.meta" detail) (build/frontend/m1.log)"
	fi

	# The movie made with the mods replays the same: in another process, with
	# a save and a load around every frame, and from a state carried over
	runproj mods m2 --dump "68K RAM=$work/m2.ram.bin" --screenshot "$((frames - 1))=$work/m2.last.tga"
	runproj mods m3 --rerecord --dump "68K RAM=$work/m3.ram.bin"
	runproj mods m4 --frames 1600 --save-state "1599=$work/m4.state"
	# the state goes on with the rest of the movie, a project of its own (a
	# project's movie starts at its first frame)
	tail -n +1601 "$work/route.movie" > "$work/rest.movie"
	( cd "$work" && "$crun" --new-project "$work/modsrest.chimeraProject" "$pkg" --movie "$work/rest.movie" \
		--firmware "$ROM=$rom_copy" --file "mods=$work/livesA.zip" --file "mods=$work/livesB.zip" > "$work/modsrest.new.log" 2>&1 )
	runproj modsrest m5 --state "$work/m4.state" --dump "68K RAM=$work/m5.ram.bin"
	if grep -q '^status=OK' "$work/m2.meta" && cmp -s "$work/m1.ram.bin" "$work/m2.ram.bin" &&
	   python3 "$here/compare-pictures.py" "$work/m1.last.tga" "$work/m2.last.tga" > /dev/null 2>&1 &&
	   cmp -s "$work/m1.ram.bin" "$work/m3.ram.bin" && [ -s "$work/m4.state" ] && cmp -s "$work/m1.ram.bin" "$work/m5.ram.bin"; then
		report "engine:replay" PASS "the mods project replayed in another process, with a save and load around every frame, and from a state of frame 1600 loaded in another process ($(stat -c %s "$work/m4.state") bytes): the same 68K RAM at the end"
	else
		report "engine:replay" FAIL "again $(cmp -s "$work/m1.ram.bin" "$work/m2.ram.bin" && echo same || echo differs); rerecord $(cmp -s "$work/m1.ram.bin" "$work/m3.ram.bin" && echo same || echo differs) $(meta "$work/m3.meta" detail); state $(cmp -s "$work/m1.ram.bin" "$work/m5.ram.bin" && echo same || echo differs) $(meta "$work/m5.meta" detail)"
	fi
fi

# ---------------------------------------------------------------- Chimera itself
if [ ! -x "$chimera_exe" ] || [ ! -f "$work/mods.chimeraProject" ]; then
	report "frontend" SKIP "Chimera not built ($chimera_exe), or no project made by chimera-run"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; [ "$failed" -eq 0 ]; exit
fi
# Chimera's window, run headless: its settings, cache and recovery files the
# run's own, and nothing carried over from a Chimera installed on this machine
export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$chimera_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" CHIMERA_QT_RECOVERY="$work/qt-recovery" \
	CHIMERA_QT_HASHES="$work/qt-file-hashes.txt" CHIMERA_QT_CHIMERA_SETTINGS="$work/no-chimera-config.ini"
export MINIBOX_LOG="$work/minibox.log"

config="$work/settings.json"
python3 - "$config" "$work" <<'EOF'
import json, sys
json.dump({"paths.base": sys.argv[2]}, open(sys.argv[1], "w"), indent=2)
EOF
touch "$work/configured"

gui() {
	# gui <tag> <config> <project> <frames> [rom]
	local tag="$1" cfg="$2" project="$3" nframes="$4" fw="${5:-$rom_copy}"
	mkdir -p "$work/$tag"
	printf 'frames=%s\nout=%s\nmeta=%s\nsave=%s\n' "$nframes" "$work/$tag" "$work/$tag/meta.txt" "${GUI_SAVE:-0}" > "$work/$tag/job.txt"
	( cd "$work" && MINIHAWK_JOB="$work/$tag/job.txt" timeout 600 "$chimera_exe" --headless "--config=$cfg" \
		"--core=$pkg" "--firmware=$ROM=$fw" "--project=$project" "--lua=$here/frontend.lua" ) > "$work/$tag.log" 2>&1
	echo $?
}
gmeta() { sed -n "s/^$2=//p" "$work/$1/meta.txt" 2>/dev/null; }

cp "$work/mods.chimeraProject" "$work/gui.chimeraProject"
GUI_SAVE=1 gui gui "$config" "$work/gui.chimeraProject" "$frames" > /dev/null
if [ "$(gmeta gui status)" = "OK" ] && cmp -s "$work/gui/ram.bin" "$work/nativemods.ram.bin"; then
	report "gui:project" PASS "the mods project opened in Chimera: after $(gmeta gui frames) frames ($(gmeta gui lag) lag) the 68K RAM is the reference's (lives $(lives_of "$work/gui/ram.bin"))"
else
	report "gui:project" FAIL "status $(gmeta gui status) $(gmeta gui detail) frames $(gmeta gui frames) (build/frontend/gui.log)"
fi

# the game's own timer (the table's gameTimer, Time.IGT Ms): Chimera reads it
# after every frame, and the project saved at the movie's end carries it
"$rn" "$work/nativemods" --frames "$frames" $args --trace "$work/igt.trace" --trace-props "Time.IGT Ms" > /dev/null 2>&1
want_ms="$(awk -v s=$((frames - 1)) '$1 == s { print $4 }' "$work/igt.trace")"
hdr() { python3 -c "import json,sys,zipfile; print(json.loads(zipfile.ZipFile(sys.argv[1]).read('project.json'))['headers'].get(sys.argv[2], ''))" "$work/gui.chimeraProject" "$1"; }
want_text="$(printf '%02d:%02d.%03d' $((want_ms / 60000)) $((want_ms / 1000 % 60)) $((want_ms % 1000)))"
if [ "$(gmeta gui saved)" = "1" ] && [ -n "$want_ms" ] && [ "$want_ms" -gt 0 ] && [ "$(gmeta gui game_time_ms)" = "$want_ms" ] &&
   [ "$(hdr GameTimeMs)" = "$want_ms" ] && [ "$(hdr GameTime)" = "$want_text" ] && [ "$(hdr GameTimeFrame)" = "$frames" ]; then
	report "gui:game-time" PASS "the game's timer $want_text after $frames frames: game.get, and the saved project's GameTime headers, are the reference's"
else
	report "gui:game-time" FAIL "saved $(gmeta gui saved); reference $want_ms ms; lua $(gmeta gui game_time_ms); headers $(hdr GameTimeMs) '$(hdr GameTime)' @ $(hdr GameTimeFrame)"
fi

nprops="$("$rn" "$work/native" --frames 1 --props-json "$work/props.json" > /dev/null 2>&1; python3 -c "import json; print(len(json.load(open('$work/props.json'))['properties']))")"
if [ "$(gmeta gui game_list)" = "absent" ]; then
	report "gui:game-properties" SKIP "this Chimera has no game.* library"
elif [ "$(gmeta gui game_list)" = "$nprops" ] && [ "$(gmeta gui game_lives)" = "$(gmeta gui game_lives_byte)" ] && [ "$(gmeta gui game_lives)" = 77 ] &&
     [ "$(gmeta gui game_x)" = "$(gmeta gui game_x_bytes)" ] && [ "$(gmeta gui game_set)" = "true" ] && [ "$(gmeta gui game_lives_after_set)" = "9" ] &&
     [ "$(gmeta gui game_unknown)" = "nil" ] && [ "$(gmeta gui game_time_set)" = "false" ]; then
	report "gui:game-properties" PASS "game.list() has the core's $nprops; Game.Lives by name is its byte (77, the mods'); Player 1.X is its big-endian bytes ($(gmeta gui game_x)); lives set to 9 took; the read-only timer refused a set"
else
	report "gui:game-properties" FAIL "list $(gmeta gui game_list)/$nprops, lives $(gmeta gui game_lives)/$(gmeta gui game_lives_byte), x $(gmeta gui game_x)/$(gmeta gui game_x_bytes), set $(gmeta gui game_set)->$(gmeta gui game_lives_after_set), timer set $(gmeta gui game_time_set) $(gmeta gui game_error)"
fi

# the core's own refusal reaches the frontend: a file of zeros in the ROM's
# place (a game core's project may pin a file of its own instead of the
# original, as the wizard lets it; the core refuses this one)
head -c 4194304 /dev/zero > "$work/zeros.bin"
( cd "$work" && "$crun" --new-project "$work/zeros.chimeraProject" "$pkg" --movie "$work/route.movie" \
	--firmware "$ROM=$work/zeros.bin" > "$work/zeros.new.log" 2>&1 )
code="$(gui refuse "$config" "$work/zeros.chimeraProject" 10 "$work/zeros.bin")"
if grep -q "This ROM is not the one Sonic 3 A.I.R. accepts" "$work/refuse.log"; then
	report "gui:refusal" PASS "the core's load error is what Chimera shows (headless: exit $code with the text)"
else
	report "gui:refusal" FAIL "exit $code; $(grep -m1 -i 'rom\|error' "$work/refuse.log" | cut -c1-120)"
fi

# the bindings the package ships are the machine's when nobody changed them
printf 'out=%s/keys.controls.json\nmeta=%s/keys.meta.txt\n' "$work" "$work" > "$work/job.keys.txt"
rm -f "$work/keys.controls.json" "$work/keys.meta.txt"
( cd "$work" && MINIHAWK_JOB="$work/job.keys.txt" timeout 300 "$chimera_exe" --headless "--config=$work/settings.keys.json" \
	"--core=$pkg" "--firmware=$ROM=$rom_copy" "--project=$work/gui.chimeraProject" "--lua=$here/dump-controls.lua" ) > "$work/keys.log" 2>&1
if grep -q "^status=OK" "$work/keys.meta.txt" 2>/dev/null && python3 "$here/check-keybinds.py" "$work/keys.controls.json" "$wb/default_keybinds.json" "Sonic 3 A.I.R." > "$work/keys.txt" 2>&1; then
	report "gui:keybinds" PASS "$(cat "$work/keys.txt")"
else
	report "gui:keybinds" FAIL "$(head -1 "$work/keys.txt" 2>/dev/null) (build/frontend/keys.log)"
fi

# nothing written under the Chimera checkout
if [ -z "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/configured" \( -name 'config*.ini' -o -name 'Movies' -o -name 'Firmware' -o -name 'CoreCache' -o -name 'ChimeraMono_last*' \) 2>/dev/null)" ]; then
	report "gui:writes-nothing-home" PASS "after the first start: config, data home, Base path and logs all in build/frontend"
else
	report "gui:writes-nothing-home" FAIL "$(find "$chimera_root/build" -maxdepth 2 -newer "$work/configured" | head -3 | tr '\n' ' ')"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
