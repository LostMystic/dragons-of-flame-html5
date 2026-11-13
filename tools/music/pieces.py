"""The game's music: six pieces written for Dragons of Flame (the seventh, the title theme, is tools/music/make_title_theme.py).
Every piece is loopable (it ends where it began, harmonically, and build_music.py folds the reverb tail back onto the start).

  overland   the map: adventure with a little tension           (D minor, 100 BPM, 24 bars)
  scroll     the side view with no monsters: the same tune, calmer (D minor, 78 BPM, 16 bars)
  combat     fighting outdoors                                    (D minor, 150 BPM, 24 bars)
  dungeon    exploring Sla-Mori and Pax Tharkas                   (A minor / Phrygian, 60 BPM, 16 bars)
  dcombat    fighting underground                                 (A minor, 136 BPM, 20 bars)
  ending     the ending scene: hopeful, a little sad              (D major, 72 BPM, 16 bars)
"""
from dof_midi import *

# General MIDI programs
HORN, TRUMPET, TROMBONE, TUBA, BRASS = 60, 56, 57, 58, 61
OBOE, FLUTE, CLARINET = 68, 73, 71
HARP, GUITAR, CELESTA, GLOCK = 46, 24, 8, 9
STRINGS, TREM, PIZZ, CELLO, BASSVIOLIN = 48, 44, 45, 42, 43
TIMPANI, CHOIR, OOHS, PAD, ORGAN, HIT, ABASS = 47, 52, 53, 89, 19, 55, 32


def overland():
    p = Piece("Dragons of Flame - the road", 100, 24, seed=11)
    HORN_C, TRUMP_C, PAD_C, BASS_C, CHOP_C, HARP_C, FLUTE_C, OBOE_C, TIMP_C, TREM_C, CELLO_C = 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11
    for ch, prog, vol, pan in ((HORN_C, HORN, 100, 52), (TRUMP_C, TRUMPET, 92, 76), (PAD_C, STRINGS, 84, 64), (BASS_C, ABASS, 104, 64), (CHOP_C, PIZZ, 92, 58),
                               (HARP_C, HARP, 92, 38), (FLUTE_C, FLUTE, 86, 90), (OBOE_C, OBOE, 98, 70), (TIMP_C, TIMPANI, 100, 64), (TREM_C, TREM, 86, 60), (CELLO_C, CELLO, 92, 80)):
        p.setup(ch, prog, vol, pan, 70)
    p.setup(9, 0, 100, 64, 40)
    A = ["Dm", "Bb", "F", "C", "Dm", "Bb", "C", "A"]
    B = ["Gm", "Dm", "Bb", "A", "Gm", "Dm", "A", "A"]
    segs = p.segments(A + B + A)
    # the tune
    mA = "A4:1 D5:1 F5:1.5 E5:.5 | D5:1 Bb4:1 D5:1 F5:1 | C5:1.5 A4:.5 C5:1 F5:1 | E5:2 D5:1 C5:1 | D5:1 F5:1 A5:1.5 G5:.5 | F5:1 D5:1 F5:1 G5:1 | E5:1.5 D5:.5 E5:1 G5:1 | C#5:1 E5:1 A5:2"
    mB = "G4:1 Bb4:1 D5:1 C5:1 | A4:1 D5:1 F5:2 | Bb4:1 D5:1 F5:1 E5:1 | C#5:1 E5:1 G5:2 | G5:1.5 F5:.5 D5:1 Bb4:1 | A4:1 D5:1 A4:1 F4:1 | C#5:1 E5:1 A5:1 G5:1 | A5:3 r:1"
    p.mel(HORN_C, mA, 0, 92)
    p.mel(OBOE_C, mB, 32, 92)
    p.mel(HORN_C, mA, 64, 96); p.mel(TRUMP_C, mA, 64, 80); p.mel(FLUTE_C, mA, 64, 62, trans=12)
    p.mel(CELLO_C, mA, 0, 60, trans=-24)
    # harmony and rhythm
    p.pad(PAD_C, segs[:8], 50); p.pad(PAD_C, segs[8:16], 36); p.pad(PAD_C, segs[16:], 58)
    p.pad(TREM_C, segs[8:16], 52, center=60)
    p.bass(BASS_C, segs[:8], "qr5", 96); p.bass(BASS_C, segs[8:16], "half", 94); p.bass(BASS_C, segs[16:], "qr5", 100)
    p.arp(HARP_C, segs[:8], "up8", 58, center=60)
    p.offbeats(CHOP_C, segs[4:8], 66, center=62); p.offbeats(CHOP_C, segs[16:], 72, center=62)
    p.timp(TIMP_C, segs[:8], (0, 2), 84); p.timp(TIMP_C, segs[8:16], (0, 1, 2, 3), 80); p.timp(TIMP_C, segs[16:], (0, 2), 92)
    p.drums([("march", 4, "crash"), ("adventure", 4, "fill"), ("march", 7), ("tom", 1, "fill"), ("adventure", 4, "crash"), ("adventure", 4, "fill")], 1.0)
    return p, {HORN_C: "Horn", TRUMP_C: "Trumpet", PAD_C: "Strings", BASS_C: "Bass", CHOP_C: "Pizzicato", HARP_C: "Harp", FLUTE_C: "Flute", OBOE_C: "Oboe",
               TIMP_C: "Timpani", TREM_C: "Tremolo strings", CELLO_C: "Cello", 9: "Drums"}


