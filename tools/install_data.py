#!/usr/bin/env python3
"""Writes the data files that come out of the original game: tools/install_data.py GAME_DIR OUT_DATA_DIR
(game.exe's data segment starts at EXE image offset 0x8c20; see docs/formats.md). Used by install.py; nothing here ships the original's content."""
import json, os, struct, sys


def extract(game_dir, out_dir):
    d = open(os.path.join(game_dir, "game.exe"), "rb").read()
    hdr = struct.unpack_from("<H", d, 8)[0] * 16
    img, B = d[hdr:], 0x8c20
    ds = lambda off, n: img[B + off:B + off + n]
    os.makedirs(out_dir, exist_ok=True)

    def put(name, obj):
        with open(os.path.join(out_dir, name), "w") as f:
            json.dump(obj, f)

    w, h = struct.unpack("<HH", ds(0x5b60, 4)); cells = list(ds(0x5b64, w * h))
    put("world_map.json", {"width": w, "height": h, "comment": "block numbers into stokens.dat; row-major; written by install.py from game.exe",
                           "blocks": [cells[y * w:(y + 1) * w] for y in range(h)]})
    put("tile_attributes.json", {"comment": "one byte per wtiles tile: 0 open, 1 blocks movement, 0x80 special", "tiles": list(ds(0x1b3e, 0x1c76 - 0x1b3e))})
    ptr = struct.unpack("<120H", ds(0x1792, 240))
    msgs = [img[B + p:img.index(b"\0", B + p)].decode("latin1") for p in ptr]
    put("messages.json", {"comment": "Message-box texts from game.exe (pointer table at data 0x1792).", "messages": msgs})
    t = struct.unpack("<21H", ds(0x658c, 42)); levels = {}
    for i, lv in enumerate(range(2, 8)):
        off, lw, lh = t[3 + i * 3:6 + i * 3]; raw = ds(off, lw * lh)
        levels[str(lv)] = {"width": lw, "height": lh, "data_offset": hex(off), "cells": [list(raw[r * lw:(r + 1) * lw]) for r in range(lh)]}
    put("levels_2_to_7.json", {"comment": "Interior level maps from game.exe (level table at data 0x658c). Cells are utiles tile numbers.", "levels": levels})
    tw = struct.unpack("<480H", ds(0x57A0, 960)); ar = ds(0x3F28, 480)
    put("quest_map.json", {"comment": "Quest map from game.exe: terrain (0x57A0), armies (0x3F28), factor (0x1C9A).",
                           "terrain": [list(tw[r * 20:r * 20 + 20]) for r in range(24)], "armies": [list(ar[r * 20:r * 20 + 20]) for r in range(24)], "factor": list(ds(0x1C9A, 128))})
    ns, ew = ds(0x225e, 304 * 3), ds(0x1eba, 304 * 3)
    put("strip_tables.json", {"comment": "Outdoor combat-strip tables from game.exe (0x225e north-south, 0x1eba east-west).",
                              "north_south": [list(ns[i * 3:i * 3 + 3]) for i in range(304)], "east_west": [list(ew[i * 3:i * 3 + 3]) for i in range(304)]})
    put("interior_tables.json", {"comment": "Interior side-view tables from game.exe (0x1dee, 0x1e54, 0x2668, 0x2602).",
                                 "cell_to_block_ew": list(ds(0x1dee, 102)), "cell_to_block_ns": list(ds(0x1e54, 102)), "exits": list(ds(0x2668, 128)), "secret_flags": list(ds(0x2602, 102))})


if __name__ == "__main__":
    extract(sys.argv[1], sys.argv[2])
