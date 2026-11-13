# Dragons of Flame (DOS, 1989) data formats

Written 2026-11-11 during stage 1. All of it was worked out from the files plus the disassembly of
`game.exe` and the resident driver `start.com`. Decoders: `tools/dofgfx.py` (Python) and `viewer/dof.js`
(JavaScript); they agree on a 14-image checksum sample. Pictures of every decoded set are in `docs/sheets/`.
Open `viewer/index.html` (via `./serve.sh`, then `/viewer/`) to browse them.

## How the program is built
- `game.exe` is plain real-mode 8086, ~63 KB of code and data, no overlays, 11 relocations. `objdump -D -b binary -m i8086 work/game.img`
  disassembles it (`work/game.img` is the EXE with its 0x200-byte header stripped; `tools/dis.sh game|start 0xSTART 0xEND` slices it).
- **Data segment base = image offset 0x8c20** (paragraph 0x8c2, loaded by `mov $0x8c2,%di` at 0x847e). A data offset `X` is image offset `0x8c20 + X`.
  (An early guess of 0x8c60 was wrong.)
- `start.com` (EGA), `cga.com`, `tandy.com` are Clipper "EXEC" resident drivers. They hook **INT F8h**; AX = function number,
  dispatched through a table at 0xc67 (COM origin 0x100). `game.exe` does no direct hardware access, so the whole platform
  layer is about 20 calls. Seen so far:

| AX | Driver address | Meaning (confirmed unless marked ?) |
|---|---|---|
| 0 | 0ca2 | clear/copy video pages, CX = rows |
| 1 | 0cee | swap/flip the visible page |
| 2 | 0d22 | set palette: DS:DX = 17 EGA register values (INT 10h AX=1002h) |
| 3 | 0d2c | draw one 16-px-wide masked sprite strip; mask = OR of the four plane words, so colour 0 is transparent |
| 8 | 134c | put image (header w,h) ? |
| 9 | 149f | load file: DS:DX = name, BX = destination segment; returns bytes read |
| 11 | 14e5 | draw text ? |
| 13 | 1624 | set font pointer (stores DS:SI) |
| 23 | 1a9b | returns the video/driver type (0x20 = EGA, skips the palette call) ? |
| 27 | 1b96 | show a loaded picture: skips 2 bytes, byte-swaps the 32,000 data bytes, sets the palette from bytes 2-17 |
| others | | 4,10,12,16,17,20,21,24,25,26 exist and are used; not decoded yet (input, timing, sound likely) |

## Palette
- In-game palette = 16 EGA register values at data offset 0x1c76: `00 3b 3d 3f 00 04 3c 07 38 03 39 3c 06 3a 02 3e`.
  A second table at 0x1c88 is the standard EGA palette (used when the driver reports a non-EGA mode).
- 6-bit register value is `rgbRGB`: R = bit2*2 + bit5, G = bit1*2 + bit4, B = bit0*2 + bit3, each x85.
  Dark yellow (R=2, G=2, B=0) shows as brown (G=1), the EGA monitor behaviour that DOSBox copies. **Verified** against a real game screenshot.
- `title.dat` carries its own palette (see below), used only for the title screen.

## Byte order: the pair swap (found in stage 3)
Pictures shown through the driver's "show picture" call (AX=27: title, pannel) are stored in the plain layout below (child.dat is not: it needs the pair swap too, as the ending's picture of the freed children showed). Data drawn
directly by the driver's image routine (AX=8) is stored in its **native byte order, with every pair of bytes swapped** (the left and right
8 pixels of each 16-pixel column exchanged). That applies to **`clips.dat` pixel data (not its w,h headers), `pax.dat` and `child.dat`**. Before the swap,
`clips` looks like half a face next to another half, and `pax` looks speckled. After it: ten hero portraits plus compass and sword icons, and the
Pax Tharkas fortress picture. Swap pixel bytes only, never the headers.

## Pixel layouts (width w in 16-pixel words; 4 bit planes, plane 0 = blue)
- **Picture layout**: per row, per 16-px column: plane 0 (2 bytes), plane 1, plane 2, plane 3. Used by pictures and portraits.
- **Sprite layout**: per row: plane 0 across the whole width (2w bytes), then plane 1, 2, 3. Used by object, hero and monster sprites.
  For w = 1 the two layouts are identical (so all tiles and one-word sprites are the same either way).
- Sprites: colour 0 is transparent.