def scroll():
    p = Piece("Dragons of Flame - a quiet stretch of road", 78, 16, seed=12)
    FL, OB, HP, GT, PD, BS, GL, CL = 0, 1, 2, 3, 4, 5, 6, 7
    for ch, prog, vol, pan in ((FL, FLUTE, 98, 60), (OB, OBOE, 88, 72), (HP, HARP, 94, 40), (GT, GUITAR, 86, 80), (PD, STRINGS, 66, 64), (BS, ABASS, 90, 64), (GL, GLOCK, 70, 54), (CL, CELLO, 74, 50)):
        p.setup(ch, prog, vol, pan, 80)
    p.setup(9, 0, 70, 64, 50)
    segs = p.segments(["Dm", "Bb", "F", "C", "Dm", "Bb", "C", "A", "Dm", "Bb", "F", "C", "Dm", "Bb", "C", "Dm"])
    m1 = "A4:2 D5:1 F5:1 | D5:2 Bb4:1 D5:1 | C5:2 A4:1 C5:1 | E5:2 D5:1 C5:1 | D5:2 F5:1 A5:1 | G5:2 F5:1 D5:1 | E5:2 D5:1 E5:1 | C#5:2 E5:1 D5:1"
    m2 = "A4:1.5 D5:.5 F5:2 | D5:2 Bb4:2 | A4:1.5 C5:.5 F5:2 | E5:2 G5:1 E5:1 | D5:1 F5:1 A5:2 | G5:1 F5:1 D5:1 F5:1 | E5:2 D5:1 C5:1 | D5:4"
    p.mel(FL, m1, 0, 80); p.mel(OB, m2, 32, 78); p.mel(FL, m2, 32, 60, trans=12)
    p.mel(GL, "A5:1 r:3 | r:4 | r:4 | r:4", 0, 0) if False else None
    p.arp(HP, segs, "up8", 54, center=58)
    p.arp(GT, segs[8:], "broken", 50, center=55, step=1.0)
    p.pad(PD, segs, 40)
    p.bass(BS, segs, "half", 70)
    p.mel(CL, "D3:4 | Bb2:4 | F3:4 | C3:4 | D3:4 | Bb2:4 | C3:4 | A2:4 | D3:4 | Bb2:4 | F3:4 | C3:4 | D3:4 | Bb2:4 | C3:4 | D3:4", 0, 50, legato=0.99)
    for k in range(0, 16, 4):   # a glockenspiel sparkle
        p.mel(GL, "D6:.5 A5:.5 F5:1", k * 4 + 2, 46)
    p.drums([("peaceful", 16)], 0.9)
    return p, {FL: "Flute", OB: "Oboe", HP: "Harp", GT: "Nylon guitar", PD: "Strings", BS: "Bass", GL: "Glockenspiel", CL: "Cello", 9: "Light percussion"}


