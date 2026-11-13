# Dragons of Flame, HTML5 rewrite

A from-scratch C++ engine (native headless for tests, WebAssembly for the browser) for SSI's *Dragons of Flame* (DOS, 1989).
This is a fan project and is not affiliated with or endorsed by the owners of the game or of Dragonlance.

**Bring your own copy.** This repository contains only the engine, the tools and the rules/data files written for it. It does **not** contain the
original game's files (graphics, music, maps, text) or any disassembly of them. To run it you need your own legally obtained copy of the
original game in `game/`; some data files (world map, level maps, message text) are dumped from it with `tools/extract_data.py`, and the
extractor, `docs/formats.md` and `docs/engine-notes.md` describe the formats.

Take-down requests from rights holders will be respected.

## Build and test
```
cmake -S . -B build && cmake --build build && tests/run.sh      # native, headless tests (need the original files)
web/build.sh                                                    # browser build (Emscripten)
```
Rules live in `data/*.json` (monsters, items, points of interest, encounters...). The version history is in `web/CHANGES.txt`.
The HTML5 version was created by Mark Barazzuol, with Claude Code.
