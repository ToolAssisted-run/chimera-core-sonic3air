#!/usr/bin/env python3
"""Writes a movie for chimera-run (--new-project ... --movie): one line a
frame, each player's buttons in the package's order, a button held written
as its letter (the package's mnemonics) and one not held as '.', the
players' groups between '|'.

The presses are given as run-native's: FRAME:KEYS[:LENGTH], player 1's
keys as gate_key_index has them (U D L R A B C X Y Z S M).

usage: make-movie.py <package> <out.txt> <frames> [FRAME:KEYS[:LENGTH]]...
"""
import json
import sys
import zipfile

package, out, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
cfg = json.loads(zipfile.ZipFile(package).read("waterbox.config"))
players = cfg["input"]["players"]
mnemonics = cfg["input"]["mnemonics"]
held = [set() for _ in range(frames)]
for spec in sys.argv[4:]:
    parts = spec.split(":")
    at, keys = int(parts[0]), parts[1]
    length = int(parts[2]) if len(parts) > 2 else 1
    for f in range(at, min(at + length, frames)):
        held[f] |= set(keys)
rows = []
for f in range(frames):
    groups = []
    for p, buttons in enumerate(players):
        cells = ""
        for b in buttons:
            letter = mnemonics[b.split(" ", 1)[1]]
            cells += letter if p == 0 and letter in held[f] else "."
        groups.append(cells)
    rows.append("|" + "|".join(groups) + "|")
open(out, "w").write("\n".join(rows) + "\n")
