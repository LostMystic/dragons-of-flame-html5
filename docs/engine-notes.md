# Dragons of Flame: how the program works (stage 2 survey, 2026-11-11)

Source of truth for the rewrite. Everything here comes from the disassembly (`work/game.asm`, `work/start.asm`, rebuilt with objdump;
see docs/formats.md) plus checks against the running original. "Confirmed" means seen working; "guess" means inferred from code only.
Addresses: code = image offset in `game.exe` after its 0x200-byte header; data = offset in the data segment (image offset + 0x8c20 = data offset).

## 1. Shape of the program
- `game.exe` is a 1988 Microsoft C program: ~0x8474 bytes of code (game plus C runtime), 27 KB of initialised data. Entry 0x8474 sets up the
  runtime and calls **main at 0x3f83**. About 180 functions; the large ones are the game logic.
- All hardware access goes through the driver (`start.com`) via INT F8h. The platform layer the new engine must supply is listed in
  docs/formats.md. Additional calls decoded this stage:

| AX | Purpose |
|---|---|
| 3 | draw a 16-px-wide masked sprite (8 bytes/row, colour 0 transparent), clipped to x -32..320 |
| 4 | same for a 32-px-wide sprite (16 bytes/row), used for hero and monster frames |
| 10 | non-blocking key read (INT 21h AH=0Bh then 07h); returns 0 if none |
| 12 | joystick read: direction bits plus 0x80 for the button |
| 16 / 17 | joystick calibration / create file |
| 20 | draw a row of tiles from the video-memory tile cache (list of tile offsets in DS:SI, x in CX, y in DX) |
| 21 | blocking key read (&0x7f) |
| 24 | load a file straight into off-screen video memory (used to cache tile sets; returns bytes) |
| 25 | fill a rectangle with a colour (direct write to A000h) |
| 26 | draw text / small glyphs with a colour (uses the font set by AX=13) |

  Tile sets (wtiles, qtiles, utiles) are loaded into off-screen EGA memory starting at 0x4000 so tiles can be copied with the latches.
  An HTML5 engine does not need that trick: keep decoded tiles in memory and blit them.

## 2. Start-up (confirmed from main at 0x3f83 and the init at 0x3eb2-0x4260)
1. Detect video type (AX=23; 0x20 = EGA), set the palette, show the title (`title.dat`), wait for a key or the timer.
2. Allocate one memory arena and load, in this order: `clips`, `stokens`, `utokens`, `msv` + letter A..S into slots 2..19 (missing files
   skipped), `missle`, `objpic`, `sbits`, `mpl0..2`, the current hero file, then `qtiles`, `wtiles`, `utiles`; read `score.dat` (high scores).
3. Set the in-game palette (table at data 0x1c76) and enter the main loop with mode 2.

## 3. Main loop (confirmed shape; main at 0x42c8-0x4780)
- **Mode variable at data 0x3038:** 1 = combat view (side-on action), 2 = wilderness view (overview with small figures), 3 = main menu.
  The numpad `0` key toggles between combat and wilderness view (blocked next to rivers, and inside Sla-Mori and Pax Tharkas).
- **Frame pacing:** a driver timer increments data 0x6d92; a frame waits until it has reached 3 ticks, then clears it.
- Per frame in combat view: read keys or joystick, scroll the camera (data 0x6e40 / 0x6e46, window 20 x 8 tiles), draw the map (0x4dda),
  update entities (0x381f), collide (0x6fca), draw (0x5930), then flip. Every 8th frame the entity AI routine **0x346f** runs.
- Keys seen: `j` joystick, `0` view toggle, the numpad directions, `+` attack. Details are in the manual (docs/reference/).

## 4. Maps and tiles (confirmed)
- **Tile lookup 0x4d12(x, y)** takes world pixel coordinates and returns a tile number. A map is `width, height` (in 128-px blocks), then one
  block number per cell. A block is a 132-byte entry in `stokens.dat` (overworld) or `utokens.dat` (interiors): 8, 8 then 64 tile numbers.
  Within a block the tile is chosen from the 16-px cell. The tile number indexes `wtiles` (overworld) or `utiles` (interiors).
- **Two maps live inside game.exe's data segment** (extracted to `data/world_map.json`, picture `docs/sheets/world_map.png`):
  - data 0x5b60: **20 x 24 blocks** = the whole overworld (2560 x 3072 px). Decoded and checked: mountains, forest, river, lake, roads, fortress.
  - data 0x5d44: 192 x 4 blocks, all zero in the file, so it is a buffer filled at run time (the interior or side-scrolling strip; how it is
    filled is not yet found).
- **Tile attributes** at data 0x1b3e, one byte per wtiles tile (312 bytes, ending at the palette): values 0, 1, 3, 5, 0x80
  (`data/tile_attributes.json`). 1 blocks movement (confirmed, see section 9); the rest are guesses: 0 open ground,
  3 reeds or shallows, 5 dense bushes, 0x80 road. Not confirmed; the main loop uses value 1 to stop the view switch.
- Tile properties for `utiles` are not yet located.

## 5. Entities (partly understood)
- Entity array starts at data 0x3618. Per entity: +1 animation frame byte, +2 facing (low 2 bits), +3 **kind 1..16**, +4 and +8 position words
  (shifted by 3, so positions are in 1/8 px or similar). The AI routine 0x346f dispatches on kind through a jump table at code 0x378b:
  kind 1 -> 0x34ba, kind 2 -> 0x3525, kind 3 -> 0x355b, kinds 4-15 -> 0x3693, kind 16 -> 0x3744.
- Kinds 1 to 3 and 16 draw from `mpl0`, `mpl1`, `mpl2` (small map figures); kinds 4 to 15 are the monster types (`msv` files A..).
- A table of 16 spawn points (x, y word pairs) sits at data 0x19a0, with 16 bytes of per-point values at 0x19e0.
- Drawing: 0x4efa and 0x4f04 draw an `mpl` sprite (frame table, facing, then driver call 3); 0x4f80 and neighbours draw the 32-px actors (call 4).

## 6. File roles, now settled
- `mpl0..2`: 16x16 figures for the wilderness view. 8 frames x 3 views (view 0 and 2 share data). Decoded and viewed (red warrior, blue man, green creature).
- `msv?.dat`: one monster type per letter, loaded into slot k = letter - 'A' + 2.
- Hero files: loaded on demand into one slot (data 0x6d9a) by name from the table at 0x16a2 (names Goldmoon...nomad).
- Text: menus and messages are in data 0x08-0x1791 (about 350 strings); names and high scores near 0x63a6-0x6610 and 0x50c0-0x5296.

## 7. Not yet understood
Combat and damage rules, hero and monster stat tables, item and spell tables, the Disks of Mishakal and Staff of Magius charge logic, the quest
map (dragon army advance), save-file layout, the interior map fill, `missle.dat`, the hero and monster header animation tables, and the sound
routines. These are the work of stage 4. Function index for further work (guessed purposes in italics in notes): 0x3f83 main, 0x4850 video
and title set-up, 0x4d12 tile at pixel, 0x4dba set a map, 0x4dda draw map, 0x346f entity AI, 0x381f update, 0x6fca collisions, 0x5930 draw,
0x6c16 wilderness-view draw, 0x83a2 text, 0x83c2 clear, 0x83ce flip, 0x83d6 key, 0x83de joystick, 0x8431 set palette.

