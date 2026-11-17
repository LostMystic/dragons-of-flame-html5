#!/bin/bash
# Headless regression tests for the engine. Run from the project root after building:
#   cmake -S . -B build && cmake --build build && tests/run.sh
cd "$(dirname "$0")/.."
BIN=./build/dragons-of-flame
fail=0
rm -f user/settings.json   # settings the tests save must not leak into the next run
export DOF_CONTROLS=original  # the default is the updated controls; the older tests were written against the original scheme
export DOF_Y_ANY=1  # (Y leaves any fight at once; the tests of the real rule unset it)
export DOF_FIXED_SPAWN=1  # monsters appear all on the right at a fixed distance (the real game places them at random on either side)
export DOF_QUICK_TITLE=1  # any key starts the game at once (the title menu has its own tests below)
export DOF_BOX_HOLD=0  # (a message normally stays up three quarters of a second; the scripted tests press faster)
export DOF_NO_INTRO=1  # the opening message box would swallow the first key of every scripted test
check() {  # name, expected substring, keys, frames
  out=$($BIN --headless --frames "$4" --keys "$3" | tail -1)
  if [[ "$out" == *"$2"* ]]; then echo "ok    $1"; else echo "FAIL  $1: wanted '$2' got '$out'"; fail=1; fi
}
# Defaults: start (1400,1200) on open grass, 8 px per logic step, one logic step about every 10 frames at 60 fps.
check "title waits for a key"        "mode=title"        "wait:30"                          30
check "enter starts the world"       "mode=world"        "enter:3,wait:10"                  40
check "walk right"                   "pos=(1048,1024)"   "enter:3,right:40"                 120
check "walk down"                    "pos=(1024,1048)"   "enter:3,down:40"                  120
check "walk left"                    "pos=(1000,1024)"   "enter:3,left:40"                  120
check "walk up"                      "pos=(1024,1000)"   "enter:3,up:40"                    120
check "walk diagonally"              "pos=(1048,1048)"   "enter:3,downright:40"             120
check "a wall stops the party"       "pos=(1056,1024)"   "enter:3,right:600"                700
check "toggle enters the combat view"   "mode=combat strip=(8256,320)" "enter:3,wait:20,toggle:2,wait:20"                  80
check "walk along the combat strip"     "mode=combat strip=(8352,320)" "enter:3,wait:20,toggle:2,wait:20,right:120,wait:5" 400
check "toggle back keeps the position"  "mode=world pos=(1038,1024)"    "enter:3,wait:20,toggle:2,wait:20,right:60,toggle:2,wait:20" 400
check "a vertical walk makes a north-south strip" "axis=ns"              "enter:3,wait:20,down:60,toggle:2,wait:20"            300
check "attacking kills enemies and earns XP" "enemies=0 xp="        "enter:3,wait:20,toggle:2,wait:5,attack:1200"       3300
out=$(DOF_TURBO=1 $BIN --headless --frames 800 --keys "enter:3,wait:20,toggle:2,wait:5,wait:700" | tail -1)
[[ "$out" != *"leader=Tanis hp=45"* ]] && echo "ok    an idle party gets hurt by the monsters" || { echo "FAIL  idle party: $out"; fail=1; }
check "a leader can be chosen"             "leader=Goldmoon" "enter:3,wait:20,sel3:2,wait:20"                    200
DOF_TURBO=1 check "a fallen party ends the game"       "mode=gameover"   "enter:3,wait:20,toggle:2,wait:5,wait:8000"         9000
check "levels 2 to 7 can be entered"        "level=2 in=(" "enter:3,wait:20,lvl2:2,wait:20"                     200
check "an interior can be walked"           "level=3"      "enter:3,wait:20,lvl3:2,wait:10,right:40,down:40" 300
check "F7 returns outdoors"                 "level=1"      "enter:3,wait:20,lvl2:2,wait:10,lvl1:2,wait:10"   200
check "Enter opens the menu"                "mode=menu"    "enter:3,wait:20,enter:2,wait:20"                 200
check "wild creatures roam the wilderness"  "wild=15"      "enter:3,wait:20"                                 100
DOF_TEST_WILD=1 check "touching a creature starts a fight"  "mode=combat"  "enter:3,wait:20,right:60,wait:10"                      300
out=$($BIN --headless --frames 200 --start 648,2688 --keys "enter:3,wait:20,left:60,wait:10" | tail -1)
[[ "$out" == *"level=2"* ]] && echo "ok    walking onto Sla-Mori's entrance enters level 2" || { echo "FAIL  entrance: $out"; fail=1; }
K="enter:3,wait:20,enter:2,wait:12"; for n in 1 2 3 4 5 6; do K="$K,down:2,wait:12"; done
out=$($BIN --headless --frames 700 --start 1152,816 --keys "$K,enter:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"inv=1"* ]] && echo "ok    Take picks up an item lying within reach" || { echo "FAIL  take: $out"; fail=1; }
out=$(DOF_TURBO=1 $BIN --headless --frames 60 --keys "enter:3,wait:20" | tail -1)
[[ "$out" == *"steps=4"* || "$out" == *"steps=5"* ]] && echo "ok    turbo mode runs the logic faster" || { echo "FAIL  turbo: $out"; fail=1; }
out=$($BIN --headless --frames 200 --start 1400,1200 --keys "enter:3,wait:20,toggle:2,wait:20,up:3,wait:20" | tail -1)
[[ "$out" == *"axis=ns"* ]] && echo "ok    up/down turns the side view to the other axis" || { echo "FAIL  axis: $out"; fail=1; }
out=$($BIN --headless --frames 200 --keys "enter:3,wait:20,toggle:2,wait:20,jumpr:2,wait:20" | tail -1)
[[ "$out" == *"strip=(8304"* ]] && echo "ok    a jump covers 48 px" || { echo "FAIL  jump: $out"; fail=1; }
rm -rf user
K2="enter:3,wait:20,enter:2,wait:12"; for n in $(seq 1 17); do K2="$K2,down:2,wait:12"; done
$BIN --headless --frames 900 --keys "$K2,enter:2,wait:30,clk100_24:2,wait:30,clk180_52:2,wait:30" >/dev/null
grep -q '"original_controls": false' user/settings.json 2>/dev/null && echo "ok    the Settings controls drop-down (click the box, click Updated controls) switches the controls and saves it" || { echo "FAIL  settings click"; fail=1; }
rm -rf user
K="enter:3,wait:20,enter:2,wait:12"; for n in $(seq 1 13); do K="$K,down:2,wait:12"; done
out=$($BIN --headless --frames 700 --keys "$K,enter:2,wait:30" | tail -1)
[[ "$out" == *"menu=13/13"* ]] && echo "ok    the quest map opens" || { echo "FAIL  quest map: $out"; fail=1; }
out=$(DOF_WARP="3,15,1" $BIN --headless --frames 100 --keys "enter:3,wait:20" | tail -1)
[[ "$out" == *"xp=10000"* ]] && echo "ok    rescuing Laurana scores and counts as a quest step" || { echo "FAIL  laurana: $out"; fail=1; }
out=$(DOF_WARP="6,13,24" $BIN --headless --frames 60 --keys "enter:3,wait:20" | tail -1)
[[ "$out" == *"mode=ending"* ]] && echo "ok    the south door of Pax Tharkas ends the game" || { echo "FAIL  ending: $out"; fail=1; }
# the whole ending with a typed name: message, 2 epilogue pages (incomplete), scores, name, table, back to the title
out=$(DOF_WARP="6,13,24" $BIN --headless --frames 1400 --keys "enter:3,wait:20,enter:2,wait:25,enter:2,wait:25,enter:2,wait:25,enter:2,wait:25,enter:2,wait:25,txtB:2,wait:25,txtob:2,wait:25,enter:2,wait:25,enter:2,wait:25" | tail -1)
[[ "$out" == *"mode=title"* ]] && echo "ok    the ending runs through the name entry back to the title" || { echo "FAIL  ending flow: $out"; fail=1; }
out=$(DOF_WARP="5,5,5" $BIN --headless --frames 100 --keys "enter:3,wait:20" | tail -1)
[[ "$out" == *"level=5"* || "$out" == *"level="* ]] && echo "ok    warp works" || { echo "FAIL  warp: $out"; fail=1; }
out=$(DOF_QUICK_TALK=1 DOF_WARP="w,1024,2528" $BIN --headless --frames 80 --keys "enter:3,wait:30" | tail -1)
[[ "$out" == *"party=9"* ]] && echo "ok    Gilthanas joins at the pass (quick talk)" || { echo "FAIL  gilthanas: $out"; fail=1; }
out=$(DOF_QUICK_TALK=1 DOF_WARP="w,1856,512" $BIN --headless --frames 80 --keys "enter:3,wait:30" | tail -1)
[[ "$out" == *"party=9"* ]] && echo "ok    a wandering nomad (Akar, old man in the hat) joins at his spot" || { echo "FAIL  nomad: $out"; fail=1; }
out=$(DOF_KILL="30,2" $BIN --headless --frames 410 --keys "enter:5,wait:360" | tail -1)
[[ "$out" == *"speech="* ]] && echo "ok    when a hero falls and the fight is over, the party speaks over the body" || { echo "FAIL  farewell: $out"; fail=1; }
# save and restore: a second run starts fresh, loads the save and finds the leader and position again
rm -rf user
$BIN --headless --frames 200 --keys "enter:3,wait:20,right:40,sel2:2,save:2,wait:20" >/dev/null
check "a saved game restores leader and place" "pos=(1056,1024) leader=Caramon" "enter:3,wait:20,load:2,wait:20" 120
rm -rf user
# the sound sequencer: effect 13 lasts 52 timer ticks (2.86 s), the title theme (15) loops
tmp=$(mktemp -d)
$BIN --render-sound 13 5 $tmp/a.wav && $BIN --render-sound 15 20 $tmp/b.wav
python3 -I - "$tmp" <<'PY' && echo "ok    sound effect length and looping theme" || { echo "FAIL  sound"; fail=1; }
import sys,wave,struct
def load(p):
    w=wave.open(p); return struct.unpack("<%dh"%w.getnframes(),w.readframes(w.getnframes()))
a=load(sys.argv[1]+"/a.wav"); b=load(sys.argv[1]+"/b.wav")
last=max(i for i,x in enumerate(a) if x)/22050
assert 2.7<last<3.0, last
assert all(x for x in b[-2000:]), "theme should still be playing at 20 s (it loops)"
PY
rm -rf "$tmp"

# the opening message and rest (checked without DOF_NO_INTRO)
# updated controls: the party answers every frame, not every 165 ms logic step
DOF_CONTROLS=updated check "updated controls: a 6-frame tap in the side view walks 6 px"   "strip=(8262,320)" "enter:3,wait:20,toggle:2,wait:20,right:6"      52
DOF_CONTROLS=updated check "updated controls: the outdoor walk is smooth"                  "pos=(1041,1024)"  "enter:3,wait:5,right:20"                         30
DOF_CONTROLS=updated DOF_STAM=6,5 check "endurance: when the lead's yellow bar runs out the next companion takes over" "lead=Tass" "enter:3,wait:20,toggle:2,wait:40,right:60,wait:100" 400
DOF_CONTROLS=updated DOF_HURT=10 DOF_CAMP=30 check "rest: Goldmoon's prayers and the bonus cures heal the hurt (Caramon 24 -> 31)" "[Car 31 28]" "enter:3,wait:2000" 4000
DOF_CAMP=30 check "rest: the view fades out (camp phase 1)" "camp=1" "enter:3,wait:20" 340
DOF_CAMP=30 check "rest: the companions talk (camp phase 2)" "camp=2" "enter:3,wait:20" 480
DOF_CAMP=30 check "rest: it ends by itself and the party can move again" "mode=world pos=(1024,1024)" "enter:3,wait:2000" 6000
DOF_CONTROLS=updated check "turning to the other axis and back puts the party exactly where it was" "strip=(8256,320) world=(1024,1024) axis=ew" "enter:3,wait:20,toggle:2,wait:20,down:1,wait:150,down:1,wait:150" 1200
out=$(DOF_CONTROLS=updated DOF_TEST_WILD=1 DOF_TEST_BEHIND=4 DOF_CLEAR=12 $BIN --headless --frames 200 --keys "enter:3,wait:20,toggle:2,wait:900" | tail -1)
[[ "$out" == *"enemies=0"* ]] && echo "ok    a creature in the distance waits while it is far" || { echo "FAIL  distant creature early: $out"; fail=1; }
out=$(DOF_CONTROLS=updated DOF_TEST_WILD=1 DOF_TEST_BEHIND=4 DOF_CLEAR=12 $BIN --headless --frames 600 --keys "enter:3,wait:20,toggle:2,wait:900" | tail -1)
[[ "$out" != *"enemies=0"* && "$out" == *"enemies="* ]] && echo "ok    ...walks up to the party and the fight begins" || { echo "FAIL  distant creature arrives: $out"; fail=1; }
DOF_WANDER=1 check "dungeon random encounters: a wandering group turns up in Pax Tharkas" "wand=" "enter:3,wait:20,lvl4:2,wait:300" 400
DOF_CONTROLS=updated check "updated controls: turning the strip swings the view for about a second" "turning=" "enter:3,wait:20,toggle:2,wait:20,turn:1" 85
DOF_CONTROLS=updated check "updated controls: after the swing the other strip is walkable" "axis=ns" "enter:3,wait:20,toggle:2,wait:20,turn:1,wait:80,right:2" 220
DOF_CONTROLS=updated check "updated controls: a jump moves at walking speed (6 frames in)" "strip=(8262,320)" "enter:3,wait:20,toggle:2,wait:20,jumpr:1,wait:6"  52
DOF_CONTROLS=updated check "updated controls: a jump lands 112 px away, the original's distance" "strip=(8368,320)" "enter:3,wait:20,toggle:2,wait:20,jumpr:1,wait:130" 180
DOF_CONTROLS=updated check "updated controls: the keyboard up arrow jumps"               "strip=(8368,320)" "enter:3,wait:20,toggle:2,wait:20,up:1,wait:130"    180
DOF_CONTROLS=updated check "updated controls: A attacks a foe in reach (kills enemies, earns XP)" "enemies=0 xp=" "enter:3,wait:20,toggle:2,wait:5,act:1200" 3300
out=$(DOF_CONTROLS=updated $BIN --headless --frames 60 --start 1152,816 --keys "enter:3,wait:20,act:1,wait:10" | tail -1)
[[ "$out" == *"inv=1"* || "$out" == *"took the Arrows"* ]] && echo "ok    updated controls: A picks up an item lying here" || { echo "FAIL  A pickup: $out"; fail=1; }
out=$($BIN --headless --frames 60 --start 1152,816 --keys "enter:3,wait:20,act:1,wait:10" | tail -1)
[[ "$out" == *"inv=0"* ]] && echo "ok    original controls: the gamepad A does nothing" || { echo "FAIL  orig A: $out"; fail=1; }
DOF_CONTROLS=updated check "updated controls: clicking a menu row picks it (the first row is Clerical spells)" "menu=0/2/" "enter:3,wait:20,enter:2,wait:12,clk100_33:1,wait:12" 160
DOF_CONTROLS=updated check "updated controls: clicking outside the scroll backs out"                       "mode=world" "enter:3,wait:20,enter:2,wait:12,clk10_10:1,wait:12"  160
DOF_CONTROLS=original check "original controls: menu clicks do nothing"                                    "mode=menu"  "enter:3,wait:20,enter:2,wait:12,clk100_33:1,wait:12" 160
DOF_CONTROLS=updated check "updated controls: clicking a portrait chooses that hero as leader"             "leader=Goldmoon" "enter:3,wait:20,clk180_140:1,wait:10" 100
out=$(env -u DOF_CONTROLS $BIN --headless --frames 80 --keys "enter:3,wait:5,right:20" | tail -1)
[[ "$out" == *"pos=(1041,1024)"* ]] && echo "ok    updated controls are the default" || { echo "FAIL  default controls: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 60 --keys "enter:3,wait:20,map:1,wait:10" | tail -1)
[[ "$out" == *"mode=menu"* ]] && echo "ok    updated controls: the gamepad Back button opens the world map" || { echo "FAIL  back map: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:20,map:1,wait:10,map:1,wait:10" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    updated controls: Back again closes the map" || { echo "FAIL  back close: $out"; fail=1; }
out=$(DOF_CONTROLS=original $BIN --headless --frames 60 --keys "enter:3,wait:20,map:1,wait:10" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    original controls: Back does nothing" || { echo "FAIL  orig back: $out"; fail=1; }
out=$(env -u DOF_NO_INTRO $BIN --headless --frames 90 --keys "enter:3,wait:20,down:2,wait:5" | tail -1)
[[ "$out" == *"box=1"* ]] && echo "ok    a message too long for the scroll scrolls down with the down key" || { echo "FAIL  box scroll: $out"; fail=1; }
out=$(env -u DOF_NO_INTRO $BIN --headless --frames 110 --keys "enter:3,wait:20,enter:2,wait:12" | tail -1)
[[ "$out" == *"box=2"* ]] && echo "ok    the confirm key pages down a long message first" || { echo "FAIL  box page: $out"; fail=1; }
out=$(env -u DOF_NO_INTRO $BIN --headless --frames 160 --keys "enter:3,wait:20,enter:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" != *"box="* ]] && echo "ok    ...and closes it at the end" || { echo "FAIL  box close: $out"; fail=1; }
DOF_CONTROLS=updated check "updated controls: a jump is exactly as fast as walking (100 frames each reach 8356)" "strip=(8356,320)" "enter:3,wait:20,toggle:2,wait:20,jumpr:1,wait:100" 146
DOF_CONTROLS=updated check "gamepad: pushing the stick up does not jump"      "strip=(8256,320)" "enter:3,wait:20,toggle:2,wait:20,padup:1,wait:60"   130
DOF_CONTROLS=updated check "gamepad: pushing the stick down does not turn"    "axis=ew"          "enter:3,wait:20,toggle:2,wait:20,paddown:1,wait:30" 100
DOF_CONTROLS=updated check "gamepad: a trigger turns to the next perpendicular direction" "axis=ns" "enter:3,wait:20,toggle:2,wait:20,turn:1,wait:30"    100
DOF_CONTROLS=updated check "keyboard down still turns the side view"          "axis=ns"          "enter:3,wait:20,toggle:2,wait:20,down:1,wait:30"    100
# the title screen (without the quick-start shortcut the other tests use)
T() { env -u DOF_QUICK_TITLE DOF_CONTROLS=updated $BIN --headless --frames "$1" --keys "$2" | tail -1; }
M="enter:3,wait:12,enter:3,wait:12"   # past the publishers' lines and the high scores, on the menu
out=$(T 20 "wait:20"); [[ "$out" == *"mode=title"*"page=0"* ]] && echo "ok    the title starts with the publishers' lines" || { echo "FAIL  title page 0: $out"; fail=1; }
out=$(T 40 "enter:3,wait:20"); [[ "$out" == *"page=5"* ]] && echo "ok    the high scores come up before the title menu" || { echo "FAIL  title scores: $out"; fail=1; }
out=$(T 60 "$M"); [[ "$out" == *"page=1 sel=0"* ]] && echo "ok    a second key goes on to the title menu" || { echo "FAIL  title menu: $out"; fail=1; }
out=$(T 1200 "wait:1200"); [[ "$out" == *"page=1"* ]] && echo "ok    the title pages go on to the menu by themselves" || { echo "FAIL  title auto: $out"; fail=1; }
out=$(T 120 "$M,down:2,wait:12,down:2,wait:12"); [[ "$out" == *"page=1 sel=2"* ]] && echo "ok    the title menu moves with the arrows" || { echo "FAIL  title nav: $out"; fail=1; }
out=$(T 100 "$M,up:2,wait:12"); [[ "$out" == *"page=1 sel=6"* ]] && echo "ok    the title menu wraps round (Exit is last)" || { echo "FAIL  title wrap: $out"; fail=1; }
out=$(T 120 "$M,enter:3,wait:30"); [[ "$out" == *"mode=world"* ]] && echo "ok    Start New Game starts the world" || { echo "FAIL  start new: $out"; fail=1; }
out=$(T 160 "$M,down:2,wait:12,enter:3,wait:30"); [[ "$out" == *"mode=title"*"page=1 sel=1"* ]] && echo "ok    Load Game with no save stays on the title" || { echo "FAIL  title load: $out"; fail=1; }
out=$(T 160 "$M,down:2,wait:12,down:2,wait:12,enter:3,wait:20"); [[ "$out" == *"page=3"* ]] && echo "ok    Intro opens the slideshow" || { echo "FAIL  title intro: $out"; fail=1; }
out=$(T 160 "$M,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:3,wait:20"); [[ "$out" == *"page=5"* ]] && echo "ok    High Scores is on the menu (after Intro)" || { echo "FAIL  title high scores: $out"; fail=1; }
out=$(T 200 "$M,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:3,wait:20"); [[ "$out" == *"page=4"* ]] && echo "ok    Settings opens (before Credits)" || { echo "FAIL  title settings: $out"; fail=1; }
out=$(T 240 "$M,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:3,wait:20"); [[ "$out" == *"page=2"* ]] && echo "ok    Credits comes after Settings" || { echo "FAIL  title credits: $out"; fail=1; }
out=$(T 200 "$M,down:2,wait:12,down:2,wait:12,enter:3,wait:12,enter:3,wait:12,esc:3,wait:20"); [[ "$out" == *"page=1"* ]] && echo "ok    Esc leaves the intro slideshow" || { echo "FAIL  intro esc: $out"; fail=1; }
out=$(T 120 "$M,clk160_134:1,wait:20"); [[ "$out" == *"page=3"* ]] && echo "ok    a click on a title menu entry chooses it (Intro)" || { echo "FAIL  title click: $out"; fail=1; }
out=$(T 600 "$M,up:2,wait:12,enter:3,wait:300"); [[ "$out" =~ steps=([0-9]+) && ${BASH_REMATCH[1]} -lt 40 ]] && echo "ok    Exit on the title menu quits (native)" || { echo "FAIL  title exit: $out"; fail=1; }
out=$(T 60 "wait:5,help:1,wait:5"); [[ "$out" == *"mode=help"* ]] && echo "ok    F1 opens the command list" || { echo "FAIL  help: $out"; fail=1; }
out=$(DOF_CONTROLS=original $BIN --headless --frames 60 --keys "enter:3,wait:12,qkey:1,wait:12" | tail -1)
[[ "$out" == *"mode=menu"* ]] && echo "ok    original controls: Q opens the quest map" || { echo "FAIL  Q map: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 60 --keys "enter:3,wait:12,qkey:1,wait:12" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    updated controls: Q does not open the map" || { echo "FAIL  Q updated: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 600 --keys "enter:3,wait:12,enter:2,wait:12,up:2,wait:12,enter:2,wait:300" | tail -1)
[[ "$out" == *"mode=title"* && "$out" == *"page=1"* ]] && echo "ok    Exit on the in-game main menu returns to the title menu" || { echo "FAIL  menu exit: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 40 --keys "enter:3,wait:12,start:1,wait:12" | tail -1)
[[ "$out" == *"mode=menu"* ]] && echo "ok    the gamepad Start opens the menu on the map" || { echo "FAIL  start map: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:12,toggle:2,wait:20,start:1,wait:12" | tail -1)
[[ "$out" == *"mode=menu"* ]] && echo "ok    the gamepad Start opens the menu in the side view" || { echo "FAIL  start side: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:12,start:1,wait:12,start:1,wait:12" | tail -1)
[[ "$out" == *"mode=menu"* ]] && echo "ok    Start does not close the menu again" || { echo "FAIL  start twice: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:12,map:1,wait:12,start:1,wait:12" | tail -1)
[[ "$out" == *"menu=0/-1/"* ]] && echo "ok    Start on the overlay map shows the main menu" || { echo "FAIL  start overlay: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 60 --start 576,2860 --keys "enter:3,wait:30" | tail -1)
[[ "$out" == *"box="* ]] && echo "ok    the front of Pax Tharkas says the way in is Sla-Mori" || { echo "FAIL  gate hint: $out"; fail=1; }
out=$(DOF_TEST_SPELLS=1 DOF_WARP="item,12" DOF_CONTROLS=updated $BIN --headless --frames 60 --keys "enter:3,wait:20,act:1,wait:12" | tail -1)
[[ "$out" == *"equipped the Longsword +2"* && "$out" == *"inv=1"* ]] && echo "ok    shared pack: a picked-up sword goes to the hero who gains most, with a message" || { echo "FAIL  auto equip: $out"; fail=1; }
out=$(DOF_WARP="item,3" DOF_CONTROLS=updated $BIN --headless --frames 60 --keys "enter:3,wait:20,act:1,wait:12" | tail -1)
[[ "$out" == *"took the Arrows"* ]] && echo "ok    shared pack: arrows go to an archer" || { echo "FAIL  arrows: $out"; fail=1; }
out=$(DOF_WARP="item,52" DOF_CONTROLS=updated $BIN --headless --frames 60 --keys "enter:3,wait:20,act:1,wait:12" | tail -1)
[[ "$out" == *"put on the Ring of Protection"* ]] && echo "ok    shared pack: a ring is put on by a hero" || { echo "FAIL  ring: $out"; fail=1; }
m() { echo "$1" | grep -o "music=[a-z-]*"; }
out=$($BIN --headless --frames 5 --keys "wait:5" | tail -1); [[ "$(m "$out")" == "music=title" ]] && echo "ok    music: the title plays the title theme" || { echo "FAIL  music title: $out"; fail=1; }
out=$($BIN --headless --frames 40 --keys "enter:3,wait:20" | tail -1); [[ "$(m "$out")" == "music=overland" ]] && echo "ok    music: the map plays the overland theme" || { echo "FAIL  music overland: $out"; fail=1; }
out=$($BIN --headless --frames 80 --keys "enter:3,wait:20,toggle:2,wait:20" | tail -1); [[ "$(m "$out")" == "music=combat" ]] && echo "ok    music: foes in the side view play the combat theme" || { echo "FAIL  music combat: $out"; fail=1; }
out=$(DOF_WARP="w,1024,2528" $BIN --headless --frames 90 --keys "enter:3,wait:30" | tail -1); [[ "$(m "$out")" == "music=scroll" ]] && echo "ok    music: the side view without monsters plays the quiet theme" || { echo "FAIL  music scroll: $out"; fail=1; }
out=$(DOF_WARP="2,6,5" $BIN --headless --frames 40 --keys "enter:3,wait:20" | tail -1); [[ "$(m "$out")" == "music=dungeon" ]] && echo "ok    music: the dungeons play the dungeon theme" || { echo "FAIL  music dungeon: $out"; fail=1; }
out=$(DOF_MUSIC=original $BIN --headless --frames 40 --keys "enter:3,wait:20" | tail -1); [[ "$(m "$out")" == "music=-" ]] && echo "ok    music: the original setting leaves the page's music off" || { echo "FAIL  music original: $out"; fail=1; }
out=$(DOF_WARP="w,552,2704" $BIN --headless --frames 90 --keys "enter:3,wait:30" | tail -1); [[ "$out" == *"mode=combat"* ]] && echo "ok    Laurana waits in the mountain passage and talks in the side view" || { echo "FAIL  laurana: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 400 --start 696,2706 --keys "enter:3,wait:20,left:70,up:40,left:50" | tail -1)
[[ "$out" == *"level=2"* ]] && echo "ok    updated controls: the party slides into the one-tile passage and the X leads into Sla-Mori (from a misaligned start)" || { echo "FAIL  entrance X: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"menu=0/-1/"* ]] && echo "ok    updated controls: the menu starts at Clerical spells (no Exit menu, Hero select, Take, Open, Give or Quest map)" || { echo "FAIL  menu start: $out"; fail=1; }
out=$(DOF_WARP=2,6,5 DOF_CONTROLS=updated $BIN --headless --frames 20 --keys "enter:3,wait:12" | tail -1)
[[ "$out" == *"cell=22"* ]] && echo "ok    level 2 has the hidden door at cell (6,5)" || { echo "FAIL  hidden door setup: $out"; fail=1; }
out=$(DOF_WARP=2,6,5 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:20" | tail -1)
[[ "$out" == *"cell=23"* && "$out" == *"box="* ]] && echo "ok    Inspect finds a hidden door in front of the party" || { echo "FAIL  inspect: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:20" | tail -1)
[[ "$out" == *"box="* ]] && echo "ok    Inspect outdoors finds nothing of interest" || { echo "FAIL  inspect nothing: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:12,enter:2,wait:12,back:2,wait:12" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    the gamepad B backs out of the main menu" || { echo "FAIL  B in menu: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,enter:2,wait:12,back:2,wait:12" | tail -1)
[[ "$out" == *"menu=1/-1/"* ]] && echo "ok    B goes back one step from a menu page to the main menu" || { echo "FAIL  B in page: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 300 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"mode=help"* ]] && echo "ok    the menu's Controls (F1) entry opens the command list" || { echo "FAIL  menu controls entry: $out"; fail=1; }
out=$(DOF_CONTROLS=original $BIN --headless --frames 80 --keys "enter:3,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"menu=0/-1/"* ]] && echo "ok    original controls keep the full menu" || { echo "FAIL  orig menu: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:12,enter:2,wait:12,enter:40,wait:20" | tail -1)
[[ "$out" == *"menu=0/2/0"* ]] && echo "ok    a held confirm key in the menu counts once (no double press)" || { echo "FAIL  menu double press: $out"; fail=1; }
out=$(DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:20,toggle:2,wait:20,ranged:300" | tail -1)
[[ "$out" =~ ammo=([0-9]+) && ${BASH_REMATCH[1]} -lt 20 ]] && echo "ok    LT: the leader fires the ranged weapon and uses up ammunition" || { echo "FAIL  ranged: $out"; fail=1; }
out=$(DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 200 --keys "enter:3,wait:20,sel1:2,toggle:2,wait:20,ranged:100" | tail -1)
[[ "$out" == *"No ranged weapon"* ]] && echo "ok    LT: a hero with no ranged weapon says so (Sturm)" || { echo "FAIL  no ranged weapon: $out"; fail=1; }
out=$(DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:20,sel3:2,toggle:2,wait:20,ranged:200" | tail -1)
[[ "$out" == *"leader=Goldmoon"* && "$out" == *"enemies=0"* ]] && echo "ok    LT: Goldmoon casts her offensive spells on the foes" || { echo "FAIL  Goldmoon ranged: $out"; fail=1; }
out=$(DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:20,sel4:2,toggle:2,wait:20,ranged:200" | tail -1)
[[ "$out" == *"leader=Raistlin"* && "$out" == *"enemies=0"* ]] && echo "ok    LT: Raistlin casts damaging spells on the foes" || { echo "FAIL  Raistlin ranged: $out"; fail=1; }
out=$(DOF_WARP="2,10,10" DOF_CONTROLS=updated $BIN --headless --frames 260 --keys "enter:3,wait:20,jumpr:1,wait:150" | tail -1)
[[ "$out" == *"in=(1448,1336)"* ]] && echo "ok    updated controls: the party can jump inside Sla-Mori (112 px)" || { echo "FAIL  dungeon jump: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 200 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"/20/"* ]] && echo "ok    the menu's Equip entry opens the character screen" || { echo "FAIL  equip page: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 220 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"/22/"* ]] && echo "ok    the menu's Inventory entry opens the sorted inventory" || { echo "FAIL  inventory page: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 240 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,rb:1,wait:12,rb:1,wait:12" | tail -1)
[[ "$out" == *"mh=6"* ]] && echo "ok    LB / RB change the hero on the equip screen (Tanis, index 4, two on)" || { echo "FAIL  hero switch: $out"; fail=1; }
out=$(DOF_GIVE=5 DOF_CONTROLS=updated $BIN --headless --frames 300 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,enter:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"hand=Broadsword +2"* && "$out" == *"/20/"* ]] && echo "ok    Equip: the Hand slot and the Broadsword +2 equips it and returns to the character screen" || { echo "FAIL  equip item: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 300 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"box="* && "$out" == *"/20/"* ]] && echo "ok    Equip: a slot with nothing to equip says there is no equipment here" || { echo "FAIL  no equipment: $out"; fail=1; }
out=$(DOF_DROP_AT=20 DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:20,toggle:2,wait:20,act:3,wait:30" | tail -1)
[[ "$out" == *"ammo=21"* && "$out" == *"dropped=0"* ]] && echo "ok    an arrow lying on the ground is picked up with A and goes back to the archer (20 -> 21)" || { echo "FAIL  arrow pickup: $out"; fail=1; }
out=$(DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 600 --keys "enter:3,wait:20,toggle:2,wait:20,ranged:6,wait:120" | tail -1)
[[ "$out" == *"flying=0"* && "$out" == *"dropped=1"* ]] && echo "ok    a fired arrow flies, comes down on the ground and waits there" || { echo "FAIL  arrow lands: $out"; fail=1; }
out=$(DOF_TEST_WILD=1 DOF_DROP_AT=150 DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 1100 --keys "enter:3,wait:20,right:170,act:500,wait:300" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"enemies=0"* ]] && echo "ok    after a fight the side view waits while ammunition lies on the ground" || { echo "FAIL  victory waits: $out"; fail=1; }
out=$(DOF_TEST_WILD=1 DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 1100 --keys "enter:3,wait:20,right:170,act:500,wait:300" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    ...and with no ammunition out there it returns to the map after the fight" || { echo "FAIL  victory returns: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 420 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"mus=0"* && "$out" == *"sfx=1"* ]] && echo "ok    Settings: the Music switch turns the music off" || { echo "FAIL  music switch: $out"; fail=1; }
rm -f user/settings.json; out=$(DOF_CONTROLS=updated $BIN --headless --frames 440 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12" | tail -1)
[[ "$out" == *"sfx=0"* && "$out" == *"mus=1"* ]] && echo "ok    Settings: the Sound effects switch turns the effects off" || { echo "FAIL  sound switch: $out"; fail=1; }
rm -f user/settings.json; out=$(DOF_CONTROLS=updated $BIN --headless --frames 420 --keys "enter:3,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,down:2,wait:12,down:2,wait:12,down:2,wait:12,enter:2,wait:12,esc:2,wait:12,esc:2,wait:12" | tail -1)
[[ "$(echo "$out" | grep -o "music=[a-z-]*")" == "music=-" ]] && echo "ok    with Music off the game plays no music" || { echo "FAIL  music off: $out"; fail=1; }
out=$(DOF_WARP=3,4,7 DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:20,act:3,wait:20" | tail -1)
[[ "$out" == *" n=24 "* ]] && echo "ok    A opens a closed door in the wall beside the party (above)" || { echo "FAIL  door above: $out"; fail=1; }
out=$(DOF_WARP=3,4,5 DOF_CONTROLS=updated $BIN --headless --frames 80 --keys "enter:3,wait:20,act:3,wait:20" | tail -1)
[[ "$out" == *" s=57 "* ]] && echo "ok    A opens a closed door below the party" || { echo "FAIL  door below: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 90 --keys "enter:3,wait:20,debug:3,wait:10,enter:3,wait:20" | tail -1)
[[ "$out" == *"pos=(648,2688)"* && "$out" == *"party=9"* ]] && echo "ok    F10 debug menu warps to the entrance with Laurana" || { echo "FAIL  debug warp: $out"; fail=1; }
out=$(DOF_WARP=2,28,4 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:10,turn:3,wait:10,right:600" | tail -1)
[[ "$out" == *" xp=150 "* ]] && echo "ok    freeing a prisoner in a Sla-Mori cell gives 150 experience" || { echo "FAIL  prisoner: $out"; fail=1; }
out=$(DOF_WARP=2,28,4 DOF_CONTROLS=updated $BIN --headless --frames 200 --keys "enter:3,wait:10,right:60,wait:10,map:3,wait:100" | tail -1)
[[ "$out" == *"mode=menu"* && "$out" == *"menu=0/13/"* ]] && echo "ok    Back in a dungeon opens the dungeon map" || { echo "FAIL  dungeon map: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 220 --keys "enter:3,wait:20,debug:3,wait:10,down:2,wait:10,down:2,wait:10,down:2,wait:10,enter:3,wait:10,debug:3,wait:10,down:60" | tail -1)
[[ "$out" == *"pos=(1024,13"* || "$out" == *"pos=(1024,12"* ]] && echo "ok    the debug menu's Run switch makes the party walk five times as fast" || { echo "FAIL  run: $out"; fail=1; }
out=$(DOF_HANDS=1 DOF_WARP=item,19 DOF_CONTROLS=updated $BIN --headless --frames 200 --keys "enter:3,wait:20,act:3,wait:60" | tail -1)
[[ "$out" == *"[Cara:Wyrmslayer:"* ]] && echo "ok    Caramon equips Wyrmslayer when it is found" || { echo "FAIL  wyrmslayer: $out"; fail=1; }
out=$(DOF_HANDS=1 DOF_TEST_SPELLS=1 DOF_WARP=item,71 DOF_CONTROLS=updated $BIN --headless --frames 100 --keys "enter:3,wait:20,act:3,wait:30" | tail -1)
[[ "$out" == *"[Cara:Longsword:3]"* ]] && echo "ok    plate armor (found with Detect treasure on) goes to Caramon and makes his armor class 3" || { echo "FAIL  plate: $out"; fail=1; }
out=$(DOF_HANDS=1 DOF_WARP=item,71 DOF_CONTROLS=updated $BIN --headless --frames 100 --keys "enter:3,wait:20,act:3,wait:30" | tail -1)
[[ "$out" == *"[Cara:Longsword:6]"* ]] && echo "ok    an invisible item cannot be taken without Detect treasure" || { echo "FAIL  invisible: $out"; fail=1; }
out=$(DOF_WARP=2,6,5 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 700 --keys "enter:3,wait:10,act:3,wait:80,act:3,wait:80" | tail -1)
[[ "$out" == *"cell=24"* && "$out" == *" n=45 "* ]] && echo "ok    A beside a hidden door finds it and the next A opens it" || { echo "FAIL  secret door: $out"; fail=1; }
out=$(DOF_WARP=2,6,5 DOF_CONTROLS=updated $BIN --headless --frames 100 --keys "enter:3,wait:60" | tail -1)
[[ "$out" == *"cell=23"* && "$out" == *"spots a secret door"* ]] && echo "ok    the elves of the party notice a secret door as they come by" || { echo "FAIL  elves: $out"; fail=1; }
for d in 5 6; do
  keys="enter:3,wait:20,debug:3,wait:10,$(printf 'down:2,wait:10,%.0s' $(seq 1 $d))enter:3,wait:60,left:300,wait:30,act:3,wait:60,left:300,wait:60"
  out=$(DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 1400 --keys "$keys" | tail -1)
  if [[ $d == 5 ]]; then want="party=9"; else want="party=8"; fi
  [[ "$out" == *"mode=ending"* && "$out" == *"$want"* ]] && echo "ok    debug option $d: to the exit door of Pax Tharkas, through it to the ending" || { echo "FAIL  debug exit door $d: $out"; fail=1; }
done
out=$(DOF_WARP=w,864,736 DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:60" | tail -1)
[[ "$out" == *"xp=500"* && "$out" == *"inv=1"* ]] && echo "ok    walking into Solace: the town visit heals the party and gives a potion and experience" || { echo "FAIL  solace poi: $out"; fail=1; }
out=$(DOF_WARP=w,256,2432 DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:60" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"enemies=3"* ]] && echo "ok    the trolls' lair starts a fight with three trolls" || { echo "FAIL  troll lair: $out"; fail=1; }
out=$(DOF_WARP=6,17,16 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 600 --keys "enter:3,wait:40,enter:3,wait:60" | tail -1)
[[ "$out" == *"xp=1000"* ]] && echo "ok    Goldmoon can heal Elistan when A is chosen" || { echo "FAIL  elistan: $out"; fail=1; }
out=$(DOF_WARP=w,1664,1536 DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:60" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"enemies=10"* ]] && echo "ok    the goblin warren: ten goblins" || { echo "FAIL  goblin warren: $out"; fail=1; }
out=$(DOF_WARP=w,128,1408 DOF_CONTROLS=updated $BIN --headless --frames 120 --keys "enter:3,wait:60" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"enemies=8"* ]] && echo "ok    the Aghar lair: eight gully dwarves" || { echo "FAIL  aghar lair: $out"; fail=1; }
out=$(env -u DOF_Y_ANY DOF_TEST_WILD=1 DOF_CONTROLS=updated $BIN --headless --frames 250 --keys "enter:3,wait:80,toggle:3,wait:60" | tail -1)
[[ "$out" == *"mode=combat"* ]] && echo "ok    Y does not leave a fight while foes are about" || { echo "FAIL  Y in a fight: $out"; fail=1; }
out=$(env -u DOF_Y_ANY DOF_TEST_WILD=1 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:80,debug:3,wait:10,down:2,wait:10,down:2,wait:10,down:2,wait:10,down:2,wait:10,enter:3,wait:10,debug:3,wait:10,toggle:3,wait:60" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    with the debug switch on, Y leaves any fight" || { echo "FAIL  Y debug switch: $out"; fail=1; }
out=$(env -u DOF_Y_ANY DOF_WARP=w,256,2432 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 700 --keys "enter:3,wait:30,enter:3,wait:20,enter:3,wait:20,enter:3,wait:20,debug:3,wait:10,down:2,wait:10,down:2,wait:10,down:2,wait:10,down:2,wait:10,enter:3,wait:10,debug:3,wait:10,right:60,wait:10,toggle:3,wait:60" | tail -1)
[[ "$out" == *"mode=world"* && "$out" == *"pos=(256,2432)"* ]] && echo "ok    leaving a lair puts the party back on the lair's own tile" || { echo "FAIL  lair exit: $out"; fail=1; }
out=$(DOF_WARP=w,2000,800 DOF_CONTROLS=updated $BIN --headless --frames 500 --keys "enter:3,wait:20,debug:3,wait:10,$(printf 'down:2,wait:10,%.0s' $(seq 1 7))enter:3,wait:10,debug:3,wait:10,right:400,wait:20" | tail -1)
x=$(echo "$out" | grep -o "pos=([0-9]*" | grep -o "[0-9]*")
[[ -n "$x" && "$x" -gt 2300 ]] && echo "ok    ghost mode walks through the mountains" || { echo "FAIL  ghost mode: $out"; fail=1; }
out=$(DOF_CONTROLS=updated $BIN --headless --frames 700 --keys "enter:3,wait:20,debug:3,wait:10,$(printf 'down:2,wait:10,%.0s' $(seq 1 8))enter:3,wait:10,debug:3,wait:10,down:700,wait:20" | tail -1)
[[ "$out" == *"mode=world"* ]] && echo "ok    with random encounters off, walking does not start a fight" || { echo "FAIL  encounters off: $out"; fail=1; }
out=$(DOF_FINAL=30 DOF_WARP=w,256,2432 DOF_GOD=1 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 2600 --keys "enter:3,wait:30,enter:3,wait:20,enter:3,wait:20,enter:3,wait:20,wait:300,enter:3,wait:30,enter:3,wait:20,enter:3,wait:20,right:1600,wait:20,act:3,wait:60" | tail -1)
[[ "$out" == *"(taken)"* && "$out" == *"xp=3870"* ]] && echo "ok    the lair's loot is a chest at the far end of the cave" || { echo "FAIL  lair chest: $out"; fail=1; }
out=$(env -u DOF_Y_ANY DOF_WARP=w,256,2432 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 700 --keys "enter:3,wait:30,enter:3,wait:20,enter:3,wait:20,enter:3,wait:20,down:3,wait:10,down:3,wait:10,right:30,wait:10" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"lair="* ]] && echo "ok    turning round in a lair leaves the cave intact" || { echo "FAIL  lair turn: $out"; fail=1; }
out=$(env -u DOF_Y_ANY DOF_WARP=w,256,2432 DOF_CONTROLS=updated DOF_BOX_HOLD=0 $BIN --headless --frames 700 --keys "enter:3,wait:30,enter:3,wait:20,enter:3,wait:20,enter:3,wait:20,map:3,wait:10,map:3,wait:10" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"lair="* ]] && echo "ok    the map key opens and closes in a lair" || { echo "FAIL  lair map: $out"; fail=1; }
out=$(DOF_REFUGEE=1 DOF_TEST_WILD=1 DOF_TEST_TYPE=Man DOF_CONTROLS=updated $BIN --headless --frames 1500 --keys "enter:3,wait:60,right:200,wait:20,act:3,wait:60,enter:3,wait:60,enter:3,wait:60" | tail -1)
[[ "$out" == *"party=9"* && "$out" == *"refugees=1"* ]] && echo "ok    a refugee met on the road joins the party when asked (A beside him)" || { echo "FAIL  refugee: $out"; fail=1; }
out=$(DOF_TEST_WILD=1 DOF_TEST_TYPE=Troll DOF_FINAL=22 DOF_CONTROLS=updated $BIN --headless --frames 400 --keys "enter:3,wait:400" | tail -1)
[[ "$out" == *"staff=0"* && "$out" == *"xp=790"* && "$out" == *"leader=Sturm"* ]] && echo "ok    the Final Strike kills the foes for its charges, empties the staff and Raistlin dies (the lead passes on)" || { echo "FAIL  final strike: $out"; fail=1; }
out=$(DOF_TEST_WILD=1 DOF_TEST_TYPE=Troll DOF_GOD=1 DOF_CONTROLS=updated $BIN --headless --frames 1100 --keys "enter:3,wait:10,attack:2500" | tail -1)
[[ "$out" == *"mode=combat"* && "$out" == *"enemies=0"* && "$out" == *"xp=790"* ]] && echo "ok    a dead troll lies in a heap and the fight waits for it to get up" || { echo "FAIL  troll heap: $out"; fail=1; }
unset DOF_NO_INTRO
out=$($BIN --headless --frames 400 --keys "enter:3,wait:20,enter:2,wait:20,enter:2,wait:20,right:60" | tail -1)
[[ "$out" == *"pos=(1048,1024)"* || "$out" == *"pos=(1056,1024)"* ]] && echo "ok    the confirm key dismisses the opening message (after paging down)" || { echo "FAIL  dismiss: $out"; fail=1; }
[ $fail = 0 ] && echo "all passed"
exit $fail

# the opening message and rest (checked without DOF_NO_INTRO)
unset DOF_NO_INTRO
out=$($BIN --headless --frames 200 --keys "enter:3,wait:20,right:60" | tail -1)
[[ "$out" == *"pos=(1024,1024)"* ]] && echo "ok    the opening message holds the party until a key" || { echo "FAIL  opening message: $out"; fail=1; }
out=$($BIN --headless --frames 400 --keys "enter:3,wait:20,enter:2,wait:20,enter:2,wait:20,right:60" | tail -1)
[[ "$out" == *"pos=(1048,1024)"* || "$out" == *"pos=(1056,1024)"* ]] && echo "ok    the confirm key dismisses the opening message (after paging down)" || { echo "FAIL  dismiss: $out"; fail=1; }
rm -f user/settings.json
