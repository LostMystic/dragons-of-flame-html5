// F1: the list of commands. With the original controls it lists the keyboard commands, with the updated controls the gamepad commands
// (and the keyboard and mouse extras). It scrolls with up / down or the mouse wheel; F1, Esc, Enter or a click closes it.
#include <algorithm>

#include "game.h"

namespace dof {
namespace {

// A line starting with '#' is a heading; "Key: what it does" shows the key part in yellow. Long lines are wrapped at build time.
void add(std::vector<std::string>& out, const std::string& line) {
    const size_t cols = 35;
    if (line.empty() || line[0] == '#' || line.size() <= cols) { out.push_back(line); return; }
    size_t colon = line.find(": ");
    std::string indent = colon == std::string::npos ? "" : "  ";
    std::string cur, word;
    size_t i = 0;
    bool first = true;
    while (i <= line.size()) {
        size_t e = line.find(' ', i); if (e == std::string::npos) e = line.size();
        word = line.substr(i, e - i);
        if (cur.size() + word.size() + 1 > cols && !cur.empty()) { out.push_back(first ? cur : indent + cur); cur.clear(); first = false; }
        if (!cur.empty()) cur += ' ';
        cur += word;
        i = e + 1;
    }
    if (!cur.empty()) out.push_back(first ? cur : indent + cur);
}

}  // namespace

std::vector<std::string> Game::help_lines() const {
    std::vector<std::string> v;
    auto L = [&](const std::string& s) { add(v, s); };
    if (orig_controls_) {
        L("#KEYBOARD COMMANDS");
        L("#Moving");
        L("Keypad 1-9: walk in that direction (Num Lock on). On the map all eight directions, in the side view left and right.");
        L("Arrow keys, W A S D: walk as well.");
        L("0: switch between the map and the side view (Tab does too).");
        L("Keypad 8 / 2: in the side view, turn to the other direction (north-south / east-west).");
        L("Keypad 7 / 9: jump left / right (while + is held they aim a high attack instead).");
        L("#Fighting (hold two keys together)");
        L("Hold the keypad + key and a keypad number at the same time, and keep both held: the hero keeps swinging. Do not tap them.");
        L("+ and 4 or 6: attack left or right.");
        L("+ and 7 or 9: high attack, left or right (sword raised).");
        L("+ and 1 or 3: low attack, left or right (crouching).");
        L("+ alone, X or Space: attack the way you face.");
        L("The crossed swords by the compass: lit means a foe is in reach and the hero fights hand to hand; dark means the ranged weapon (bow, sling, spear) is used.");
        L("The number keys along the top of the keyboard (1-8) are not for fighting: they choose the party leader.");
        L("#Menus and keys");
        L("Space or Enter: the main menu on the map. Esc: the menu in the side view, and back out of any menu.");
        L("Up / Down, Enter: choose in a menu.");
        L("Q: the quest map, an overview of the land.");
        L("1-8: make that party member the leader.");
        L("F5 / F9: save / load the game.");
        L("V: sound on / off.");
        L("F11: full screen.");
        L("F1: this list.");
        L("Shift + F1 to F7: jump to a level (a cheat).");
    } else {
        L("#GAMEPAD COMMANDS");
        L("#Moving");
        L("Left stick / D-pad: walk. On the map all directions, in the side view left and right.");
        L("B: jump (in menus: back one step).");
        L("Right trigger: turn the side view to the next perpendicular direction.");
        L("Y: switch between the map and the side view.");
        L("LB / RB: the previous / next party leader.");
        L("#Fighting and acting");
        L("A: a foe in reach is attacked first. Otherwise A picks up what lies within reach, then opens a door or chest, and if there is nothing it just attacks.");
        L("X: attack. With the stick centred it is a low attack; hold the stick up for a high attack.");
        L("Left trigger (LT): the ranged attack. Heroes with a bow, sling, spear or throwing axe fire it while they have ammunition (arrows and the rest are shared in the pack and the next one is taken up on its own).");
        L("Raistlin casts his weakest damaging spell first and works up to the strongest; Goldmoon uses her turn undead, hammer and hold spells on foes, and with none about she heals, then blesses and protects the party, and finds traps underground. The Disks and Final strike only when you choose them from the list.");
        L("#Menus");
        L("Start: pause (opens the main menu; B or Exit menu closes it).");
        L("Back: the world (quest) map. Back again closes it.");
        L("D-pad / stick: choose. A: confirm. B: back.");
        L("#KEYBOARD");
        L("Arrow keys / W A S D / keypad: walk. Up jumps, Down turns the side view. Q / E: jump left / right.");
        L("Space or Enter: menu and confirm. X or keypad +: attack. R: ranged attack. Tab or 0: switch view.");
        L("1-8: choose the leader. F5 / F9: save / load. V: sound. F11: full screen. F1: this list.");
        L("#MOUSE");
        L("Click menu rows to choose them, click outside a scroll to go back, the wheel scrolls lists.");
        L("Click a portrait to make that hero the leader. Click in the view to attack, pick up or open (it does not walk the party).");
    }
    return v;
}

void Game::open_help() {
    help_return_ = mode_;
    mode_ = Mode::Help;
    help_scroll_ = 0;
    help_close_ = false;
    screen_.pal = kGamePalette;
}

void Game::step_help(const Input& in) {
    auto lines = help_lines();
    int visible = 18, maxs = std::max(0, (int)lines.size() - visible);
    if (edges_[0]) help_scroll_ = std::max(0, help_scroll_ - 1);
    if (edges_[1]) help_scroll_ = std::min(maxs, help_scroll_ + 1);
    if (edges_[2]) help_scroll_ = std::max(0, help_scroll_ - visible + 1);
    if (edges_[3]) help_scroll_ = std::min(maxs, help_scroll_ + visible - 1);
    if (edges_[4] || edges_[5] || in.click || help_close_) {
        help_close_ = false;
        mode_ = help_return_;
        if (mode_ == Mode::Title) screen_.pal = (title_page_ == 2 || title_page_ == 4 || title_page_ == 5) ? kGamePalette : title_pal_;
    }
}

void Game::draw_help() {
    screen_.reset_clip();
    screen_.clear(0);
    screen_.fill_rect(8, 4, 304, 12, 7);
    screen_.text(orig_controls_ ? 100 : 108, 6, orig_controls_ ? "KEYBOARD COMMANDS" : "GAMEPAD COMMANDS", 0, false);
    auto lines = help_lines();
    int visible = 18, maxs = std::max(0, (int)lines.size() - visible);
    help_scroll_ = std::clamp(help_scroll_, 0, maxs);
    for (int i = 0; i < visible && help_scroll_ + i < (int)lines.size(); i++) {
        const std::string& s = lines[help_scroll_ + i];
        int y = 20 + i * 9;
        if (!s.empty() && s[0] == '#') { screen_.text(12, y, s.c_str() + 1, 15); continue; }
        size_t colon = s.find(": ");
        if (colon != std::string::npos && s[0] != ' ') {
            screen_.text(12, y, s.substr(0, colon + 1).c_str(), 1);
            screen_.text(12 + (int)(colon + 2) * 8, y, s.substr(colon + 2).c_str(), 3);
        } else screen_.text(12, y, s.c_str(), 3);
    }
    bool blink = (steps_ / 3) % 2 == 0;
    auto arrow = [&](int x, int y, bool down) { for (int r = 0; r < 4; r++) { int w = down ? 7 - 2 * r : 1 + 2 * r; screen_.fill_rect(x + (7 - w) / 2, y + r, w, 1, 15); } };
    if (help_scroll_ < maxs && blink) arrow(300, 186, true);
    if (help_scroll_ > 0) arrow(300, 20, false);
    screen_.text(12, 190, "Up/Down: scroll   Esc/Enter/F1: close", 8, false);
}

}  // namespace dof

