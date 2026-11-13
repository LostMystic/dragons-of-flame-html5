"""A tiny toolkit for writing the game's music as Standard MIDI Files (General MIDI instruments), loopable.

Notes are written as names ("D5", "Bb3", "F#4"); melodies as text: "A4:1 D5:1 F5:1.5 E5:.5 r:1" (note:beats, r = rest).
Chords are written as names ("Dm", "Bb", "A7", "Bdim", "Em7", "Dsus4", "E5").  Times are in beats (quarter notes).
"""
import random, struct

PPQ = 480
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
QUAL = {"": [0, 4, 7], "m": [0, 3, 7], "dim": [0, 3, 6], "7": [0, 4, 7, 10], "m7": [0, 3, 7, 10], "sus4": [0, 5, 7], "5": [0, 7], "maj7": [0, 4, 7, 11], "aug": [0, 4, 8], "m6": [0, 3, 7, 9]}

# General MIDI drum notes
KICK, SNARE, SIDE, HAT, OPENHAT, CRASH, RIDE, TOM_LO, TOM_MID, TOM_HI, TAMB, SHAKER, FLOORTOM, CLAVES = 36, 38, 37, 42, 46, 49, 51, 41, 45, 48, 54, 70, 43, 75


def nm(s):
    pc = PC[s[0]]
    i = 1
    while i < len(s) and s[i] in "#b":
        pc += 1 if s[i] == "#" else -1
        i += 1
    return 12 * (int(s[i:]) + 1) + pc


def parse_chord(s):
    pc = PC[s[0]]
    i = 1
    while i < len(s) and s[i] in "#b":
        pc += 1 if s[i] == "#" else -1
        i += 1
    return pc % 12, QUAL[s[i:]]


def chord_notes(s, center=62):
    """Close-position notes of a chord, moved to sit around `center`."""
    pc, iv = parse_chord(s)
    base = 48 + pc
    notes = [base + i for i in iv]
    mean = sum(notes) / len(notes)
    shift = round((center - mean) / 12) * 12
    return [n + shift for n in notes]


def root_note(s, low=36):
    pc, _ = parse_chord(s)
    return low + (pc - low) % 12


def fifth_note(s, low=36):
    pc, iv = parse_chord(s)
    return root_note(s, low) + (7 if 7 in iv else iv[-1])


