"""Decoders for the DOS Dragons of Flame graphics files (EGA planar). See docs/formats.md.

Two row layouts exist (w = width in 16-px words):
  pictures (title, pannel, child, pax, clips portraits): per row, per 16-px column: 4 planes x 2 bytes  (decode_rows)
  sprites (objpic, hero and monster frames, sbits):      per row: plane 0 across the whole width (2*w bytes),
                                                           then plane 1, 2, 3                         (decode_sprite)
For w = 1 the two are identical, so tiles and one-word sprites are the same either way.
Colour 0 is transparent for sprites.
"""
import struct
from PIL import Image

GAME_DIR = "game"


def ega_rgb(v):
    """6-bit EGA register value (rgbRGB) -> RGB, with the monitor's dark-yellow-to-brown quirk."""
    r = ((v >> 2) & 1) * 2 + ((v >> 5) & 1)
    g = ((v >> 1) & 1) * 2 + ((v >> 4) & 1)
    b = (v & 1) * 2 + ((v >> 3) & 1)
    if (r, g, b) == (2, 2, 0):
        g = 1
    return (r * 85, g * 85, b * 85)


# The in-game palette: game.exe data segment offset 0x1c76 (16 EGA register values).
GAME_REGS = (0, 59, 61, 63, 0, 4, 60, 7, 56, 3, 57, 60, 6, 58, 2, 62)
GAME_PAL = [ega_rgb(v) for v in GAME_REGS]


def title_pal(title_bytes):
    """The title picture carries its own palette in bytes 2..17."""
    return [ega_rgb(v) for v in title_bytes[2:18]]


def read(name):
    return open(f"{GAME_DIR}/{name}.dat", "rb").read()


def decode_rows(buf, off, wwords, h):
    """h rows, wwords 16-px columns. Returns (rows of palette indices, next offset)."""
    w = wwords * 16
    out = [[0] * w for _ in range(h)]
    i = off
    for y in range(h):
        row = out[y]
        for wc in range(wwords):
            for pl in range(4):
                for b in range(2):
                    v = buf[i] if i < len(buf) else 0
                    i += 1
                    if v:
                        x0 = wc * 16 + b * 8
                        for bit in range(8):
                            if (v >> (7 - bit)) & 1:
                                row[x0 + bit] |= 1 << pl
    return out, i


def decode_sprite(buf, off, wwords, h):
    """Sprite rows: per row, plane 0 across the width, then planes 1-3. Returns (rows, next offset)."""
    w = wwords * 16
    out = [[0] * w for _ in range(h)]
    i = off
    for y in range(h):
        row = out[y]
        for pl in range(4):
            for bx in range(wwords * 2):
                v = buf[i] if i < len(buf) else 0
                i += 1
                if v:
                    for bit in range(8):
                        if (v >> (7 - bit)) & 1:
                            row[bx * 8 + bit] |= 1 << pl
    return out, i


def to_image(pix, pal, scale=1, transparent=False):
    h = len(pix); w = len(pix[0]) if h else 0
    im = Image.new("RGBA" if transparent else "RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = pix[y][x]
            px[x, y] = (*pal[v], 0 if (transparent and v == 0) else 255) if transparent else pal[v]
    return im.resize((w * scale, h * scale), Image.NEAREST) if scale != 1 else im


# ---- file formats -------------------------------------------------------------------------

def _swap_pairs(b, start=0):
    b = bytearray(b)
    for i in range(start, len(b) - 1, 2):
        b[i], b[i + 1] = b[i + 1], b[i]
    return bytes(b)


def picture(name):
    """title.dat (palette header, 34-byte head) / pannel, child, pax (w,h head). Returns (pix, palette or None)."""
    d = read(name)
    if name == "title":
        pix, _ = decode_rows(d, 34, 20, 200)
        return pix, title_pal(d)
    if name == "pax":  # stored in the driver's native byte order: swap pairs
        d = _swap_pairs(d, 4)
    w, h = struct.unpack("<HH", d[:4])
    pix, _ = decode_rows(d, 4, w, h)
    return pix, None


def tiles(name):
    """qtiles / wtiles / utiles: consecutive 16x16 tiles, 128 bytes each."""
    d = read(name)
    return [decode_rows(d, t * 128, 1, 16)[0] for t in range(len(d) // 128)]


def token_blocks(name):
    """stokens / utokens: 132-byte blocks: width(8), height(8), then 64 tile numbers."""
    d = read(name)
    out = []
    for b in range(len(d) // 132):
        w = struct.unpack("<66H", d[b * 132:b * 132 + 132])
        assert w[0] == 8 and w[1] == 8
        out.append(list(w[2:]))
    return out


def sprite_chain(name, start, count=None):
    """clips: sprites back to back, each w(words), h, data. Returns list of pixel arrays."""
    d = read(name)
    dd = _swap_pairs(d, start) if name == "clips" else d  # clips pixels are in driver byte order, headers are not
    o = start; out = []
    while o + 4 <= len(d) and (count is None or len(out) < count):
        w, h = struct.unpack("<HH", d[o:o + 4])
        pix, o = decode_rows(dd, o + 4, w, h)
        out.append(pix)
    return out


def objpic():
    """objpic: table of word offsets (until the first sprite), each sprite w,h,data. Sprites may be shared."""
    d = read("objpic")
    n = struct.unpack("<H", d[:2])[0] // 2
    offs = struct.unpack(f"<{n}H", d[:2 * n])
    sprites = {}
    for o in set(offs):
        if o + 4 > len(d):
            continue
        w, h = struct.unpack("<HH", d[o:o + 4])
        if 1 <= w <= 8 and 1 <= h <= 128 and o + 4 + w * h * 8 <= len(d):
            sprites[o] = decode_sprite(d, o + 4, w, h)[0]
    return [(i, o, sprites.get(o)) for i, o in enumerate(offs)]


def actor(name):
    """Hero / monster files: word0 = frame height, word10 = offset of the first frame, frames are 32 px wide.
    Returns (height, frames)."""
    d = read(name)
    h = struct.unpack("<H", d[:2])[0]
    base = struct.unpack("<H", d[20:22])[0]
    fsz = h * 16
    n = (len(d) - base) // fsz
    return h, [decode_sprite(d, base + k * fsz, 2, h)[0] for k in range(n)]


HEROES = "tanis goldmoon raistlin caramon sturm river laurana flint tassle gilth nomad".split()
MONSTERS = [f"msv{c}" for c in "abcfhijklmnopqr"]
PICTURES = ["title", "pannel", "child", "pax"]


def mpl(name):
    """mpl0-2: 16x16 map-view creature sheets. Header: word 0x3a+2f -> per-frame table of 4 direction offsets
    (dir 0 and 2 share data); 8 frames x 3 views, each 128 bytes (16 rows x 8). Returns frames[f][view]."""
    d = read(name)
    ft = struct.unpack_from("<8H", d, 0x3a)
    out = []
    for f in range(8):
        dt = struct.unpack_from("<4H", d, ft[f])
        out.append([decode_rows(d, dt[k], 1, 16)[0] for k in (0, 1, 3)])
    return out
