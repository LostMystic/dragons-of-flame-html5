#!/usr/bin/env python3
"""Dragons of Flame (HTML5): installer and launcher. Needs only Python 3.

    python3 install.py                      asks where your copy of the original game is, installs, and starts the game in your browser
    python3 install.py /path/to/the/game    the same without the question (the folder that holds GAME.EXE, or the GAME.EXE file itself)
    python3 install.py                      run again later: starts the game that is already installed
    python3 install.py --reinstall [path]   install again (after updating this folder)

Your original files are only read, never changed. They are copied into the local folder play/ (about 1 MB each pack, made in your own
copy only); nothing is uploaded anywhere. The game is served to your own browser from http://127.0.0.1:<port>/ while this window is open.
"""
import http.server, os, shutil, socketserver, subprocess, sys, tempfile, threading, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
PLAY = os.path.join(HERE, "play")
sys.path.insert(0, os.path.join(HERE, "tools"))


def find_game(arg):
    cands = [arg] if arg else []
    cands += [os.path.join(HERE, "game"), HERE, os.getcwd()]
    for c in cands:
        if not c:
            continue
        c = os.path.abspath(os.path.expanduser(c.strip().strip('"').strip("'")))
        if os.path.isfile(c):
            c = os.path.dirname(c)
        if os.path.isdir(c):
            names = {n.lower(): n for n in os.listdir(c)}
            if "game.exe" in names:
                return c, names
    return None, None


def install(game_dir, names):
    import install_data
    dat = [n for l, n in names.items() if l.endswith(".dat")]
    if len(dat) < 20:
        sys.exit("That folder has GAME.EXE but only %d .DAT files; it does not look like a complete copy of Dragons of Flame." % len(dat))
    stage = tempfile.mkdtemp(prefix="dof-install-")
    try:
        os.makedirs(os.path.join(stage, "game")); os.makedirs(os.path.join(stage, "web"))
        for n in dat:
            shutil.copyfile(os.path.join(game_dir, n), os.path.join(stage, "game", n.lower()))
        shutil.copytree(os.path.join(HERE, "data"), os.path.join(stage, "data"))
        print("Reading the original game's maps and texts ...")
        install_data.extract(game_dir if "game.exe" in names else game_dir, os.path.join(stage, "data"))
        shutil.copyfile(os.path.join(HERE, "web", "CHANGES.txt"), os.path.join(stage, "web", "CHANGES.txt"))
        if os.path.isdir(PLAY):
            shutil.rmtree(PLAY)
        shutil.copytree(os.path.join(HERE, "dist"), PLAY)
        dist = os.path.join(HERE, "dist")
        extras = [os.path.join(PLAY, f) for f in ("dragons-of-flame.html", "dragons-of-flame.js", "dragons-of-flame.wasm", "packs.js", "exit.html")]
        env = dict(os.environ, DOF_ROOT=stage)
        print("Packing the game ...")
        subprocess.run([sys.executable, os.path.join(HERE, "tools", "web", "pack.py"), PLAY] + extras, check=True, env=env)
        shutil.copyfile(os.path.join(PLAY, "dragons-of-flame.html"), os.path.join(PLAY, "index.html"))
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def serve():
    class H(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **k):
            super().__init__(*a, directory=PLAY, **k)
        def log_message(self, *a):
            pass
        def end_headers(self):
            self.send_header("Cache-Control", "no-cache")
            super().end_headers()
    H.extensions_map = dict(H.extensions_map, **{".wasm": "application/wasm", ".js": "text/javascript"})
    socketserver.TCPServer.allow_reuse_address = True
    srv = None
    for port in [8780] + list(range(8781, 8800)):
        try:
            srv = socketserver.ThreadingTCPServer(("127.0.0.1", port), H); break
        except OSError:
            continue
    if not srv:
        sys.exit("Could not find a free port to start the game on.")
    url = "http://127.0.0.1:%d/" % srv.server_address[1]
    print("\nThe game is running at %s  (opening your browser)\nLeave this window open while you play; press Ctrl+C to stop." % url)
    threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    have = os.path.exists(os.path.join(PLAY, "version.json"))
    if have and not args and "--reinstall" not in flags:
        return serve() if "--no-serve" not in flags else None
    game, names = find_game(args[0] if args else None)
    if not game:
        print("Where is your copy of the original Dragons of Flame (the folder with GAME.EXE in it)?")
        if args:
            print("(%s has no GAME.EXE)" % args[0])
        while not game:
            try:
                ans = input("Folder: ")
            except EOFError:
                sys.exit(1)
            game, names = find_game(ans)
            if not game:
                print("No GAME.EXE there. Try again (you can drag the folder into this window).")
    print("Found the original game in", game)
    install(game, names)
    print("Installed.")
    if "--no-serve" not in flags:
        serve()


if __name__ == "__main__":
    main()