class Piece:
    def __init__(self, title, bpm, bars, beats_per_bar=4, seed=1):
        self.title, self.bpm, self.bars, self.bpb = title, bpm, bars, beats_per_bar
        self.beats = bars * beats_per_bar
        self.ev = {}
        self.rng = random.Random(seed)

    # --- low level
    def _add(self, ch, tick, data, order):
        self.ev.setdefault(ch, []).append((tick, order, bytes(data)))

    def setup(self, ch, program, vol=100, pan=64, reverb=70, chorus=0):
        if ch != 9:
            self._add(ch, 0, [0xC0 | ch, program], 0)
        for n, v in ((7, vol), (10, pan), (91, reverb), (93, chorus)):
            self._add(ch, 0, [0xB0 | ch, n, v], 0)

    def note(self, ch, pitch, start, length, vel=80):
        if start >= self.beats:
            return
        v = max(1, min(127, int(vel) + self.rng.randint(-3, 3)))
        self._add(ch, int(round(start * PPQ)), [0x90 | ch, pitch, v], 1)
        end = min(self.beats * PPQ - 2, int(round((start + length) * PPQ)) - 2)
        self._add(ch, max(end, int(round(start * PPQ)) + 1), [0x80 | ch, pitch, 0], 0)

    def cc(self, ch, beat, n, v):
        self._add(ch, int(round(beat * PPQ)), [0xB0 | ch, n, v], 0)

    # --- melodies
    def mel(self, ch, text, start=0.0, vel=85, trans=0, legato=0.95):
        t = start
        for tok in text.replace("|", " ").split():
            name, _, beats = tok.partition(":")
            b = float(beats) if beats else 1.0
            if name != "r":
                self.note(ch, nm(name) + trans, t, b * legato, vel)
            t += b
        return t

    # --- accompaniment helpers; `segs` is a list of (chord name, beats) covering the piece
    def segments(self, bars_chords):
        """['Dm', 'Bb', 'F,C', ...] one entry per bar; 'F,C' = two chords of two beats. Returns [(chord, start, beats)]."""
        out, t = [], 0.0
        for entry in bars_chords:
            parts = entry.split(",")
            d = self.bpb / len(parts)
            for c in parts:
                out.append((c, t, d))
                t += d
        return out

    def pad(self, ch, segs, vel=55, center=62, gap=0.02):
        for c, s, d in segs:
            for n in chord_notes(c, center):
                self.note(ch, n, s, d - gap, vel)

    def bass(self, ch, segs, style="qr5", vel=95, low=36):
        for c, s, d in segs:
            r, f = root_note(c, low), fifth_note(c, low)
            if style == "drone":
                self.note(ch, r, s, d * 0.98, vel)
            elif style == "half":
                k = 0.0
                while k < d:
                    self.note(ch, r if (k / 2) % 2 == 0 else f, s + k, min(2, d - k) * 0.95, vel)
                    k += 2
            elif style == "qr5":
                for k in range(int(d)):
                    self.note(ch, r if k % 2 == 0 else f, s + k, 0.9, vel - (6 if k % 2 else 0))
            elif style == "eighth":
                for k in range(int(d * 2)):
                    self.note(ch, r, s + k * 0.5, 0.45, vel + (8 if k % 2 == 0 else -6))
            elif style == "gallop":   # an eighth and two sixteenths on each beat
                for k in range(int(d)):
                    self.note(ch, r, s + k, 0.5, vel + 6)
                    self.note(ch, r, s + k + 0.5, 0.25, vel - 8)
                    self.note(ch, r + (12 if k % 2 else 0), s + k + 0.75, 0.25, vel - 4)
            elif style == "walk":
                for k in range(int(d)):
                    self.note(ch, [r, r, f, r + 12][k % 4] if k < 4 else r, s + k, 0.9, vel)

    def arp(self, ch, segs, style="up8", vel=60, center=58, step=0.5):
        for c, s, d in segs:
            tones = sorted(chord_notes(c, center))
            if len(tones) < 4:
                tones = tones + [tones[0] + 12]
            seq = {"up8": [0, 1, 2, 3, 2, 1], "up": [0, 1, 2, 3], "down": [3, 2, 1, 0], "broken": [0, 2, 1, 3, 1, 2], "rock": [0, 1, 2, 1]}[style]
            for k in range(int(d / step)):
                self.note(ch, tones[seq[k % len(seq)]], s + k * step, step * 1.1, vel + (6 if k % 2 == 0 else -4))

    def offbeats(self, ch, segs, vel=70, center=60, length=0.3, beats=(0.5, 1.5, 2.5, 3.5)):
        for c, s, d in segs:
            for b in beats:
                if b < d:
                    for n in chord_notes(c, center):
                        self.note(ch, n, s + b, length, vel)

    def timp(self, ch, segs, per_bar=(0,), vel=90, low=36):
        for c, s, d in segs:
            for b in per_bar:
                if b < d:
                    self.note(ch, root_note(c, low) if root_note(c, low) <= 47 else root_note(c, low) - 12, s + b, 1.2, vel)

    # --- drums (channel 9); a bar at a time
    def drums_bar(self, bar, style, level=1.0, fill=False, crash=False):
        t0 = bar * self.bpb
        v = lambda x: x * level
        n = lambda p, b, l, x: self.note(9, p, t0 + b, l, v(x))
        if crash:
            n(CRASH, 0, 2, 92)
        if style == "march":
            for b in (0, 2): n(KICK, b, 0.5, 96 if b == 0 else 82)
            for b in (1, 3): n(SIDE, b, 0.3, 72)
        elif style == "adventure":
            for b in (0, 2, 2.5): n(KICK, b, 0.4, 94 if b != 2.5 else 70)
            n(TOM_MID, 1.5, 0.3, 74); n(SNARE, 3, 0.3, 86); n(SIDE, 1, 0.3, 64)
        elif style == "peaceful":
            for b in (1, 3): n(TAMB, b, 0.3, 54)
            n(KICK, 0, 0.5, 60)
        elif style == "combat":
            for b in (0, 1.5, 2, 3.5): n(KICK, b, 0.4, 104)
            for b in (1, 3): n(SNARE, b, 0.3, 110)
            for k in range(8): n(HAT, k * 0.5, 0.2, 62 if k % 2 == 0 else 48)
        elif style == "dcombat":
            for b in (0, 0.75, 2, 2.75): n(KICK, b, 0.4, 106)
            n(SNARE, 1, 0.3, 108); n(SNARE, 3, 0.3, 108)
            for b in (1.5, 3.5): n(TOM_LO, b, 0.3, 96)
            for k in range(4): n(RIDE, k, 0.4, 56)
        elif style == "tom":
            for b, p in ((0, FLOORTOM), (1.5, TOM_LO), (2, FLOORTOM), (3, TOM_LO)): n(p, b, 0.5, 84)
        elif style == "sparse":
            n(FLOORTOM, 0, 1, 56); n(FLOORTOM, 2, 1, 44)
        if fill:
            for k, p in enumerate((TOM_HI, TOM_MID, TOM_LO, FLOORTOM)):
                n(p, self.bpb - 1 + k * 0.25, 0.25, 84 + k * 6)

    def drums(self, bars_styles, level=1.0):
        """bars_styles: list of (style, bars, options...) e.g. [("march", 4), ("combat", 8, "fill")]"""
        bar = 0
        for item in bars_styles:
            style, count = item[0], item[1]
            opts = item[2:]
            for k in range(count):
                self.drums_bar(bar, style, level, fill=("fill" in opts and k == count - 1), crash=("crash" in opts and k == 0))
                bar += 1
        return bar

    # --- output
    def write(self, path, names=None):
        def varlen(n):
            out = [n & 0x7F]
            n >>= 7
            while n:
                out.append((n & 0x7F) | 0x80)
                n >>= 7
            return bytes(reversed(out))

        end = int(self.beats * PPQ)

        def track(items, name):
            items = sorted(items, key=lambda x: (x[0], x[1]))
            out = bytearray(b"\x00\xff\x03" + bytes([len(name)]) + name.encode())
            last = 0
            for tick, _, data in items:
                out += varlen(tick - last) + data
                last = tick
            out += varlen(max(0, end - last)) + b"\xff\x2f\x00"
            return b"MTrk" + struct.pack(">I", len(out)) + bytes(out)

        tempo = int(60_000_000 / self.bpm)
        title = self.title.encode()
        cond = b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big") + b"\x00\xff\x58\x04" + bytes([self.bpb, 2, 24, 8]) + b"\x00\xff\x03" + bytes([len(title)]) + title + varlen(end) + b"\xff\x2f\x00"
        chunks = [b"MTrk" + struct.pack(">I", len(cond)) + cond] + [track(self.ev[ch], (names or {}).get(ch, "channel %d" % ch)) for ch in sorted(self.ev)]
        data = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ) + b"".join(chunks)
        open(path, "wb").write(data)
        return self.beats * 60.0 / self.bpm