## 8. Reference material (local copies of the 1989 manual and the cluebook, OCR text, in docs/reference/)
They confirm the structure above (two views, hero select, Staff of Magius and Disks of Mishakal charges, rest and quest map menu items)
and are the cross-check for rules in stage 4. The cluebook's maps are images and have not been compared to the extracted world map yet.

## 9. Decompiler output (added in stage 3)
Reko 0.12.5 was run on `game.exe` (user approved; 2 seconds). Output in `work/reko/`: `game_0800.c` (~7,400 lines, 165 functions), `game.h` (types),
`game_0800.asm`. Function names are `fn0800_XXXX` where XXXX is the same code offset used in these notes. The C is rough (`ds->*offset` pointer
notation, merged types) but readable, and it agrees with the hand-read logic: for example `fn0800_4D12` is the tile lookup, and the movement
checks probe the two leading corners of the 16x16 box and stop when `(attribute & 0x0F) == 1` (table at 6974 = 0x1b3e).
Use it as a map for stage 4: find a function by its address here, read the C, then write the rule in clean code and data.
Re-run: `dotnet <reko>/decompile.dll game.exe` in a folder holding a copy of game.exe (decompiler binaries are not stored in the project).
**Confirmed meaning of tile attributes:** `attribute & 0x0F == 1` blocks movement (river, rock, wall tiles); `0x80` is tested by the entity AI.

## 10. Stage 3 results: the engine skeleton (2026-11-11)
- `src/` is a new C++17 engine. `gfx` (320x200 indexed screen), `assets` (decoders for the original files), `world` (map, tile lookup, collision,
  the same rules as fn 4D12 and the movement probes), `game` (title and world scenes, fixed-rate logic), `platform_sdl` (web window, keys, 4:3 display),
  `platform_null` (headless tests, scripted keys, PPM screenshots), `main`. Editable data: `data/rules.json`, `data/panel.json`, `data/world_map.json`,
  `data/tile_attributes.json`.
- Build: native headless `cmake -S . -B build && cmake --build build`, tests `tests/run.sh`; web `web/build.sh` (uses the Emscripten SDK from
  `../fantemp/work/emsdk`), output `build-web/web/` (serve it and open `index.html`).
- **What the slice does:** title picture with its own palette; any key starts the world; the world map walked at ground level with the real tiles, Tanis
  as the party sprite (flipped when walking left), real collision rules, a camera that follows, and the hero panel with portraits in the real order.
  Logic steps at the original's rate (18.2 Hz timer, 3 ticks per step). **Step size 8 px, the start position and the walk-frame list are guesses**,
  marked in `data/rules.json`.
- **Important finding:** the original's first combat-view screen is NOT part of the 20x24 world map. The combat view draws from the second map buffer
  (data 0x5d44, 192x4 blocks, filled at run time), generated from the wilderness terrain. Walking the world map directly is not how the original
  plays; reproducing the generator is a stage 4 task.
- Byte-pair swap for `clips` and `pax`: see docs/formats.md.

## 11. Chunk one of stage 4: levels and the combat-view generator (2026-11-11)
- **Seven levels, one table.** Data 0x658c holds six bytes per level: a data offset, a width and a height; `fn 52FA(level)` copies them into
  data 0x6e4a / 0x303e / 0x3040 (the current level's cell array and its size). Level 1 is the outdoors: 160 x 192 cells with no stored data
  (offset 0), built at run time (160 x 192 = the 20 x 24 world map expressed in tiles). Levels 2 to 7 are stored: 40x30, 32x24, 29x24, a 2x2 stub,
  29x25, 26x21 cells (about 3.4 KB at data 0x415a-0x50bc). Extracted to `data/levels_2_to_7.json`, picture `docs/sheets/levels_2_to_7.png`: cells are
  **utiles** tile numbers, and the plans show rooms and corridors (the Sla-Mori and Pax Tharkas interiors).
- **CORRECTION (chunk two):** `data 0x3042` is the **level number** (1 to 7), not a kind. Level 1 is the outdoors, and fn 54CC's `==1` branch (the one that calls fn 4D12) is the outdoor generator; the other branch (table 0x1e54 and cell arrays) is for levels 2-7, the interiors. The description below is otherwise right but has those two branches swapped.
- **Combat-view terrain generator = `fn 54CC`** (944 bytes). It fills the 192 x 4 block strip at data 0x5d44 (zeroed first; the bottom row gets block 0x38).
  Variable `data 0x3042` is the level kind (1 = interior, else outdoors) and `0x303a` the travel axis (0 = east-west, else north-south).
  - Outdoors: from the party's cell (position >> 7) it walks along the line of cells, looks each cell up in **table 0x1e54** (`0x1dee` for the other axis)
    to get a block number, adds scenery blocks above/below for special blocks (0x39 puts 0x3b above; cell values 0x55-0x57 add a block below),
    and keeps going while the cell's flags in **table 0x2668** (bit 1 east, 2 west/south..., 4, 8) say the next cell is open. The stopping point sets the
    scroll limits (data 0x6d9e, 0x6e44).
  - Interior: the strip is built from the level map through `fn 4D12` and the tables at 0x2254-0x2260.
  So the combat view is a four-block-high slice of terrain chosen from the overview cell under the party, not a piece of the world map.
- **Entities, two arrays:** the wilderness array at data 0x3c6a (40 entries of 18 bytes) and the combat array at 0x3118 (80 entries of 16 bytes).
  `fn 6AC4` copies wilderness monsters near the party into combat entities (kind from table 0x1daa, position >> 3), `fn 6B46` and `fn 6B92`
  position and tidy them when the view changes.
- **Still to do for the generator:** how level 1's 160 x 192 cell array is filled (a function around 0x3b9x-0x3bc4 reads it; 0x5d92 is called
  there), and the contents of tables 0x1e54, 0x1dee, 0x2668.

## 12. Chunk two: the outdoor combat strip, implemented (2026-11-11)
- **Outdoor (level 1) generator, exact:** the party's world position (wilderness pixels, the same coordinates as the world map in `data/world_map.json`)
  picks a straight line of tiles. East-west travel (`data 0x303a` nonzero, the start state): tiles at x = 0, 16 ... 2544 along row y = party y, using table
  **0x1eba**. North-south travel: tiles at x = party x with y running from 3056 down to 16 in steps of 16, using table **0x225e**. For each tile `t` the
  table gives three bytes at base + 3t, in the order [row 2, row 1, row 0]; row 3 is always block 10. Result: a strip of 192 x 4 blocks (stokens block numbers).
  The tables are in `data/strip_tables.json` (303 tiles each; every value is a valid block number, maximum 203 of 204).
- **Coordinates:** one world tile (16 px) corresponds to one 128-px combat block, so combat position = world position x 8. The party enters the combat view
  at x = world x * 8 + 0x40 (east-west) or (3056 - world y) * 8 + 0x40 (north-south), at y = 296 (row 2). Leaving converts back.