def combat():
    p = Piece("Dragons of Flame - battle", 150, 24, seed=13)
    TB, TP, TR, CEL, TIM, TUB, HITC, BR = 0, 1, 2, 3, 4, 5, 6, 7
    for ch, prog, vol, pan in ((TB, TROMBONE, 104, 56), (TP, TRUMPET, 98, 76), (TR, TREM, 92, 64), (CEL, CELLO, 100, 44), (TIM, TIMPANI, 104, 64), (TUB, TUBA, 98, 60), (HITC, HIT, 92, 64), (BR, BRASS, 92, 70)):
        p.setup(ch, prog, vol, pan, 60)
    p.setup(9, 0, 104, 64, 35)
    S1 = ["Dm", "Dm", "Bb", "C", "Dm", "Dm", "Gm", "A"]
    S2 = ["Dm", "Bb", "C", "Dm", "Gm", "Bb", "A", "A"]
    segs = p.segments(S1 + S2 + S1)
    m1 = "D5:.5 D5:.5 F5:1 D5:.5 D5:.5 A5:1 | G5:.5 F5:.5 E5:1 D5:2 | Bb4:.5 Bb4:.5 D5:1 Bb4:.5 Bb4:.5 F5:1 | E5:.5 D5:.5 C5:1 E5:2 | D5:.5 D5:.5 F5:1 D5:.5 D5:.5 A5:1 | A5:.5 G5:.5 F5:1 E5:.5 D5:.5 C5:1 | Bb4:.5 D5:.5 G5:1 F5:1 D5:1 | C#5:1 E5:1 A5:2"
    m2 = "A5:1 F5:.5 D5:.5 A5:1 F5:1 | F5:1 D5:.5 Bb4:.5 F5:1 D5:1 | G5:1 E5:.5 C5:.5 G5:1 E5:1 | F5:.5 A5:.5 D6:1 C6:1 A5:1 | Bb5:1 G5:.5 D5:.5 Bb5:1 G5:1 | D5:1 F5:.5 Bb5:.5 A5:1 F5:1 | C#5:.5 E5:.5 A5:1 G5:1 E5:1 | A5:3 r:1"
    p.mel(TB, m1, 0, 100); p.mel(TP, m1, 0, 80, trans=12)
    p.mel(TP, m2, 32, 98); p.mel(TB, m2, 32, 84, trans=-12)
    p.mel(TB, m1, 64, 104); p.mel(TP, m1, 64, 88, trans=12)
    p.bass(CEL, segs, "eighth", 86, low=38)
    p.bass(TUB, segs, "half", 90, low=26)
    p.pad(TR, segs, 62, center=60)
    p.offbeats(BR, segs[::1], 64, center=58, length=0.25, beats=(1.5, 3.5)) if False else None
    for c, s, d in segs:   # the timpani drives every beat
        for b in range(int(d)):
            p.note(TIM, root_note(c, 38) - 12 if root_note(c, 38) > 47 else root_note(c, 38), s + b, 0.6, 96 if b % 2 == 0 else 74)
    for s in (0, 32, 64):
        p.note(HITC, 50, s, 1.5, 110); p.note(HITC, 57, s, 1.5, 100)
    p.drums([("combat", 7, "crash"), ("combat", 1, "fill"), ("combat", 7, "crash"), ("combat", 1, "fill"), ("combat", 7, "crash"), ("combat", 1, "fill")], 1.0)
    return p, {TB: "Trombone", TP: "Trumpet", TR: "Tremolo strings", CEL: "Cello", TIM: "Timpani", TUB: "Tuba", HITC: "Orchestra hit", BR: "Brass", 9: "Drums"}


