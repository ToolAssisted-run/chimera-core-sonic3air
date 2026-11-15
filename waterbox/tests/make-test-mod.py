#!/usr/bin/env python3
"""Writes a small Sonic 3 A.I.R. mod, zipped, for the tests: our own, made at
test time, never anybody else's mod.

The mod is a script mod: its scripts/main.lemon overrides the game's
Standalone.onLevelStart (scripts/standalone/gamecallbacks.lemon), calls the
function it overrides, then sets the ring counter to the value given. So with
the mod the player starts an act with that many rings, and with two such
mods the one loaded last has the last word - which is what tells the order
the mods were loaded in.

usage: make-test-mod.py <out.zip> <unique id> <lives> [--folder <name>]
  --folder puts the mod in a folder of the zip (as most mods are shared);
  without it, mod.json is at the zip's root.
"""
import argparse
import json
import zipfile

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("uid")
ap.add_argument("lives", type=int)
ap.add_argument("--folder", default="")
a = ap.parse_args()

prefix = a.folder + "/" if a.folder else ""
meta = {"Metadata": {"Name": "Chimera test mod " + a.uid, "UniqueID": "chimera-test-" + a.uid,
                     "Author": "chimera-core-sonic3air tests",
                     "Description": "Starts every act with %d lives." % a.lives,
                     "ModVersion": "1.0.0.0", "GameVersion": "26.03.28.0"}}
script = """// Chimera test mod %s: every act starts with %d lives
function void Standalone.onLevelStart()
{
	base.Standalone.onLevelStart()
	lives_counter = %d
}
""" % (a.uid, a.lives, a.lives)
with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in ((prefix + "mod.json", json.dumps(meta, indent="\t")), (prefix + "scripts/main.lemon", script)):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