## Files
| File | Format | Status |
|---|---|---|
| `title.dat` 32,034 | 2 bytes unknown, 16 palette registers (+1 overscan), then 32,000 bytes: 320x200 picture layout | verified |
| `pannel.dat` 11,524 | w=20, h=72 words/rows, then picture layout: the stone panel with hero slots | verified against the live game |
| `child.dat`, `pax.dat` | w, h (words), picture layout: 320x126 and 320x120 scene pictures; **pax needs the pair swap** | both verified visually (pax: red fortress, cyan sky, mountains) |
| `qtiles.dat` 160, `wtiles.dat` 303, `utiles.dat` 187 | consecutive 16x16 tiles, 128 bytes each | verified: 157 of 160 cells of a real map screen matched wtiles/qtiles/utiles |
| `stokens.dat` 204, `utokens.dat` 77 | 132-byte blocks: 8, 8, then 64 tile numbers (8x8 block of 16x16 tiles, row-major). stokens use wtiles (max id 303), utokens use utiles (max 186) | composites look right (overworld, castle interiors) |
| `objpic.dat` | table of word offsets up to the first sprite (84 entries; repeats = shared sprites); each sprite: w, h, sprite layout | verified visually (items, potions, web, chest, weapons) |
| `clips.dat` 11,304 | 0x44-byte offset table, then 21 sprites back to back: w, h, picture layout **with the pair swap** (data only). Index 2 Goldmoon, 3 Caramon, 4 Sturm (the user found these two swapped in the game), 5 Raistlin, 6 Tanis, 7 Tassle, 8 Riverwind, 9 Flint, 10 Gilthanas, 11 Laurana; 12-15 compass frames; 16-17 sword icons; 18-19 two more faces | verified against the real panel (order matches) |
| `sbits.dat` 640 | five 16x16 sprites, sprite layout: burst, three fire stages, a gem | verified visually |
| hero files (`tanis goldmoon raistlin caramon sturm river laurana flint tassle gilth nomad`.dat) | word0 = frame height, word1 = 22, word5 = frame count? (34), word10 = offset of first frame; frames are 32 px wide, `height*16` bytes each, sprite layout. Bytes 0x18..base hold animation/behaviour tables (undecoded). Frame count = (size - base) / (height*16) works for every file (34, 28, 37 frames) | frames verified visually (Tanis with sword and bow, etc.) |
| `mpl0-2.dat` 3,210 | 0x8a-byte header: word 0x3a+2f = per-frame table of 4 direction offsets; then three 1,024-byte blocks (views), 8 frames x 16x16 each, picture layout. Map-view figures | verified visually |
| monster files `msv[abcfhijklmnopqr].dat` | same header and frame layout as heroes; frame heights 19-40, 4-20 frames | verified visually (draconians) |

## Not decoded yet
- Hero/monster header tables (animation scripts, hit boxes, stats, words 2-4, 6-9, 11 onward).
- `missle.dat` (projectile sprites and a table; layout unclear), `score.dat`
  (referenced, not shipped), `women.dat`, `eben.dat` (referenced by name, not shipped), `*.ins` installer files.
- Everything in `game.exe` beyond the file loader and driver calls: the map format, main loop, combat, AI, scripts, text, sound.
- Not documented anywhere else: searches found no prior reverse-engineering, ScummVM support or open-source engine for this game,
  and the Shikadi ModdingWiki has no page for it.

## Data inside game.exe
See `docs/engine-notes.md`: the 20x24-block world map (data 0x5b60), the tile attribute table (0x1b3e), spawn points (0x19a0), strings. `tools/extract_data.py` writes `data/*.json`.

## Pitfalls learned
- Do not treat sprite and picture layouts as interchangeable for w > 1.
- Test the decoders on a real game screenshot: the emulator canvas could not be read back (blank `toDataURL`), but a page screenshot
  plus colour-agnostic tile matching recovered the palette.
- Run test browsers with `--mute-audio`.

## Correction (second run): tile numbers are 1-based
Token blocks (stokens, utokens) store tile numbers 1..303 (wtiles) and 1..186 (utiles): **graphic = number - 1**. The attribute table (0x1b3e) and the strip tables
(0x1eba, 0x225e; 3 bytes per number, 304 entries) are indexed by the raw number. Found by matching a screenshot of the original's first screen: with this the
terrain, the rocks and the river match tile for tile. The view starts 160 px left of and about 70 px above the party (fn 4DDA); the party and creatures are 16x16
`mpl` figures (mpl0 = party, mpl1 = blue creatures, mpl2 = green). The party starts at world (1024,1024) (data 0x361E/0x3622). (Correction: 0 on the keypad does toggle wilderness/combat view; the rest message came from the menu.) The game asks a copy-protection question on start (page/paragraph/word of the manual; Enter skips it).
