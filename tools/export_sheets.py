#!/usr/bin/env python3
"""Write contact sheets of every decoded graphic to docs/sheets/ (run from the project root:
python3 -I tools/export_sheets.py). Mirrors the browser viewer; handy for checking without a browser."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import dofgfx as g
from PIL import Image

OUT = "docs/sheets"
os.makedirs(OUT, exist_ok=True)
BG = (36, 36, 44)


def sheet(images, cols, name, pad=4, scale=2):
    cw = max(i.width for i in images); ch = max(i.height for i in images)
    rows = (len(images) + cols - 1) // cols
    sh = Image.new("RGBA", (cols * (cw + pad) + pad, rows * (ch + pad) + pad), BG + (255,))
    for n, im in enumerate(images):
        sh.alpha_composite(im.convert("RGBA"), (pad + (n % cols) * (cw + pad), pad + (n // cols) * (ch + pad)))
    sh = sh.resize((sh.width * scale, sh.height * scale), Image.NEAREST)
    sh.convert("RGB").save(f"{OUT}/{name}.png")
    print(f"{OUT}/{name}.png  {len(images)} images")


for n in g.PICTURES:
    pix, pal = g.picture(n); sheet([g.to_image(pix, pal or g.GAME_PAL)], 1, n, scale=2)
for n in ("wtiles", "qtiles", "utiles"):
    sheet([g.to_image(t, g.GAME_PAL) for t in g.tiles(n)], 20, n)
for n, tl in (("stokens", "wtiles"), ("utokens", "utiles")):
    tt = g.tiles(tl); ims = []
    for ids in g.token_blocks(n):
        im = Image.new("RGB", (128, 128))
        for i, t in enumerate(ids):
            if t < len(tt): im.paste(g.to_image(tt[t], g.GAME_PAL), ((i % 8) * 16, (i // 8) * 16))
        ims.append(im)
    sheet(ims, 8, n, scale=1)
sheet([g.to_image(s, g.GAME_PAL, transparent=True) for _, _, s in g.objpic() if s], 12, "objpic")
sheet([g.to_image(p, g.GAME_PAL, transparent=True) for p in g.sprite_chain("clips", 0x44)], 7, "clips")
for n in g.HEROES + g.MONSTERS:
    _, fr = g.actor(n); sheet([g.to_image(f, g.GAME_PAL, transparent=True) for f in fr], 12, n)
