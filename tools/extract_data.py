#!/usr/bin/env python3
"""Pull the static game data out of game.exe's data segment into editable files under data/.
Run from the project root: python3 -I tools/extract_data.py
Data segment base = EXE image offset 0x8c20 (see docs/formats.md)."""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dofgfx as g
from PIL import Image

EXE = "game/game.exe"
d = open(EXE, "rb").read()
hdr = struct.unpack_from("<H", d, 8)[0] * 16
img = d[hdr:]
B = 0x8c20
ds = lambda off, n: img[B + off:B + off + n]

os.makedirs("data", exist_ok=True)

# World map at DS:0x5b60: width, height (in 128-px blocks), then one stokens block number per cell.
w, h = struct.unpack("<HH", ds(0x5b60, 4))
cells = list(ds(0x5b64, w * h))
json.dump({"width": w, "height": h, "comment": "block numbers into stokens.dat (8x8 tiles of 16x16 px each, wtiles.dat); row-major",
           "blocks": [cells[y * w:(y + 1) * w] for y in range(h)]}, open("data/world_map.json", "w"), indent=1)

# Tile attributes at DS:0x1b3e, one byte per wtiles tile (312 bytes, up to the palette at 0x1c76).
attrs = list(ds(0x1b3e, 0x1c76 - 0x1b3e))
json.dump({"comment": "one byte per wtiles tile: 0 = open, 1 = blocks movement, 0x80 = special (meaning not yet confirmed)",
           "tiles": attrs}, open("data/tile_attributes.json", "w"))
print("world map", w, "x", h, "blocks; tile attribute bytes:", len(attrs), "values:", sorted(set(attrs)))

# Render the world map and mark blocking tiles.
tt = [g.to_image(t, g.GAME_PAL) for t in g.tiles("wtiles")]
blocks = []
for ids in g.token_blocks("stokens"):
    im = Image.new("RGB", (128, 128))
    for i, t in enumerate(ids):
        if t < len(tt):
            im.paste(tt[t], ((i % 8) * 16, (i // 8) * 16))
    blocks.append(im)
world = Image.new("RGB", (w * 128, h * 128))
for y in range(h):
    for x in range(w):
        if cells[y * w + x] < len(blocks):
            world.paste(blocks[cells[y * w + x]], (x * 128, y * 128))
os.makedirs("docs/sheets", exist_ok=True)
world.resize((world.width // 2, world.height // 2), Image.NEAREST).save("docs/sheets/world_map.png")
print("wrote data/world_map.json, data/tile_attributes.json, docs/sheets/world_map.png")

# ---- party records (see data/party.json; written by the stage 4 session, kept here for re-runs)
