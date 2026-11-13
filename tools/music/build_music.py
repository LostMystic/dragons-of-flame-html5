#!/usr/bin/env python3
"""Builds all of the game's music: writes the MIDI files (music/*.mid), renders each with FluidSynth and a General MIDI SoundFont,
folds the reverb tail back onto the start so the loop is seamless, and encodes Ogg Vorbis files for the web build (web/static/music_*.ogg).

    python3 tools/music/build_music.py /path/to/GeneralUser-GS.sf2 [name ...]
(GeneralUser GS is free to use; it is only needed here, the game plays the rendered files.)
"""
import os, subprocess, sys, tempfile, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
from pieces import PIECES

SR = 44100


def render(mid, sf, loop_seconds, out_ogg, tail=5.0):
    with tempfile.TemporaryDirectory() as td:
        wav = os.path.join(td, "r.wav")
        subprocess.run(["fluidsynth", "-ni", "-g", "0.7", "-R", "1", "-C", "1", "-F", wav, "-r", str(SR), sf, mid], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        w = wave.open(wav)
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels()).astype(np.float32) / 32768.0
        w.close()
    L = int(round(loop_seconds * SR))
    need = L + int(tail * SR)
    if len(raw) < need:
        raw = np.vstack([raw, np.zeros((need - len(raw), raw.shape[1]), np.float32)])
    body, tl = raw[:L].copy(), raw[L:need]
    body[:len(tl)] += tl                                   # the tail of the last notes rings into the start of the loop
    # a few milliseconds of fade at the very ends of the loop body keep the seam click-free
    fade = int(0.004 * SR)
    body[:fade] *= np.linspace(0, 1, fade)[:, None]; body[-fade:] *= np.linspace(1, 0, fade)[:, None]
    peak = float(np.max(np.abs(body)))
    body *= 0.89 / max(peak, 1e-6)
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "loop.wav")
        w = wave.open(out, "wb"); w.setnchannels(body.shape[1]); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(body, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", out, "-c:a", "libvorbis", "-q:a", "4", out_ogg], check=True)
    rms = float(np.sqrt(np.mean(body ** 2)))
    return peak, rms


def main():
    sf = sys.argv[1]
    only = sys.argv[2:]
    os.makedirs(os.path.join(ROOT, "music"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "web", "static"), exist_ok=True)
    jobs = []
    for name, fn in PIECES.items():
        if only and name not in only:
            continue
        piece, names = fn()
        mid = os.path.join(ROOT, "music", name + ".mid")
        secs = piece.write(mid, names)
        jobs.append((name, mid, secs))
    if not only or "title" in only:
        mid = os.path.join(ROOT, "music", "title_theme_enhanced.mid")
        subprocess.run([sys.executable, os.path.join(HERE, "make_title_theme.py"), mid], check=True, stdout=subprocess.DEVNULL)
        jobs.append(("title", mid, 32 * 60.0 / (60.0 / (8 / 18.2065))))
    for name, mid, secs in jobs:
        ogg = os.path.join(ROOT, "web", "static", "music_%s.ogg" % name)
        peak, rms = render(mid, sf, secs, ogg)
        print("%-9s %5.1f s  peak %.2f  rms %.3f  %6.0f KB  %s" % (name, secs, peak, rms, os.path.getsize(ogg) / 1024, os.path.relpath(ogg, ROOT)))


if __name__ == "__main__":
    main()
