# Dragons of Flame, HTML5 rewrite

A from-scratch engine (C++, compiled to WebAssembly) that plays SSI's *Dragons of Flame* (DOS, 1989) in your browser.
This is a fan project and is not affiliated with or endorsed by the owners of the game or of Dragonlance.

## Install (about a minute)

You need **your own copy of the original game** (the folder that holds `GAME.EXE`) and **Python 3** (python.org; on Windows tick "Add to PATH").

1. Download this repository: green **Code** button, **Download ZIP**, and unpack it anywhere.
2. Run the installer:
   - Windows: double-click `install.bat`
   - Linux / Mac: `./install.sh` (or `python3 install.py`)
3. When it asks, type or drag in the folder where your original game is. (You can also give it on the command line: `python3 install.py /path/to/game`.)

It reads your files (they are never changed), builds the game in a local folder called `play/`, and opens it in your browser at `http://127.0.0.1:8780/`.
Leave its window open while you play. To play again later, run the same file again; it starts straight away.
After you download a newer version of this repository, run `install.py --reinstall`.

Nothing is uploaded anywhere, and this repository contains none of the original game's files (graphics, music, maps or text).

## Playing
Arrow keys, WASD or the numpad move; Space opens the menu; 0 or Tab switches between the map and the side view; F1 shows all commands; F10 is the debug menu.
Game controller: the stick moves, buttons as shown in F1.

## For developers
`src/` is the engine, `data/` the rules files you can edit (monsters, items, points of interest ...), `docs/` how the original works,
`tools/` decoders. Native tests (need the original files in `game/`): `cmake -S . -B build && cmake --build build && tests/run.sh`.
Browser build (Emscripten): `web/build.sh`; the prebuilt engine is in `dist/`. The version history is in `web/CHANGES.txt`.

The HTML5 version was created by Mark Barazzuol, with Claude Code. Take-down requests from rights holders will be respected.