def dungeon():
    p = Piece("Dragons of Flame - beneath the mountain", 60, 16, seed=14)
    CE, CH, PD, CL, BV, TM, PZ, HP = 0, 1, 2, 3, 4, 5, 6, 7
    for ch, prog, vol, pan, rev in ((CE, CELESTA, 92, 70, 100), (CH, CHOIR, 70, 64, 100), (PD, PAD, 66, 64, 90), (CL, CELLO, 82, 46, 80), (BV, BASSVIOLIN, 84, 64, 70),
                                    (TM, TIMPANI, 88, 64, 110), (PZ, PIZZ, 72, 84, 90), (HP, HARP, 70, 40, 100)):
        p.setup(ch, prog, vol, pan, rev)
    p.setup(9, 0, 80, 64, 90)
    segs = p.segments(["Am", "Am", "Bb", "Bb", "Am", "Am", "Dm", "E", "Am", "Am", "F", "F", "Dm", "Bb", "E", "E"])
    a = "E5:2 r:1 A5:1 | G5:1.5 E5:.5 D5:2 | F5:2 r:1 Bb5:1 | A5:1.5 F5:.5 E5:2 | C5:1 E5:1 A5:2 | B5:1 A5:1 G5:2 | F5:2 D5:1 F5:1 | E5:1 G#5:1 B5:2"
    b = "A4:1 C5:1 E5:2 | D5:2 C5:2 | A4:1 C5:1 F5:2 | E5:2 C5:2 | D5:1 F5:1 A5:2 | F5:2 D5:2 | G#4:1 B4:1 E5:2 | E5:4"
    p.mel(CE, a, 0, 74); p.mel(CE, b, 32, 70); p.mel(HP, b, 32, 52, trans=12)
    p.pad(CH, segs, 40, center=60); p.pad(PD, segs, 38, center=57)
    p.bass(BV, segs, "drone", 66, low=33)
    p.bass(CL, segs, "half", 56, low=40)
    for c, s, d in segs:   # a distant tick, like water dripping in the dark
        if 16 <= s < 32 or s >= 48:
            p.note(PZ, root_note(c, 57) + 12, s + 1, 0.4, 56); p.note(PZ, fifth_note(c, 57) + 12, s + 3, 0.4, 48)
    p.timp(TM, segs, (0,), 70)
    p.drums([("sparse", 1), ("sparse", 1)] * 8, 0.8) if False else None
    for bar in range(0, 16, 2):
        p.drums_bar(bar, "sparse", 0.9)
    return p, {CE: "Celesta", CH: "Choir", PD: "Warm pad", CL: "Cello", BV: "Contrabass", TM: "Timpani", PZ: "Pizzicato", HP: "Harp", 9: "Distant drums"}


def dcombat():
    p = Piece("Dragons of Flame - shadows awake", 136, 20, seed=15)
    TB, HN, TR, CEL, BV, ORG, TIM, HITC = 0, 1, 2, 3, 4, 5, 6, 7
    for ch, prog, vol, pan in ((TB, TROMBONE, 104, 52), (HN, HORN, 94, 72), (TR, TREM, 96, 64), (CEL, CELLO, 102, 42), (BV, BASSVIOLIN, 100, 64), (ORG, ORGAN, 66, 64), (TIM, TIMPANI, 106, 64), (HITC, HIT, 96, 64)):
        p.setup(ch, prog, vol, pan, 90)
    p.setup(9, 0, 108, 64, 50)
    chords = ["Am", "Am", "Bb", "Am", "Am", "Am", "Dm", "E", "Am", "Bb", "Am", "E", "Am", "Am", "F", "E", "Am", "Bb", "E", "E"]
    segs = p.segments(chords)
    ph = [
        "A4:.5 A4:.5 Bb4:.5 A4:.5 G#4:1 A4:1 | C5:.5 C5:.5 B4:.5 C5:.5 E5:2 | D5:.5 D5:.5 F5:.5 D5:.5 Bb4:2 | C5:1 B4:1 A4:2",
        "E5:.5 E5:.5 F5:.5 E5:.5 D#5:1 E5:1 | A5:1 G5:1 E5:2 | F5:.5 F5:.5 A5:.5 F5:.5 D5:2 | G#4:1 B4:1 E5:2",
        "A4:.5 A4:.5 Bb4:.5 A4:.5 G#4:1 A4:1 | Bb4:.5 Bb4:.5 C5:.5 Bb4:.5 D5:2 | C5:.5 C5:.5 E5:.5 C5:.5 A4:2 | G#4:1 B4:1 E5:2",
        "E5:1 A5:1 G5:1 E5:1 | C5:1 E5:1 A5:2 | F5:1 A5:1 C6:2 | B5:1 G#5:1 E5:2",
        "A4:.5 A4:.5 Bb4:.5 A4:.5 G#4:1 A4:1 | Bb4:1 D5:1 F5:2 | E5:.5 G#4:.5 B4:.5 E5:.5 G#5:2 | E5:3 r:1"]
    t = 0
    for k, text in enumerate(ph):
        p.mel(TB, text, t, 104); p.mel(HN, text, t, 80, trans=12 if k % 2 else 0)
        t += 16
    p.pad(TR, segs, 66, center=58); p.pad(ORG, segs, 40, center=52)
    p.bass(CEL, segs, "gallop", 88, low=33); p.bass(BV, segs, "half", 96, low=28)
    for c, s, d in segs:
        for b in (0, 2):
            p.note(TIM, root_note(c, 33) if root_note(c, 33) <= 45 else root_note(c, 33) - 12, s + b, 0.8, 100 if b == 0 else 80)
    for s in range(0, 80, 16):
        p.note(HITC, 45, s, 1.2, 108); p.note(HITC, 52, s, 1.2, 100)
    p.drums([("dcombat", 3, "crash"), ("dcombat", 1, "fill")] * 5, 1.0)
    return p, {TB: "Trombone", HN: "Horn", TR: "Tremolo strings", CEL: "Cello", BV: "Contrabass", ORG: "Organ", TIM: "Timpani", HITC: "Orchestra hit", 9: "Drums"}


