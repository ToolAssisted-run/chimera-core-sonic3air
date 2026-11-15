#!/usr/bin/env python3
"""Packs the data the core carries into one blob, linked into the core (data.S).

What goes in is what upstream's own packages hold (its PackageBuilder): the
engine's data folder (enginedata.bin), the game's data folder without its
audio (gamedata.bin) and with its original-sound folder (audiodata.bin), all
under data/ - the game's files over the engine's where both have one - and
the game's scripts under scripts/, which upstream compiles at start-up when
it has no compiled scripts.bin, and oxygenproject.json, which names them. The remastered soundtrack
(data/audio/remastered, upstream's audioremaster.bin) is left out: a project
brings it in its soundtrack slot.

Format: b"S3AIRDAT", a count (u32), then for each file its path's length
(u32), the path (UTF-8), its offset and its size (u64 each, from the blob's
start), then the files' bytes. Everything little-endian, files in path order.

usage: data-pack.py <extern/sonic3air> <out.bin>
"""
import os
import struct
import sys

src, out = sys.argv[1], sys.argv[2]
oxygen = os.path.join(src, "Oxygen")
game = os.path.join(oxygen, "sonic3air")
engine = os.path.join(oxygen, "oxygenengine")

files = {}


def add_tree(base, prefix, skip=()):
    for root, dirs, names in os.walk(base):
        dirs.sort()
        rel_root = os.path.relpath(root, base).replace(os.sep, "/")
        if any(rel_root == s or rel_root.startswith(s + "/") for s in skip):
            continue
        for name in sorted(names):
            rel = name if rel_root == "." else rel_root + "/" + name
            files[prefix + rel] = os.path.join(root, name)


add_tree(os.path.join(engine, "data"), "data/")
add_tree(os.path.join(game, "data"), "data/", skip=("audio/remastered",))
add_tree(os.path.join(game, "scripts"), "scripts/")
# the project file a build from source starts with (S3AIR's EngineDelegate
# reads it when it is there): the main script's name, the ROM's check, the
# 68000 stack translations savestates of the original game need
files["oxygenproject.json"] = os.path.join(game, "oxygenproject.json")

paths = sorted(files)
header = bytearray(b"S3AIRDAT" + struct.pack("<I", len(paths)))
for p in paths:
    header += struct.pack("<I", len(p.encode())) + p.encode() + b"\0" * 16
offset = (len(header) + 63) // 64 * 64
entries = bytearray(b"S3AIRDAT" + struct.pack("<I", len(paths)))
blobs = []
for p in paths:
    data = open(files[p], "rb").read()
    entries += struct.pack("<I", len(p.encode())) + p.encode() + struct.pack("<QQ", offset, len(data))
    blobs.append((offset, data))
    offset = (offset + len(data) + 63) // 64 * 64
tmp = out + ".tmp"
with open(tmp, "wb") as f:
    f.write(entries)
    for off, data in blobs:
        f.write(b"\0" * (off - f.tell()))
        f.write(data)
os.replace(tmp, out)
print(f"{len(paths)} files, {offset} bytes")