- **Engine:** `src/strip.*` builds the strip; `Game` has three modes (title, wilderness, combat); Tab, numpad 0 or 0 toggles. The combat view uses the same
  16x16 collision rule on the strip. Tests in tests/run.sh cover entering, walking, returning and the vertical strip.
- **Not verified against the original.** I decoded the original's first combat screen (screenshot) into tiles and searched for the generator's blocks in it,
  at every offset, with mirrored and rotated variants: no match. So either the original starts somewhere I don't know, or later drawing steps change the
  blocks (water animation, scenery, the interior branch's 0x39/0x3b rules) or the screenshot isn't a plain generator output. Treat the strip as a faithful
  reading of the code, not a confirmed reproduction. Next: trace the draw routine fn 4DDA to see how blocks become tiles on screen.
- **Also found:** the wilderness view is the world map at 1:1 (what the stage 3 slice shows), the party figure there should be a 16x16 `mpl` sprite
  rather than the 32x40 hero frame, and levels 2-7 use the cell-array generator (tables 0x1e54, 0x1dee, 0x2668).

## 12b. Combat terrain: found the cause (second run)
The mismatch in section 12 was the 1-based tile numbers (see docs/formats.md, Correction). The wilderness now matches the original's first screen. The strip
generator itself is unchanged and uses the same numbers, so it is expected to match too, but an original combat screenshot has not been captured (it needs a
fight in the DOS build; the creature near the start is entity kind 7).

## 13. Stages 4 to 6 as built (2026-11-11), and what is still open
**Online:** a private build exists; see README.

**Real (taken from game.exe):** the tile and block maps and the combat-strip tables; the tile blocking rule; the party's names, hit points
(`data/party.json`); the panel order and portraits; the 16-entry sound table and the two note-stream sounds (title theme 15, effect 13);
the message texts (`data/messages.json`, 120 strings from the table at data 0x1792, not shown in the game yet); the six interior level maps
(`data/levels_2_to_7.json`, not walkable yet).

**Estimated (editable, marked in the data):** walking step size, start position, walking and attack animation frames, hero damage (1-8 from the manual),
monster hit points, damage, speed and frames (`data/monsters.json`), encounter size and monster mix, enemy aggro range and cooldown, XP = monster hit points.

**Behaviour in the build:** title with music (V mutes); any key starts. World view: walk the 20x24-block map at 1:1. Tab or numpad 0: combat view
(strip from the tables; 3 enemies spawn ahead). Space, numpad + or X attacks (front arc, 34 px reach); 1-8 choose the leader; enemies close in and
strike the leader; a dead leader hands over to the next living hero; no one left shows "the party has fallen" (effect 13) and returns to the title.
F5 saves, F9 restores (user/save.json; browser storage on the web). Update banner and Exit page work as in Fantasy Empires.

**Update (second run, same day):** all six open items were worked on; what is real and what is not:
1. *Combat terrain:* still unconfirmed against the original's first screen (needs a side-by-side with DOSBox); not changed.
2. *Monsters (real):* hit points = dice x d6 + bonus, damage dice, to-hit and flags from the 7-byte table at data 0x2986 (`data/monsters.json`);
   hero armour table at 0x2A0E, a hit needs d20 >= armour[hero.abilities[6]] + to_hit - 1 (fn 6310); encounter pool table 0x2868; 1-3 monsters per
   contact (fn 6572); contact = both distances under 16 (fn 342B); enemy distance bands touch <32 / approach <150 / idle >=384 (fn 5D92). Still estimated:
   hero damage (1-8), XP, enemy speed and animation frames, attack period, missiles, spells, the Staff and Disks.
3. *Wilderness:* 14 wandering `mpl1` creatures (estimated starting list); they chase within 160 px; touching one starts a fight in the generated
   strip and the party returns when all are dead. Tab still toggles the view.
4. *Interiors:* F1-F6 enter levels 2-7, F7 returns. Drawn from utokens/utiles; collision is an estimate (empty cells block, the rest is walkable);
   the real level changes (3D7F-3E4E), doors and monsters are not done.
5. *Sound:* types 0 and 1 are now played (`data/sounds.json` has `segments` / `intervals`; speaker flips per timer interrupt as in fn 8289/8305);
   hero hit by a monster plays 3, monster hit plays 4 (the two fn 810E calls). Other event-to-sound links are unknown.
6. *Menu:* Enter opens Party, Experience, Rest (estimate), Quest map (overview of the world), Messages (the 120 texts). The original's menu layout is not decoded.

**Not done (in rough priority order):**

1. Combat-view accuracy: the generator output did not match the original's first combat screen (section 12). Trace `fn 4DDA` (block to tile drawing) and the
   level-1 start state. Until then the combat terrain is a faithful reading of the tables but not a confirmed reproduction.
2. Real monster and combat rules from the decompiled code (`work/reko/game_0800.c`): damage, hit points, AI kinds (jump table at 0x378b), spawn points
   (0x19a0), the entity arrays (0x3c6a wilderness, 0x3118 combat), weapons, the Staff of Magius and Disks of Mishakal charges, resting, experience.
3. The wilderness view as the original draws it: the party and creatures as 16x16 `mpl0-2` figures (decoded) moving through the overview, encounters triggered
   by contact instead of a key.
4. Interiors: levels 2 to 7 (Sla-Mori, Pax Tharkas): the cell-array strip generator (tables 0x1e54, 0x1dee, 0x2668) and the level changes at 3D7F-3E4E.
5. Sound: the noise-burst effects (sound types 0 and 1, handlers 8289 and 8305) need a PIT-timer emulation; which game events play which sound.
6. The original's menu (hero select, spells, items, quest map, rest, xp list), the quest map and the dragon armies' advance, text boxes from `messages.json`.
7. Verification against the original, side by side, for each of the above.

**Third run (same day):** wilderness creatures now use the original's start list (data/wilderness_entities.json, entities 80-94 at data 0x3118); interior
creatures come from entities 0-63 (data/creature_table.json: type at +0, level at +2, x/y at +4/+6; types 2-19 only), fight in place with the same rules, and
interior walls use the same attribute table as outdoors. Enter on the start shows the original's opening message; 0 rests. Still open: stair/door level changes,
what the type 23-59 entries are (objects?), the level-1 rows of the table (scripted encounters?), the missing monster sprites (slots 5, 6, 8), the protection question.

**Fourth run (same day), findings from the original running in the browser and from the cluebook:**
- **Keys:** the manual says `0` on the keypad toggles wilderness and combat view (the earlier "numpad 0 = rest" was my mistake: the Enter that showed the rest message had
  opened the menu). Rest is a menu command (heals every wounded hero 1 HP). Menu = Space or the first letter of a command; commands: Exit menu, Hero select,
  Clerical spells, Magic user spells, Use, Rest, Take, Open, Give, Inspect, Drop, Put, Shut, Quest map, Save game, Restore game, Xp. Attack = hold `+` and a number.
- **Level changes:** table at data 0x287A (10 records, fn 3CBE) now in `data/level_links.json` and used by the engine (walk onto Sla-Mori's entrance at world
  (624-640, 2688-2704)). Levels 2 and 7 also swap at a fixed place by climbing (fn 3CBE tail: tile 5/6 and y limits), not done.
- **Monster sprites:** slots 5, 6 and 8 (msvD, msvE, msvG) have no file in the DOS game; the loader (game.exe 402D-405F) leaves the slot on the next loaded file, so War Dog,
  Dire Wolf and Wild Dog all share msvf, Hobgoblin and Goblin share msvh. Names and XP are in `data/monsters.json` (cluebook Appendix B; its HD/AC/damage columns
  match the table at 0x2986 exactly, which confirms the decoding): Baaz, Bozak, Kapak, War Dog, Dire Wolf, Wild Dog, Hobgoblin, Goblin, Troll, Griffon, Zombie, Wight,
  Wraith, Dragon, Giant Wasp, Stirge, Aghar, Man (slots 2-19).
- **Menus:** the original's scroll layout (banner, 8 visible lines) is reproduced for the main menu, hero status and XP list; spells, Use, Take, Open etc. are not.
- **Creature table rows with types above 19 (and the level-1 rows):** probably objects (the manual: "items in the wilderness are represented by small squares"); not drawn yet.

**Fifth run:** level-1 rows of the creature table are fixed places: types 2-19 are guard monsters (touching the place starts a fight with exactly those), types above 19 are
items, drawn as small squares (the manual says items in the wilderness are small squares; a user screenshot showed a chest as one). Taking items is not implemented.
The Amiga disk images are not a normal Amiga filesystem (copy-protected trackloader), but their file names are the same as the DOS set: msvA-C, F, H-R only, so the
missing D, E, G files are missing on the Amiga too (they share art, see the fourth run).
**Adding a monster:** add an entry to `data/monsters.json` (`slot`, `sprite` = the .dat file name, `name`, `xp`, `hp_dice`, `hp_bonus`, `dmg_sides`, `dmg_bonus`, `to_hit`,
frames), put the .dat file in `game/` (frames: 32 px wide, same format as the msv files), and list its slot in `encounter_pool_slots` if it should appear at random.
The js-dos fallback build runs at `cycles=fixed 3000` on purpose (a 286-like speed so the game is playable); scripted test runs could use a faster bundle.

**Sixth run - corrections and additions:**
- **The "creature table" is an item table.** Data 0x3118, entities 0-63: item id at +0 (names: word pointer table at data 0x185e, 69 names), quantity/charges at +1
  (arrows x20), level at +2 (values above 7 = "inside the chest in row N"), x/y at +4/+6. Chests are ids 42-45, id 59 is the hidden treasure ("invisible treasures" in
  the manual), Wyrmslayer (id 18) lies in Sla-Mori. I had wrongly read some of these as monsters; the interior monsters and "guards" are gone (`data/items_table.json`,
  `data/item_names.json`). Where interior monsters come from is still unknown (probably random spawns near the party).
- **Take / Open / Use / Drop** work (items within 32 px; chests spill their contents). Spells: Clerical and Magic user lists with the original's charges (Disks of Mishakal
  100, Staff of Magius 100), memorized spells show a hand when spent, Rest restores them. Healing, Hold/Sleep/Charm/Web, Magic Missile, Burning Hands, Fireball,
  Final Strike and Turn Undead have effects (damage numbers are estimates). In combat the menu opens with Esc.