def ending():
    p = Piece("Dragons of Flame - dawn", 72, 16, seed=16)
    HN, OB, VL, FL, HP, PD, OO, BV, TM, CE = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 - 0
    # (channel 9 is drums, so the celesta is moved to channel 10)
    CE = 10
    for ch, prog, vol, pan in ((HN, HORN, 98, 54), (OB, OBOE, 90, 72), (VL, 49, 88, 60), (FL, FLUTE, 80, 90), (HP, HARP, 94, 38), (PD, STRINGS, 78, 64), (OO, OOHS, 72, 64),
                               (BV, BASSVIOLIN, 86, 64), (TM, TIMPANI, 90, 64), (CE, CELESTA, 70, 70)):
        p.setup(ch, prog, vol, pan, 90)
    p.setup(9, 0, 90, 64, 70)
    segs = p.segments(["D", "A", "Bm", "F#m", "G", "D", "Em", "A", "D", "A", "Bm", "F#m", "G", "A", "D", "A"])
    m1 = "F#5:2 A5:1 B5:1 | A5:2 E5:2 | D5:1.5 F#5:.5 B5:2 | A5:2 C#5:2 | B4:1 D5:1 G5:2 | F#5:2 D5:2 | E5:1 G5:1 B5:2 | A5:3 r:1"
    m2 = "F#5:1 A5:1 D6:2 | C#6:2 A5:2 | B5:1.5 A5:.5 F#5:2 | A5:1 C#6:1 E6:2 | D6:2 B5:2 | A5:1 F#5:1 D5:2 | G5:1 B5:1 D6:1 C#6:1 | D6:4"
    p.mel(HN, m1, 0, 84); p.mel(OB, m1, 0, 52, trans=0)
    p.mel(VL, m2, 32, 88); p.mel(HN, m2, 32, 84, trans=-12); p.mel(FL, m2, 32, 70, trans=12)
    p.pad(PD, segs[:8], 52); p.pad(PD, segs[8:], 66)
    p.pad(OO, segs[8:], 50, center=64)
    p.arp(HP, segs[:8], "up8", 62, center=58); p.arp(HP, segs[8:], "up8", 66, center=60)
    p.bass(BV, segs, "half", 80, low=33)
    for c, s, d in segs[:8]:
        if int(s) % 8 == 0:
            p.note(TM, root_note(c, 33) + (12 if root_note(c, 33) < 38 else 0), s, 3, 70)
    for k in range(12): p.note(TM, 45, 28 + k * 0.33, 0.33, 40 + k * 5)   # a roll into the second half
    p.note(9, CRASH, 32, 3, 80); p.note(9, CRASH, 0, 3, 70)
    for b in range(8, 16): p.note(9, KICK, b * 4, 0.5, 70)
    p.mel(CE, "D6:.5 A5:.5 F#5:1 r:2 | r:4 | A5:.5 E5:.5 C#5:1 r:2 | r:4 | B5:.5 G5:.5 D5:1 r:2 | r:4 | G5:.5 B5:.5 E5:1 r:2 | r:4", 0, 56)
    return p, {HN: "Horn", OB: "Oboe", VL: "Violins", FL: "Flute", HP: "Harp", PD: "Strings", OO: "Voices", BV: "Contrabass", TM: "Timpani", CE: "Celesta", 9: "Percussion"}