// ---- F10: the debug menu. Cheats for testing; none of it is needed to play.
namespace dof {
namespace {
const char* const kDebugRows[] = {"Warp to the dungeon + Laurana", "Everyone joins the party", "God mode on / off", "Run: the party walks 5x faster", "Y leaves any fight at once", "Exit door: all quest parts done", "Exit door: none of the quest done", "Ghost mode: walk through walls", "Random encounters off", "Close"};
}

// To just outside Sla-Mori's door, with Laurana in the party.
void Game::debug_warp_entrance() {
    for (auto& h : all_heroes_) if (h.name.rfind("Princess Laurana", 0) == 0) join_party(h);
    entrance_known_ = true;
    int ex = 640, ey = 2688;
    for (auto& l : links_) if (l.from == 1 && l.mode == 3) { ex = l.x1 / 8 + 16; ey = l.y1 / 8; }
    enemies_.clear(); ienemies_.clear(); npcs_.clear(); from_wild_ = false;
    level_ = 1; box_.clear();
    px_ = ex + 8; py_ = ey;   // one step east of the door: walk west to go in
    fix_start();
    debug_return_ = Mode::World;
    mode_ = Mode::World;
    message_ = "Warped to the entrance"; message_steps_ = 12;
}

void Game::step_debug(const Input&) {
    const int n = (int)(sizeof kDebugRows / sizeof *kDebugRows);
    if (edges_[0]) debug_sel_ = (debug_sel_ + n - 1) % n;
    if (edges_[1]) debug_sel_ = (debug_sel_ + 1) % n;
    confirm_prev_ = true;   // the key that chose a row must not also open the main menu in the world
    if (edges_[4]) { mode_ = debug_return_; return; }
    if (edges_[5]) {
        switch (debug_sel_) {
            case 0: debug_warp_entrance(); break;
            case 1: for (auto& h : all_heroes_) join_party(h); mode_ = debug_return_; break;
            case 2: god_mode_ = !god_mode_; save_settings(); break;
            case 3: run_ = !run_; break;
            case 4: y_exit_any_ = !y_exit_any_; break;
            case 5: case 6: {   // the end of the quest, to be tried: the party stands in the corridor just north of the exit door of Pax Tharkas (level 6, the south door), facing it. Walk down to the door, press A to open it
                                // and walk through: that starts the ending. "All done" has every part of the quest flagged done (Laurana with the party), "nothing done" none of it.
                if (debug_sel_ == 5) {
                    quest_flags_ = 0;
                    for (auto& h : all_heroes_) if (h.name.rfind("Princess Laurana", 0) == 0) join_party(h);
                    entrance_known_ = true;
                } else {
                    quest_flags_ = 0x0f;
                    for (size_t i = 0; i < party_.size(); i++) if (party_[i].name.rfind("Princess Laurana", 0) == 0) { party_.erase(party_.begin() + i); if (leader_ >= (int)party_.size() || leader_ > (int)i) leader_ = std::max(0, std::min(leader_ - (leader_ > (int)i ? 1 : 0), (int)party_.size() - 1)); break; }
                }
                enemies_.clear(); corpses_.clear(); ienemies_.clear(); box_.clear();
                mode_ = Mode::World; debug_return_ = Mode::World;
                enter_level(6, 13, 21);   // the corridor two cells north of the exit door, turned to face south (the view runs north-south)
                iv_ns_ = true; face_left_ = true; istrip_key_ = -1;
                break; }
            case 7: ghost_ = !ghost_; break;
            case 8: no_encounters_ = !no_encounters_; break;
            default: mode_ = debug_return_; break;
        }
    }
}

void Game::draw_debug() {
    screen_.reset_clip();
    screen_.clear(0);
    screen_.fill_rect(8, 4, 304, 12, 7);
    screen_.text(124, 6, "DEBUG MENU", 0, false);
    const int n = (int)(sizeof kDebugRows / sizeof *kDebugRows);
    const int visible = 10;   // the rows that fit above the position line; a longer list scrolls
    debug_scroll_ = std::clamp(debug_scroll_, std::max(0, debug_sel_ - visible + 1), std::min(debug_sel_, std::max(0, n - visible)));
    for (int i = debug_scroll_; i < std::min(n, debug_scroll_ + visible); i++) {
        int y = 24 + (i - debug_scroll_) * 12;
        if (i == debug_sel_) screen_.fill_rect(10, y - 2, 300, 11, 5);
        std::string t = kDebugRows[i];
        if (i == 2) t += god_mode_ ? ":  ON" : ":  off";
        if (i == 3) t += run_ ? ":  ON" : ":  off";
        if (i == 4) t += y_exit_any_ ? ":  ON" : ":  off";
        if (i == 7) t += ghost_ ? ":  ON" : ":  off";
        if (i == 8) t += no_encounters_ ? ":  ON" : ":  off";
        screen_.text(16, y, t.c_str(), i == debug_sel_ ? 15 : 7, false);
    }
    if (debug_scroll_ > 0) screen_.text(300, 18, "^", 15, false);                              // more above
    if (debug_scroll_ + visible < n) screen_.text(300, 24 + visible * 12 - 2, "v", 15, false);    // more below
    {   // where the party is, to line things up with the X on the map
        char pos[96];
        if (interior()) snprintf(pos, sizeof pos, "level %d  cell %d,%d  (x %d y %d)", level_, ix_ >> 7, iy_ >> 7, ix_, iy_);
        else { int ex = 640, ey = 2688; for (auto& l : links_) if (l.from == 1 && l.mode == 3) { ex = l.x1 / 8 + 16; ey = l.y1 / 8; }
               snprintf(pos, sizeof pos, "you %d,%d   X (Sla-Mori) %d,%d", px_, py_, ex, ey); }
        screen_.text(12, 160, "Position", 15, false);
        screen_.text(12, 172, pos, 7, false);
    }
    screen_.text(12, 190, "Up/Down: choose  Enter: do  F10: close", 8, false);
}
}  // namespace dof