- **Screen scale:** the browser canvas of the DOS build is 650x488 for 320x200 (not 2.03 vertically; 2.44), which is why my earlier vertical comparisons were off.
  The wilderness view starts exactly 64 px above the party (the code value 0x40).
- **Panel:** portraits at x 64,120,176,232,288; two 3-px bars left of each portrait (green = hit points; yellow = unknown second value, start fractions measured).
- **Turbo (tool-assisted) mode:** set `DOF_TURBO=1`: logic runs 8x faster, message boxes close themselves, no victory pause, scrolls open at once.
  Everything still happens in order. Not exposed on the website.

**Seventh run - the dungeon side view (from the user's screenshots in reference/ and game.exe fn 54CC):**
- Levels 2-7 are shown only as the side view. The strip is built from the level's cells along the party's row (east/west) or column (north/south): row 2 gets the block
  of each cell reachable in a straight line through open sides (tables in `data/interior_tables.json`: cell to block per axis and the open-side flags 1 N, 2 E, 4 S, 8 W).
  The line's ends limit the walk. The view starts at the top of row 2 (hero y 0x170 - 0x70 = 256) and 160 px left of the party; the hero stands at y 112 on the white floor.
  Up/down (the original's 8 and 2) turn the view to the other axis where a passage crosses the cell, which is the "perpendicular" turning the user described.
- Outdoors the same rule: the combat view runs along one axis only (left/right walk, up/down turn to the other axis at the same world position, J/Q/E-style jump of 48 px
  over rivers: keypad 7 and 9, Q and E). Free movement in depth is gone.
- Block 0x39 is the climbable chain base (not a door); doors are ordinary blocks of some cells. Closed doors do not block yet and Open/Shut/Inspect do not change doors,
  because the table that swaps a cell for its open-door twin has not been found.
- Cleric list as the original: one entry per memorized spell, spent ones under "--- used ---", flame mark in front of each; scroll with gold-knobbed rollers, banner and an
  unrolling animation (geometry measured from the screenshots).

**Eighth run - compass, doors, ground (from the user's screenshots):**
- **Compass:** clips 12-15 are the star with its north, west, south or east point red (the pointer = the way the party faces). On an east/west strip right = east and left =
  west; on a north/south strip right = north and left = south (the strip numbers run northwards). Top-down: the last move. The panel picture also holds a half star and two
  arrow marks, which the original covers; the engine paints granite over that cell and draws the clip at (1,7). Crossed swords (clips 16, 17) are not used.
- **Doors:** a door is two cell values: closed and open (EW view / NS view art comes from the cell tables). Pairs found by viewing every cell value in both views
  (tools in the scratchpad): 23/91 -> 24, 26 -> 27, 35 -> 36, 38 -> 39, 47 -> 48, 50 -> 51. Open/Shut in the menu swap the cell the party stands in; a closed door has no
  crossing exit, so the party cannot turn into it. A door seen from the passage it leads off is the thin red door of the "Side Door" screenshot; the same cell seen along the
  wall is the front door; the dark band with bolts under the floor and its white gap come from the row-2 block art of the cells.
- **Turning** between axes needs the party to stand in the dark middle of the cell (between the pillars).
- **Outdoor ground:** in the outdoor side view the party always stands on the first blocking tile found scanning down the strip (fn 672A: attribute bit 0 set, not 5); walking
  can climb one tile (16 px), a jump (48 px) can climb three. The view starts at the top of row 2. The bridge appears as the red deck over stones, as in the screenshots.

**Ninth run:**
- **Scroll art is original:** qtiles 0-3 ragged left edge, 4-7 right edges, 8-11 roller, 13-15 banner, 16-19 / 20-23 left and right knob (animated); the paper between the
  edges is a plain yellow fill. Message boxes use the same scroll.
- **Panel:** 10 identical slots (portraits 32x32 at x 64+56*column, panel y 3 and 38, measured by matching the portrait clips against a screenshot); empty slots are grey.
- **Top-down figures:** the eight mpl frames are the eight directions clockwise from north (table at data 0x19e0), the three views are the walking steps. Party and creatures face
  the way they move; diagonal keypad keys (1, 3, 7, 9) walk diagonally outdoors.
- **Settings** (main menu, last entry): mouse or keyboard; Original controls (on = keypad scheme, off = arrows: up jumps, down turns), graphics 16-colour (checked) and 256-colour
  (disabled, for later). Saved in user/settings.json. Mouse input is new in the platform layer (Input.mx/my/click).
- **Side view:** the party stands on the ground at once on entering; monsters face the party (the sheets face right, flipped when facing left), stop at striking distance
  (reach - 8) and cannot overlap the party or each other, so only the front one can strike; the party cannot walk or jump into a monster. None of this is in the original.

**Tenth run - panel matched to the screenshots:** `pannel.dat` only holds an outline (black slot wells, half a star), so the engine repaints the panel: granite (sampled from the picture),
a compass cell framed at x 3 and 45 (top y 2, bottom y 69), a bright line at x 45+56k between slot columns, per slot a framed black bar well (x-17, 11 wide), the black portrait box with its
frame on the right and lower side, two 3-px bars (28 px high). The star clip sits at (0,7), the crossed swords (clips 16 lit, 17 dark) at (0,44); lit when a foe is within 48 px (my guess).
The cluebook says a hero's "damage bar turns red" when badly hurt, so the green hit-point bar turns red below one third. Nothing in the manual or cluebook explains the yellow bar.

**Eleventh run - quest map and the dragon armies (game.exe fn 4B1E, 4C06, 538C, 4C66):**
- Quest map: 20x24 qtile numbers (1-based) at data 0x57A0 (words), drawn 16 px per cell in a 128-px window starting at (party y/8) - 64 (0..256); fire marks come from
  sbits sprites 1-3 chosen by bits 0x60 of the army strength at data 0x3F28 (20x24 bytes; 127 = a source). Both arrays and the terrain factor table (0x1C9A) are in `data/quest_map.json`.
- Armies: every 360 logic steps (6 x 60, counter at data 0x6D80) each inner cell takes factor% of the difference to each stronger neighbour (the neighbour loses the same
  unless it is 127); the result replaces the array (outer ring empties). Implemented in `Game::army_tick`. What happens when the armies reach the south (loss, messages)
  is not decoded.
- Test cheats (native runs): `DOF_GOD=1` nobody takes damage; `DOF_WARP=w,X,Y` world position, `DOF_WARP=L,CX,CY` level L cell, `DOF_WARP=item,ID` where an item lies; `DOF_GIVE=ID` item in the
  pack; `DOF_ARMY_TICKS=N` advance the armies; `DOF_TURBO=1` fast logic; `DOF_NO_INTRO=1` skip the opening box; F1-F7 jump between levels.
- Item id 18 (Wyrmslayer) is listed at level 1, (1152, 816), in a pile with arrows and longswords, not in Sla-Mori; the item table or my reading of levels may need another look.

**Twelfth run - the story and the ending (from the decompiled code):**
- **Item ids are 1-based** (name = names[id-1]): Wyrmslayer = 19 (lies in Sla-Mori), chests 43-46, Wand of Fireballs 29 etc. Taking an item gives the experience in the table at data 0x3050
  (`data/item_values.json`, once per id; Wyrmslayer 10000). Ids 56 (women, two of them on level 6), 57 (children, level 4) and 58 (the south door, level 6 cell 13,24) are story triggers
  (fn 6C16); 59 is hidden treasure.
- **Quest flags** (data 0x304E, starts 0x0F): bit 0 women freed, bit 1 children freed, bit 2 Wyrmslayer taken, bit 3 Laurana rescued. Each completed part gives 10000 points to the leader.
  Touching the south door ends the game: message 26 ("completed your quest") when all bits are clear, otherwise 27 ("escaped ... but your quest is incomplete"); then each hero's score
  (experience + 2000), the total, and the high score table (initial entries Richard J Lilley 55000, Sarah 45000, Kevin 35000, Kate 18000, Graham 1000; fn 2C66, 2A96).
- **Creatures per level** (data 0x2BB4, fn 6468; `data/level_creatures.json`): [type, cell x, cell y, level]. Level 2 (Sla-Mori) zombies and wraiths, level 3 Aghar and Kapak with Laurana (type 0x1d) at
  cell (15,1), level 4 two dragons plus Kapak, level 6 Baaz/Bozak/Kapak/Dire Wolves, level 7 Baaz/Bozak. Fights in a dungeon take the creatures in the party's row or column.
- Not done: Gilthanas joining, the armies reaching the south (message 59 is the "you failed" line for a dead party), name entry for the high score, the hidden treasure (59),
  the second dragon's guarded treasure and what makes doors open by themselves.

**Thirteenth run - hidden things, pickup messages and a written epilogue:**
- **Id 59 are hidden traps, not treasure** (fn 6C16): within 24 px, if Tasslehoff leads there is a 45% chance of message 30 ("Tasslehoff locates and removes a trap"), otherwise message 60
  ("A trap is set off") and a missile hits. Find traps and Detect invisible show them (a glitter on the floor) for a while. (The manual's "invisible treasures" are these.) Damage 2-8 is an estimate.
- **Pickup messages:** there is no message per item. The original has: 3 nothing to take, 4 cannot carry more (limit 12 here, estimate), 6 the Staff of Magius burns anyone but Raistlin, 7 the Disks burn anyone but
  Goldmoon, 16 a potion cures wounds, 17 a ring ("more confident"), 20 Wyrmslayer, 40 Inspect finds nothing, 41 a secret door, 22 / 14 only Goldmoon / Raistlin can use an item. All of these are in.
- **Epilogue** (`data/epilogue.json`) is new text written for this version, loosely after the novel Dragons of Autumn Twilight: the escape from Pax Tharkas, the freed prisoners, the dragon
  armies, the winter, Fizban, and Tarsis as the next stop (hinting at the sequel novel). Two versions: quest complete / incomplete. It is shown between the original message and the scores.

**Fourteenth run - Gilthanas, the armies, name entry, and a correction about the wilderness creatures:**
- **The wandering figures at the start are people, not monsters.** Entities 81-94 are nomads/NPCs: on contact the game shows message (type + 15), type = table 0x1DDC[kind]
  (43 hint about Pax Tharkas and Sla-Mori, 45 "guard your flanks", 46 trolls, 47 Gilthanas rumoured near, 48-57 advice, caches of items, "flee south"). They are now drawn as the blue figures and just talk.
- **Armies and monsters (fn 67A2/689C/68EE):** nothing happens to the game when the armies reach the south; instead, each time the view scrolls over a 128-px line, every block along the newly uncovered edge gets
  (army strength >> 5) + 2 random monsters, so the wilderness is thicker with monsters where the fires are. Implemented as `spawn_roaming_monsters` (green figures, chase and fight on contact, at most 30).
  The "You failed in your quest" message (59) is for a dead party.
- **Gilthanas** begins at the pass to Pax Tharkas (cluebook). The exact spot is not in the data I found; he waits at world (656, 2688) next to Sla-Mori's entrance (estimate), says message 45 and joins the party (ninth slot).
- **High score name entry:** after the scores page, a party that makes the table types a name (keyboard text input is new in the platform layer); Enter commits it.

**Fifteenth run - who joins, and where:** the original (driven in the browser) shows message 45 ("Hail companions I will guard your flanks.") after walking south from the start, then a ninth portrait
(the plain man, clip 18) appears: that is Eben, the NPC at world (1024,1200) = kind 4. fn 60D0 says it all: touching an NPC of type T shows message T+15 and joins hero T-20 into a free slot (ten at most),
else replaces a nomad. Types come from table 0x1DDC[kind]: kind 2 -> type 28 -> hero 8 **Gilthanas** (world (1024,2528), "the opening of the pass", message 43), kind 4 -> Eben (10), kinds 3, 5-15 -> Akar, Toragan, Valdor, Cedric,
Hanal, Ash, Barac, Gerath, Camar, Garian, Issus, Rencel (11-22; stats in `data/nomads.json`). In the original the contact starts the side view; here a message box shows on the map. Dead heroes show clip 20 (the dithered portrait).
The fortress: when the army fire reaches the quest map cell of Sla-Mori's entrance (block 4,21) the game ends (my reading; the original's check was not found).

**Sixteenth run - emulator checks and the AD&D-based combat rules (many of these are educated guesses, marked):**
- **Seen in the original (emulator):** hero status = Min/Max header, bars Str Wis Dex HP | Int Con Cha AC, "xx / yy", "Using Range: <weapon>" and "Hand: <weapon>" (Caramon: Spear / Longsword); magic list names are Charm,
  Sleep, Magic missile, Web, Detect magic, Detect invisible, Burning hands (Fire Ball, Final strike below), no charge numbers; Take with nothing near says "There are no items available" (message 3); the nomad Eben joins
  at (1024,1200) with the plain-man portrait. The Amiga disks cannot be read like DOS files (custom loader, no 68000 tools here), and their data files are the same names, so the DOS code remains the source.
- **Weapons** (`data/equipment.json`, from the manual's AD&D statistics): Sturm 2-handed sword +3 (1-10), Caramon longsword (1-8) and spear, Tanis and Riverwind longsword +2 (1-8) and bow (1-6), Tasslehoff hoopak +2 and sling +1 (2-7),
  Flint battle axe, Goldmoon quarterstaff +2, Raistlin Staff of Magius (+2 to hit, 1-8), Gilthanas and Laurana longsword and bow; nomads longsword. THAC0 (guesses): Sturm, Caramon, Flint 14, Tanis, Riverwind 15, others 17-19.
- **Attack:** one swing every 4 steps while the keys are held, d20 >= THAC0 - target AC (AC from the monster table) - bonuses, damage = weapon dice + magic bonus + strength bonus (19+ = +3, 18 = +2, 17 = +1).
  Wyrmslayer with Caramon: d10 + 5, +3 to hit, double against dragons (guess). Bozaks explode when killed (cluebook).
- **Spells (AD&D-style guesses):** Bless +1 to hit; Prayer +1 to hit and -1 for the monsters; Protection from Evil -2 for the monsters; Hold Person freezes up to 3 humanoids; Turn Undead destroys zombies and makes wights and wraiths flee;
  Charm and Sleep (up to 4 hit dice, not undead, not dragons); Web (strong monsters break out in 25 steps); Magic Missile 2 x (d4+1); Burning Hands 2d6; Fire Ball 5d6; Final Strike (charges) damage and it can kill Raistlin.
- **Monsters:** dragons breathe fire (14-30 damage every 90 steps within 260 px), Men shoot bows (1-6), Bozaks cast (2-8), trolls regenerate 1 hit point every 15 steps (they should only die for good to magic: not done).

**Seventeenth run - measured in the original with the memory harness (`tools/emu/mem.py`, see tools/emu/README.md):**
- **Harness:** `index-debug.html` runs DOSBox on the main thread and exposes its RAM; the data segment is found by Goldmoon's ability bytes. Offsets are the same as in these notes.
- **Confirmed live:** hero hit points (24, 45, 20, 36, 44, 40, 11, 52, 17, 18 for records 0-9), the party slot list at 0x30C8 ([2,1,0,3,4,5,6,7,23,23]; 23 = empty slot), quest flags 0x0F at the start, position at 0x361E/0x3622 (1024,1024),
  mode at 0x3038 (2 map, 1 side view), the side-view position 0x3C5C/0x3C5E/0x3C60 (x, y, ground 336), and the combat creature records at 0x3C6A (18 bytes: type, x at +4, y at +6, ground at +8, hit points at +10).
  Touching Eben at (1024,1136) switched to the side view with his record (type 0x1e) in slot 0, and the game then spawned a Bozak, a Man and a Kapak around him.
- **Monster damage confirmed** (a monster poked next to the leader, hero hit points reset after every hit, 30 s each): Troll 3-5 (d3 + 3), Dragon 5-40 (36 + 5), Man 1-8, War Dog 3-8 (6 + 3); Baaz (8 + 1) only hit twice (1, 1).
  So damage = rnd(sides) - 1 + bonus from the 7-byte table at 0x2986 is exactly right, as the engine already has it.
- **Not resolved:** the hit chance (the original's hit counts per 30 s were Troll 38, Dragon 32, Man 21, War Dog 16, Baaz 2, which do not follow the to-hit field the way my formula predicts, so attack rates differ per monster), and the hero's attack:
  holding the keypad `+` with `6` only walked, so the attack chord was not recognised through the browser's key events (the manual's alternative is the RIGHT ARROW next to the 0). Next steps: find the keyboard state variable
  by watching memory while the key is held, and measure hero damage the same way (record the monster's hit points).

**Eighteenth run - hero attack measured in the original (harness key-table trick, tools/emu/exp_hero_attack.py):**
- **There are no character levels.** The manual and cluebook never mention them and a hero with 60000 experience changes no stat (only the score at record +12 changes). Experience is only a score.
- **Attack chord works** by writing the driver's key table (see tools/emu/README.md). Leader at that point = record 4 (Str 20). Damage per hit on a Man, Baaz and War Dog: 4-11 = d8 + 3, i.e. weapon die + strength bonus
  (Str 20 = +3), as the engine does. Troll hit points collapse to 1 rather than reaching 0 (it does not die from plain weapons, as the cluebook says); a Dragon placed 40 px away took no hits.
- **Rate:** against a Man every hit came exactly 2.2 s apart (9 in 20 s), Baaz and War Dog 8 in 20 s with some gaps, Troll about every 0.67 s. The engine swings every 4 logic steps (0.66 s), so it is likely too fast for most
  targets. Open: separate swing rate from hit chance (needs the hero's swing animation or the dice call), and test other heroes and weapons.

**Nineteenth run - updated controls made responsive, gamepad:** the logic step is 3 ticks (165 ms, about 6 a second) and each step moved the party 8 px at once (jumps teleported 48 px), which felt laggy.
With Updated controls (orig_controls_ off, not turbo) `Game::frame_move` runs every rendered frame: first pixel on the key-down frame, then `fast_walk_px_per_s` (data/rules.json, 60) a pixel at a time through the same
collision code (`move_party`), walking animation from distance walked (frame 1 at once, 0 on release), jumps an arc of `jump_ms` (320) with a vertical offset in the draw. Monsters and combat still tick at the original rate.
Original controls are unchanged (move_party is called per step with 8 px). Test hook `DOF_CONTROLS=updated`. Gamepad (SDL GameController, platform_sdl.cpp): stick or d-pad move, A confirm/attack, X attack, B jump (back in menus),
LT/RT jump left/right, Y toggle view, LB/RB previous/next leader, Start/Back menu. Updated controls only (the platform ignores the pad when Original controls is on; `Platform::set_pad_enabled`). A is contextual (`Game::frame`): foe in reach -> melee attack (held), else `context_action`: take the nearest item, else open a door/chest here; Enter in menus. Untested on real hardware.

**Twentieth run - defaults, mouse, jumps, camera:** updated controls are the default (`orig_controls_ = false`, saved key `original_controls`; tests export DOF_CONTROLS=original). Mouse in menus (`step_menu`, updated controls only):
hover selects a row, click confirms, click outside the paper (or on the XP / quest pages) backs out, wheel scrolls; `translate_input` makes the A button and the left mouse button contextual in play (foe in reach -> attack, else take, else open) and a click on a portrait
chooses the leader; the mouse never walks the party. Start = pause (opens the main menu, nothing runs while it is open). World camera clamps to `World::play_area()` (the blocks that differ from the corner block, i.e. inside the grey ring of block 10).
**Jump measured in the original** (harness, key-table trick, leader id poked into data 0x30C8 for ids 0-9): one jump = 14 ticks (165 ms each) of 8 px = 112 px, ground height per tick above the start 0,6,11,15,17,18,17,16,15,14,13,11,7,2,0 (data/jumps.json, `profile`);
all ten leader ids gave the same profile. The code (fn 5228 + the tables at 0x6582 / 0x2846) picks the animation from the key and state only, with no hero test, so no per-hero jump rule was found; the cluebook says Tasslehoff cannot jump rivers, which data/jumps.json
models as a shorter jump (guess, marked). The poke may not switch the hero's sprite, so a per-hero difference hidden in the sprite or collision box is not excluded. Per-hero rules the code does contain: leader 5 avoids 45% of hits (fn 25B2 / 6406 call sites near 1517 and 6020),
leader 3 has its own spell entities (0x19-0x1C), leader 0 the 0x14-0x17 ones; fn 6310 is the monster hit roll: d20 >= table[0x2A0E + hero AC byte] + monster to-hit - 1 (to be checked against the engine).
Original controls keep the 48 px teleporting jump.

**Twenty-first run - title screen (src/title.cpp), controller and ledges, God mode, scrolling boxes:** Title pages: 0 publishers' lines (auto after 7 s), 1 menu (Start New Game, Load Game, Intro, Settings, Credits),
2 credits roll (the manual's credits), 3 intro slideshow (the manual's "The action starts" story, then message 36), 4 settings. `DOF_QUICK_TITLE=1` makes any key start the game (all older tests). Large lettering is `fancy_text` (the 8x8 font scaled, gradient per row, outline, shadow).
Message boxes: six lines of 22 characters fit between the scroll's rollers; longer texts scroll (up / down / wheel, confirm pages down then closes, Esc closes) with blinking arrows. Banner: qtiles 13 (left cap), 14 (middle), 15 (right cap).
Updated controls on the gamepad: B jumps, stick up / down only walk (Input.pad_up / pad_down are ignored for jump and turn), triggers turn the side view (Input.turn), A = foe in reach -> attack, else take, else open/use, else swing the sword, Start opens the menu (never closes it),
Back opens the quest map. Jumps move at walking pace (`jump_dur_ = distance / fast_walk`). Ledges are not walked up in updated controls; `climb` per hero in data/jumps.json. Settings: God mode (`god_mode`, infinite HP and spells).

**Inspect (menu entry 9, fn 2ECC):** only reachable from the menu. In an interior (data 0x3038 == 3) it looks at the party's own cell: table 0x2602 (data/interior_tables.json `secret_flags`) gives per cell value flags; low 3 bits == 1 = hidden door:
the cell value and the neighbours marked by bits 0x10 north / 0x20 east / 0x40 south / 0x80 west (the other half of the door) are incremented (fn 5CDA, then the strip is rebuilt, fn 54CC) -> message 41; cell value 0x5B (91) -> message 62 "This is a false door";
anything else (and everything outdoors) -> message 40. Hidden doors in the data: level 2 at cells (21,2)+(21,3), (6,4)+(6,5), (5,20)+(6,20), (2,25)+(3,25); level 3 (27,9)+(28,9); level 6 (19,14)+(19,15), (25,15)+(25,16); a false door at level 2 (5,5). No other hooks call it.

**Wolves and griffon:** msvf (War Dog, Dire Wolf, Wild Dog) and msvj (Griffon) store each pose as two 32-px frames (body, head); frames 2k + 1 follow 2k, six poses, then five small front views. `data/monsters.json` `wide: 6` joins them into 64-px frames at load
(game.cpp, monster loading) and the three monster draw calls centre a wide frame on the creature. The other monster sheets are single 32-px frames (12 poses + 5 front views).

**Twenty-second run:** title menu: Start New Game, Load Game, Intro, High Scores, Settings, Credits, Exit; publishers' lines -> high scores (page 5, saved in user/scores.json) -> menu. Exit: browser `Module.feExit()` (exit.html), native quit (`quit_`). The in-game main menu has Exit as its last entry.
Friendly people (`Wild::npc` with `joins`): contact -> `start_talk` (side view, `talk_`), the person waits `talk_.x` ahead and dithers in (`Screen::blit_dither`, ordered 4x4) when the party is within 150 px, speaks and joins at 30 px, then `toggle_view()` back; `DOF_QUICK_TALK=1` keeps the old instant message.
Original controls: Q = quest map (`Input.qkey`); F1 = command list (Mode::Help, src/help.cpp), keyboard list for original controls, gamepad list for updated; the old F1-F7 level cheats are Shift+F1-F7. The original's attack: hold keypad + (or Right Arrow) with a direction key.

**Hero poses measured in the original (harness + screenshot matching against the sheet, all heroes share the layout; Sturm and the nomad have 28 frames, Gilthanas 37):** stand 8 (also 0-7 stances), jump rising 18 then falling 19 (action 20, then 9 on landing), crouch / duck 9 (action 7, keypad 1 / 3 without +),
attack: action 1 frame 11 (hand to hand, straight), action 11 frames 15 then 16 (ranged: bow, spear throw), high attack action 16 frames 12-13-14, low attack action 18 frames 28-29-30. Game state byte 0x3C59 = action, 0x3C5A = step within it, 0x3C5B facing.
Engine: `draw_combat` picks 18 / 19 by jump progress (`jump_p_`), attack poses by `aim_` (keypad 7/9 high, 1/3 low with +; stick up/down on the pad). The hit rules do not depend on the aim yet (the original's effect of high / low attacks is unknown; the cluebook only mentions that shooters fire high or low and the hero can jump or duck).

**The way into Pax Tharkas:** the gatehouse at the bottom of the map (door at about world (575, 2890)) is closed (message 43 now shows there). The only way in is Sla-Mori's entrance (level link 1 -> 2 at world (624-640, 2688-2704)); a breadth-first search on the walking rules finds it from the start (about 1300 steps: south down the road, west into the mountains around (290, 2660), east along the passage at y about 2704), and no path from the gate area. Sla-Mori (levels 2 and 3) leads to Pax Tharkas (levels 4, 6, 7).
Hero pose frames are in data/hero_frames.json: the 34-frame sheets share Caramon's measured layout; Sturm and the nomads (28 frames) and Gilthanas (37) are read off their sheets and flagged `guess`.

**Shared pack (updated controls):** `Game::auto_equip` with `data/item_stats.json` (slot hand / range / ammo / ring, which heroes may use it, damage). The hero who gains most (average damage) takes a weapon; the weapon he puts down is offered to the others (up to four rounds), leftovers go to the pack (`inv_`, 12 slots).
Class rules are from the cluebook: Flint alone uses axes, Caramon alone spears, Tasslehoff alone the hoopak, sling and bullets, only bow users need arrows (20 shots a quiver, 10 for +2), a Ring of Protection is never given up. Arrows are only counted (`Hero::ammo`), nothing uses them up yet. Potions and spells stay in the pack for the Use command; Wyrmslayer keeps its special handling.

**Music (twenty-third run):** only the title tune is real music in the original (sound 15, a 25-note loop); every other sound is an effect. The updated build plays rendered loops: `tools/music/` (dof_midi.py = tiny MIDI writer, pieces.py = overland, scroll, combat, dungeon, dcombat, ending; make_title_theme.py = the title theme, which keeps the original's exact tune)
-> `build_music.py SOUNDFONT.sf2` writes music/*.mid, renders with FluidSynth (GeneralUser GS, not part of the project), folds the reverb tail onto the start (gapless) and writes web/static/music_<name>.ogg (about 4.4 MB in all).
The game decides the track (`Game::desired_music`: title, overland, scroll = side view with no foes, combat, dungeon, dcombat = fighting inside a level, ending; menus keep the current one; game over is silent) and calls `Module.feMusic(name)` (web/shell.html: WebAudio, cross-fade, starts after the first key or click);
`Module.feMusicMute` follows V. Settings: Music enhanced / original (original = the PC-speaker title tune through the old sequencer, nothing from the page). Native / headless builds have no audio; `music=` in the debug line shows the chosen track. Test hook DOF_MUSIC=original.
The unchanged pre-music build is in backups/2026-11-11-before-enhanced-music/.
Laurana: while quest flag 8 is set a stationary person (`Wild::who = "Princess Laurana"`, message 44) stands at world (560, 2704) in the passage to the Sla-Mori entrance; the talk gives her rescue (flag cleared, 10000 XP). The Pax Tharkas gate message is custom text.

**Sla-Mori entrance:** a one-tile-high nook; the open cells are (624, 2688), (640, 2688), (656, 2688) joined to the passage at y = 2704 (x 656-688) which the party must enter exactly aligned. With 1-px walking that was nearly impossible, so the updated controls have corner correction (`slide`, up to 8 px) in `move_party`.
The way in is marked by a flashing X on the open cell (640, 2688) and touching it enters level 2 (`check_links`: body overlap with that cell, instead of the original's top-left test of a 16 x 16 patch).

**Menu with the updated controls:** `Game::menu_entries()` builds the main menu: without Exit menu (B), Hero select (LB / RB), Take and Open (A), Give (shared pack), Quest map (Back); with "Controls (F1)" (id 19) after Settings. The menu draws the party's overland position (and the Sla-Mori X) on a line under the scroll;
the quest map draws a pulsing X at the entrance (world / 8). (The tests file lost its Inspect and B-button tests in an earlier rewrite of the title tests; they are back, adapted to the shorter menu.)

**Attacks, ranged weapons and casters (updated controls):** stick centred = low attack (frames per sheet: data/hero_frames.json `low`), stick up = high (`high`), LT / R = `Input.ranged` -> `Game::ranged_attack`: heroes with a ranged weapon fire it while `Hero::ammo > 0` (starting ammo in data/equipment.json; arrows, bullets, spears and throwing axes picked up add shots through data/item_stats.json slot `ammo`);
Raistlin (`caster_attack`) casts Magic missile, Burning hands, Fire ball in that order of escalation per fight (never Final strike) and Goldmoon turn undead (only with undead), Spiritual hammer, Hold person (humanoids not held), and with no foes Cure light wounds (hurt), Bless, Protection from evil (unless still in effect) and Find traps (underground only); the Disks (entries 13-15) are never used by her.
The ranged pose is `ranged_pose_`; the classic keypad scheme is unchanged. In menus `use.confirm` is the single press (it used to be overwritten with the held state, so a key held for more than one logic step acted twice). Jumps also work along the dungeon strip (`move_party`, `frame_move`).
**Entrance check in the original:** with the original party moved by memory to world (648, 2688) and a step west, the original enters Sla-Mori (level 2, about (632, 2688)); at (480, 2880) and (504, 2880) it does nothing. So the entrance is where data/level_links.json says.

**What the menu commands do in the original (manual + code):** Use readies one of the lead's items (arrows, sling bullets and the like must be readied to fire), Give trades an unused item between heroes, Drop drops unused items to the floor, Put places an item into a chest, bag or enclosure (to stash it), Shut closes doors, chests and so on.
**Dispel Magic (clerical entries 10, 11; fn 22CB sets `w6B02 = 2`):** during the next pass of the missile / effect update (fn 6F0A / 7322 loop) every entity whose type is flagged in the table at data 0x1B28 (types 9-15, 20, 21, 42-51, 53-56, 58-61, 63) is removed, so it cancels hostile magical missiles and spell effects already in flight; it does nothing to ordinary arrows or to Web / Hold, and the engine does not implement it yet.
In the original Protection from Evil, Prayer and Bless are three strengths of one defensive effect on the heroes (timers w6E04 for the first two, w69E0 for Bless; fn 6310 makes the monsters' to-hit threshold one higher while Bless is on): the manual calls Bless "the most powerful protection from evil", not a bonus to hit, which is how the engine treats it (guess, to revisit).
**Projectiles (missle.dat):** table at 0x10 of 8-byte records [offset of a frame-offset list, width in words, height, ?]; record 6 / 7 = the spinning axe (8 frames 16x15), 8 = the long spear (32x3), 10 and 12 = arrows (16x5), 9 = sling stones, 0 = a small burst, 2-5 = big bursts; hero shots are created by fn 6F0A(type, ..., speed 15, x, y, z, vx = +-(1d6+13)).
The engine: `Assets::missile`, `Game::missiles_` (flight, impact, `drop_thrown` -> a `GroundItem` with `thrown`), `floaters_` (damage numbers), `equip.cpp` (Equip / Inventory pages, `menu_page_` 20-22).

## Dungeon minimap (Game::draw_minimap)
Per-level `dmap_` (0 unknown, 1 seen along a passage, 2 explored); drawn top right in draw_interior, 9x5 cells of 6 px centred on the party, walls from iexits_ bits (1 N, 2 E, 4 S, 8 W); cleared on new game.
