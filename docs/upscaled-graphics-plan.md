# Upscaled graphics plan (not started)

Goal: keep the game exactly as it is when "Original 16-colour (EGA)" is selected, and add an optional modern mode with high-resolution menus and text,
256-colour or true-colour art, and modern effects. Nothing here changes game rules. Do the phases in order; each one ends with the original mode still
looking identical (compare screenshots) and `tests/run.sh` passing.

## Where we are today
- Everything is drawn by `Screen` (`src/gfx.*`) into one 320x200 buffer of 16 palette indexes, then `platform_sdl.cpp` converts it to RGBA, uploads one texture and
  stretches it to a 320x240 logical size (4:3).
- All drawing code is in `src/game.cpp` (about 1,500 lines): world, side view, dungeon, menus, scrolls, panel, ending. It writes pixels straight into the indexed buffer.
- Game logic uses its own units (world pixels, strip pixels, cell pixels). It does not depend on how the picture is shown. This is what makes the plan possible.
- Art comes from the original `.dat` files, decoded in `src/assets.*` into indexed `Image`s.
- A "256-colour" option exists in Settings, disabled, as a placeholder.

## Phase 0 - Refactor (about 2-3 hours, behaviour must not change)
1. Split `game.cpp` into files by job: `game_world.cpp` (overhead map), `game_side.cpp` (strip fights), `game_dungeon.cpp`, `game_menu.cpp`, `game_ui.cpp` (panel, scrolls),
   `game_story.cpp` (quest flags, ending), `game_init.cpp`. Keep `Game` as the owner of state.
2. Move all drawing out of the game logic functions into a `Draw*` set of functions that take a render target, so a second renderer can be added later.
3. Replace pixel positions typed into the code with named constants or a small layout table (`data/layout.json`): panel slots, scroll geometry, menu line positions.
4. Move the test hooks (`DOF_*` environment variables) into one `Cheats` struct.
Check: screenshots of title, world, side view, dungeon, menu, scroll, panel, ending are pixel-identical before and after.

## Phase 1 - A renderer with layers (about 1 day)
1. Add `Renderer` (interface) with two layers:
   - **Base layer**: the 320x200 picture exactly as now (indexed buffer plus palette), integer-scaled (3x, 4x, ...) with nearest-neighbour filtering and the 4:3 aspect.
   - **Overlay layer**: drawn at the window's native resolution, on top, for modern UI.
2. Convert the indexed buffer to RGBA once per frame in the renderer, not in the platform code. Keep a palette per frame so palette changes (title screen) still work.
3. Window handling: letterbox the base layer, let the overlay use the whole window. Mouse coordinates are given to the game in both spaces.
4. Settings: "Display" (original look / modern look) alongside the graphics option. Original look = base layer only.
Check: original look is pixel-identical to today at 4x.

## Phase 2 - True-colour pipeline (about 1 day)
1. Introduce `Surface` (RGBA32) next to `Image` (indexed). The base layer can be either; the renderer composites them.
2. Palette-to-RGBA conversion happens at load time for anything that is shown in "modern" mode, so the original files are never modified.
3. Asset override: `Assets` looks for `assets/modern/<name>.png` (plus an optional `<name>.json` for frame sizes and anchor points) before decoding the original `.dat`.
   Same names as the originals (`wtiles`, `stokens`, hero and monster sheets, clips, panel). Missing files fall back to the original art, so art can be replaced piece by piece.
4. Pack system: put modern art in its own packs (`dfpk`), loaded only when the modern look is on, so the original download stays small.
5. The Settings page enables the "256-colour / modern" choice once at least the tiles and heroes exist.

## Phase 3 - High-resolution UI (about 2-3 days; the biggest piece)
1. Add a font renderer: `stb_truetype` (single header, public domain) baked into an atlas at the window's scale. Ship one or two open-licence fonts.
2. Re-create the scroll menus, spell lists, hero status, quest map frame, message boxes and the ending as overlay widgets: vector-drawn frames or 9-slice PNGs, real text,
   mouse hover and click on every entry (keyboard stays).
3. Keep the old scroll look as the "original look" so nothing is lost; the modern widgets are an alternative drawn by the same menu code through a small `Theme` interface.
4. Portraits and the panel: draw at window resolution from larger portrait art when available, otherwise upscale the 32x32 art with nearest-neighbour (crisp, not blurred).
Check: every menu works with keyboard only, with mouse only, and in both looks.

## Phase 4 - Effects (about 2-4 days, after the above)
1. Presentation through a shader path: on the web, WebGL via SDL's OpenGL ES 2 renderer (or a small WebGL canvas); natively, OpenGL. Keep the plain SDL texture path as the fallback.
2. Post-effects, each switchable in Settings and off in the original look: bloom on fires and spells, soft lighting in dungeons (torches), a vignette, mild screen shake on hits,
   optional CRT filter (scanlines, curvature) for people who want the DOS look.
3. Particles drawn by the overlay layer in world coordinates: spell effects, fire marks on the quest map, dust, water sparkle.
4. Animated water and grass in modern mode (tile animation via the override files).
5. Sound can follow the same override idea (`assets/modern/sound/*.ogg`), with the PC speaker music as the original.

## Things to decide before starting
- Target sizes: base scale (4x gives 1280x960) and what the minimum browser window should be.
- True colour or 256 colours: true colour is simpler to author and render; 256 colours matches the Settings label. Recommendation: render in true colour, author in whatever the
  artist likes, and call the setting "Modern graphics".
- Art source: redraw, AI upscaling of the originals (tile by tile, reviewed by hand), or a mix. The override system allows all three.
- Licences of fonts and any third-party art.

## Risks
- Phase 3 touches every menu; do the refactor first or the menu code will be painful.
- Shader effects behave differently on phones and old browsers; keep the fallback.
- Download size: modern art must live in optional packs.
- Keep the two looks in step: any new feature must work in both.

## Order of work
Refactor -> layered renderer -> true-colour surfaces and asset override -> modern UI widgets -> art replacement (tiles, heroes, monsters, panel) -> effects.