def elven_hymn():
    """The Elven Hymn, from the sheet music in reference/ElvenHymn.png (a harp arrangement; no key signature, E minor / C major, 4/4).
    The scan is small: bars 1-8 are read note by note, the rest follows the tune's shape and the lyric phrases (to be checked by ear against the score)."""
    p = Piece("Elven Hymn", 132, 40, seed=21)
    MEL, ARP, PAD_C, LOW = 0, 1, 2, 3
    for ch, prog, vol, pan, rev in ((MEL, HARP, 104, 70, 80), (ARP, HARP, 78, 50, 80), (PAD_C, STRINGS, 52, 64, 85), (LOW, CELLO, 62, 60, 80)):
        p.setup(ch, prog, vol, pan, rev)
    bars = [
        # verse 1: "The sun, the splendid eye of all our heavens, dives from the day and leaves the dozing sky dappled with fireflies, deepening in gray"
        "Em", "Em", "C", "G", "Em", "Em", "C,G", "Em,Am",
        # "Now sleep, our oldest friend, lies on the deepwood trees and calls us in. The leaves are firey cold; they blaze into ash at end of year."
        "Am", "Am", "Em", "C", "Am", "G", "C", "Em",
        "C", "G", "Am", "Em", "C", "G", "Am,G", "Em",
        # "The day grows dark with sunset's pyre; so we await the sun's green fire upon the trees. The wind blows through the days..."
        "Am", "Em", "C", "G", "Am", "Em", "C", "G,Em",
        "Am", "G", "C", "Em", "Am", "Em", "G,C", "Em",
    ]
    segs = p.segments(bars)
    tune = (
        # 1-8, read from the score
        "G4:1 B4:2 E5:1 | D5:1.5 E5:.5 D5:1 B4:1 | C5:1 D5:1 E5:2 | D5:1 r:1 D5:1 B4:.5 G4:.5 | E5:4 | E5:1 G4:1 C5:1 E5:1 | D5:1.5 E5:.5 D5:1 r:1 | B4:.5 C5:.5 D5:1 E5:1 D5:1 | "
        # 9-16
        "C5:4 | C5:1 r:2 C5:1 | E5:3 D5:1 | C5:1.5 A4:.5 B4:1 D5:1 | E5:1.5 E5:.5 D5:1 C5:1 | D5:2 r:1 E5:1 | G5:4 | E5:4 | "
        # 17-24
        "E5:1 G4:1 C5:1 E5:1 | D5:1.5 E5:.5 D5:1 B4:1 | C5:1 D5:1 E5:2 | D5:1 r:1 D5:1 B4:1 | E5:1.5 D5:.5 C5:1 E5:1 | D5:1.5 B4:.5 G4:2 | A4:1 C5:1 E5:1 D5:1 | E5:4 | "
        # 25-32
        "E5:2 D5:1 B4:1 | C5:1.5 D5:.5 E5:1 D5:1 | E5:1.5 G5:.5 E5:1 C5:1 | D5:2 B4:1 G4:1 | C5:1 D5:1 E5:1 G5:1 | E5:1.5 D5:.5 C5:1 A4:1 | G4:1 B4:1 D5:1 E5:1 | E5:4 | "
        # 33-40: "...kings rise... so now sleep"; ends on the opening chord so it loops
        "A4:1 C5:1 E5:2 | D5:1.5 E5:.5 D5:1 B4:1 | C5:1 E5:1 G5:2 | E5:1.5 D5:.5 B4:2 | C5:1 D5:1 E5:1 D5:1 | C5:1.5 B4:.5 A4:1 G4:1 | A4:2 B4:1 D5:1 | G4:1 B4:3"
    )
    p.mel(MEL, tune, 0, 92)
    p.arp(ARP, segs, "up8", 58, center=57, step=0.5)
    p.pad(PAD_C, segs, 40, center=60)
    p.bass(LOW, segs, "half", 70, low=40)
    return p, {MEL: "Harp melody", ARP: "Harp arpeggio", PAD_C: "Strings", LOW: "Cello"}


PIECES = {"overland": overland, "scroll": scroll, "combat": combat, "dungeon": dungeon, "dcombat": dcombat, "ending": ending, "elven": elven_hymn}
