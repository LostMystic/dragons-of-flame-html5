// Points of interest: little callbacks to the module "Dragons of Flame" scattered over the outdoor map (and a few inside the dungeons), the towns with their buildings, Elistan, and the scrolls.
// The places and their texts are data/pois.json. Each one fires once: a text, maybe a small treasure (taken into the shared pack or handed to whoever can use it), maybe a healing for the party,
// maybe a fight (the trolls' lair) whose reward comes when the foes are beaten.
#include <algorithm>
#include <cstring>

#include "game.h"
#include "../third_party/json/json.hpp"

namespace dof {

void Game::load_pois() {
    pois_loaded_ = true;
    pois_.clear();
    try {
        auto j = nlohmann::json::parse(assets_.read("data/pois.json"));
        for (auto& o : j["pois"]) {
            Poi p;
            p.name = o.value("name", ""); p.icon = o.value("icon", "marker"); p.accent = o.value("accent", 15); p.level = o.value("level", 1); p.x = o.value("x", 0); p.y = o.value("y", 0); p.text = o.value("text", "");
            if (o.contains("items")) p.items = o["items"].get<std::vector<int>>();
            p.heal = o.value("heal", 0); p.xp = o.value("xp", 0); p.town = o.value("town", false);
            if (o.contains("guard")) {   // one {slot, count} or a list of them (a mixed band)
                if (o["guard"].is_array()) { for (auto& g : o["guard"]) p.guard.push_back({g.value("slot", -1), g.value("count", 1)}); }
                else p.guard.push_back({o["guard"].value("slot", -1), o["guard"].value("count", 1)});
            }
            
            pois_.push_back(p);
        }
    } catch (...) {}
    {   // every outdoor place is moved to the nearest spot the party can really walk to from where it starts (a walk of 16-px steps over the map), so that it can always be reached
        const int W = 160, H = 192;
        std::vector<int> dist((size_t)W * H, -1);
        std::vector<int> q; int sx = std::clamp(px_ / 16, 0, W - 1), sy = std::clamp(py_ / 16, 0, H - 1); dist[sy * W + sx] = 0; q.push_back(sy * W + sx);
        for (size_t qi = 0; qi < q.size(); qi++) {
            int cx = q[qi] % W, cy = q[qi] / W;
            static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : d4) {
                int nx = cx + d[0], ny = cy + d[1];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H || dist[ny * W + nx] >= 0) continue;
                if (!world_.can_move(cx * 16, cy * 16, d[0] * 16, d[1] * 16)) continue;
                dist[ny * W + nx] = dist[cy * W + cx] + 1; q.push_back(ny * W + nx);
            }
        }
        for (auto& p : pois_) {
            if (p.level != 1) continue;
            int gx = std::clamp(p.x / 16, 0, W - 1), gy = std::clamp(p.y / 16, 0, H - 1);
            bool ok = false;
            for (int r = 0; r < 60 && !ok; r++)
                for (int dy = -r; dy <= r && !ok; dy++)
                    for (int dx = -r; dx <= r && !ok; dx++) {
                        if (std::max(abs(dx), abs(dy)) != r) continue;
                        int x = gx + dx, y = gy + dy;
                        if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1 || dist[y * W + x] < 0) continue;
                        p.x = x * 16; p.y = y * 16; ok = true;
                    }
            if (getenv("DOF_POIS")) fprintf(stderr, "poi %s at %d,%d reach %d\n", p.name.c_str(), p.x, p.y, ok ? dist[(p.y / 16) * W + (p.x / 16)] : -1);   // test hook
        }
    }
}

// The map icons of the places of interest: a 16x16 picture for each kind of place (cave mouth, camp, den, ruin, statue, standing stone, bridge, waterfall, battlefield, wagon, bones). The places
// not yet visited blink a yellow diamond above them; visited ones are drawn in greys.
void Game::draw_poi_icon(const Poi& p, int sx, int sy) {
    const bool done = poi_done_.count(p.name) > 0;
    auto C = [&](int c) { return done ? (c == 0 ? 0 : c == 3 || c == 15 ? 7 : 8) : c; };
    auto px = [&](int x, int y, int c) { int X = sx + x, Y = sy + y; if (X >= 0 && X < kW && Y >= 0 && Y < 128) screen_.px[Y * kW + X] = (uint8_t)C(c); };
    auto rect = [&](int x, int y, int w, int h, int c) { for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < w; xx++) px(x + xx, y + yy, c); };
    const std::string& k = p.icon;
    const int a = p.accent;
    if (k == "cave") {   // a grey hill with a black mouth and two eyes (the colour says whose)
        for (int y = 2; y < 15; y++) { int w = std::min(14, 4 + (y - 2) * 2); rect(8 - w / 2, y, w, 1, (y > 8 && w > 10) ? 8 : 7); px(8 - w / 2, y, 0); px(7 + w / 2, y, 0); }
        rect(5, 14, 6, 1, 0);
        rect(5, 8, 6, 7, 0); rect(6, 7, 4, 1, 0);
        px(6, 10, a); px(9, 10, a); px(7, 12, 3); px(8, 12, 3);
    } else if (k == "camp") {   // two small tents and a flag
        for (int r = 0; r < 7; r++) { rect(1 + (6 - r), 7 + r, 1 + 2 * r, 1, r % 2 ? 12 : 6); }
        for (int r = 0; r < 6; r++) { rect(9 + (5 - r), 9 + r, 1 + 2 * r, 1, r % 2 ? 12 : 6); }
        rect(7, 2, 1, 8, 12); rect(8, 2, 4, 3, a); rect(3, 14, 4, 1, 0);
    } else if (k == "den") {   // a dark hollow with bones around it
        rect(4, 6, 8, 6, 0); rect(5, 5, 6, 1, 0); rect(5, 12, 6, 1, 0); px(6, 8, a); px(9, 8, a);
        rect(1, 11, 4, 1, 3); px(1, 10, 3); px(4, 10, 3); rect(11, 13, 4, 1, 3); px(14, 12, 3); px(11, 12, 3); rect(2, 4, 3, 1, 3); rect(12, 4, 2, 1, 3);
    } else if (k == "ruins") {   // broken walls with soot and embers
        rect(1, 8, 4, 6, 8); rect(1, 8, 4, 1, 7); rect(7, 5, 3, 9, 8); rect(7, 5, 3, 1, 7); rect(12, 9, 3, 5, 8); rect(12, 9, 3, 1, 7);
        rect(0, 14, 16, 1, 0); px(5, 13, 6); px(10, 12, 6); px(6, 11, 5); px(11, 13, 15); rect(2, 4, 1, 3, 0); rect(8, 2, 1, 2, 0);
    } else if (k == "statue") {   // a figure on a plinth
        rect(3, 11, 10, 4, 7); rect(3, 11, 10, 1, 3); rect(3, 14, 10, 1, 8); rect(6, 3, 4, 8, 3); rect(7, 1, 2, 2, 3); rect(4, 5, 2, 1, 3); rect(10, 5, 2, 1, 3);
    } else if (k == "bridge") {   // planks over water
        rect(0, 2, 16, 4, 9); rect(0, 10, 16, 4, 9); rect(0, 6, 16, 4, 12);
        for (int x = 1; x < 16; x += 3) rect(x, 6, 1, 4, 0); rect(0, 5, 16, 1, 0); rect(0, 10, 16, 1, 0);
    } else if (k == "falls") {   // water pouring down a cliff into a pool
        rect(1, 0, 14, 11, 8); rect(5, 0, 6, 11, 10); for (int y = 0; y < 11; y += 2) { rect(6, y, 1, 1, 3); rect(9, y + 1, 1, 1, 3); }
        rect(2, 11, 12, 4, 9); rect(4, 12, 3, 1, 3); rect(9, 13, 3, 1, 3);
    } else if (k == "battle") {   // crossed swords over a shield
        for (int i = 0; i < 13; i++) { px(1 + i, 2 + i, 3); px(2 + i, 2 + i, 7); px(14 - i, 2 + i, 3); px(13 - i, 2 + i, 7); }
        rect(2, 11, 3, 1, 15); rect(11, 11, 3, 1, 15); rect(6, 9, 4, 5, 5); px(7, 10, 15); px(8, 11, 15);
    } else if (k == "wagon") {   // an overturned wagon
        rect(2, 5, 12, 6, 12); rect(2, 5, 12, 1, 6); rect(2, 10, 12, 1, 0);
        for (int dx = 0; dx < 2; dx++) { int cx = 4 + dx * 8; rect(cx - 2, 9, 5, 5, 0); rect(cx - 1, 10, 3, 3, 7); px(cx, 11, 0); }
    } else if (k == "bones") {   // a skull and crossbones
        rect(5, 3, 6, 6, 3); px(6, 5, 0); px(9, 5, 0); rect(7, 7, 2, 1, 0); rect(6, 9, 4, 2, 3);
        for (int i = 0; i < 6; i++) { px(2 + i, 10 + i / 2, 3); px(13 - i, 10 + i / 2, 3); }
    } else if (k == "village") {   // the original map already draws the village on its own tile: only the blinking diamond
    } else if (k == "town") {   // a village that fits its tile: two small houses (one with a chimney), a lane and a tree
        rect(0, 14, 16, 1, 7); rect(3, 15, 9, 1, 8);                                     // the lane
        rect(2, 5, 3, 1, 5); rect(1, 6, 5, 1, 6); rect(0, 7, 7, 1, 5);                    // the low house: roof
        rect(1, 8, 5, 5, 12); rect(1, 8, 5, 1, 6); rect(0, 13, 7, 1, 0); px(2, 9, 15); px(4, 9, 15); rect(3, 10, 1, 3, 0);
        rect(10, 3, 3, 1, 5); rect(9, 4, 5, 1, 6); rect(8, 5, 7, 1, 5);                   // the tall house: roof
        rect(9, 6, 5, 7, 12); rect(8, 13, 7, 1, 0); px(10, 8, 15); px(12, 8, 15); px(10, 10, 15); rect(11, 10, 1, 3, 0); rect(12, 1, 2, 3, 8); px(13, 0, 3);   // walls, windows, door, chimney and smoke
        rect(14, 10, 2, 2, 14); rect(14, 9, 1, 1, 13); px(15, 12, 12); px(15, 13, 12);    // a small tree at the edge
    } else if (k != "none") {   // a standing stone with a rune
        rect(5, 2, 6, 12, 7); rect(5, 2, 6, 1, 3); rect(9, 3, 2, 11, 8); rect(5, 14, 6, 1, 0); rect(7, 6, 2, 4, 15); px(6, 8, 15); px(9, 8, 15);
    }
    if (!done && k != "none" && (render_frames_ / 15) % 2 == 0) { px(7, -4, 15); px(6, -3, 15); px(7, -3, 15); px(8, -3, 15); px(7, -2, 15); }   // a blinking diamond: something to see here
}

// Hands an item to the party: the hero who gains most takes it up (updated controls), otherwise it goes in the shared pack.
std::string Game::give_item_text(int id) {
    std::string m;
    if (!orig_controls_ && auto_equip(id, m, -1)) return m;
    if (inv_.size() < kPackMax) { inv_.push_back(id); return "Took " + item_name(id) + "."; }
    return "(no room for " + item_name(id) + ")";
}

void Game::start_fight_here(const std::vector<int>& slots, bool lair, const std::string& lair_name) {
    cx_ = 0;
    strip_.build(world_, px_, py_, east_west_);
    cx_ = strip_.start_x(px_, py_, east_west_);
    lair_active_ = false;
    cy_ = ground_y(cx_) - 16;
    if (lair) {   // into the cave: a small flat room, the party at its left end
        int w = strip_.width_px();
        lair_ground_ = ground_y(cx_);
        const int len = std::min(w, 1360);   // two rooms and a passage between them: about four screens of cave
        lair_x0_ = std::clamp(cx_ - len / 2, 0, std::max(0, w - len)); lair_x1_ = lair_x0_ + len;
        lair_active_ = true; lair_chest_taken_ = false; lair_msg_ = false; lair_poi_name_ = lair_name;
        lair_chest_x_ = lair_x1_ - 62;   // the loot is in the far room, against the end wall
        cx_ = lair_x0_ + len / 2 - 120;   // the party starts in the first room, a little past its middle: foes come from both sides
        cy_ = lair_ground_ - 16;
    }
    spawn_encounter_at((int)slots.size(), slots);
    from_wild_ = true;
    mode_ = Mode::Combat;
    message_ = "Enemies!"; message_steps_ = 12;
}

void Game::give_poi_reward() {
    if (poi_reward_.empty() && !poi_reward_xp_) return;
    std::string t = poi_reward_text_;
    for (int id : poi_reward_) t += (t.empty() ? "" : "\n") + give_item_text(id);
    if (poi_reward_xp_) { party_[leader_].xp += poi_reward_xp_; xp_ += poi_reward_xp_; }
    poi_reward_.clear(); poi_reward_xp_ = 0; poi_reward_text_.clear();
    if (!t.empty()) box_ = t;
}

void Game::trigger_poi(Poi& p) {
    poi_done_.insert(p.name);
    std::string t = p.text;
    if (p.heal) { for (auto& h : party_) if (h.hp > 0) h.hp = h.hp_max; }
    if (!p.guard.empty()) {   // a fight first; what the place holds is the reward
        poi_reward_ = p.items; poi_reward_xp_ = p.xp; poi_reward_text_ = "You search the lair.";
        std::vector<int> slots;
        for (auto& g : p.guard) for (int k = 0; k < g.second; k++) slots.push_back(g.first);
        box_ = t;
        start_fight_here(slots, true, p.name);   // the lair itself: a cave
        return;
    }
    for (int id : p.items) t += "\n" + give_item_text(id);
    if (p.xp) { party_[leader_].xp += p.xp; xp_ += p.xp; }
    box_ = t;
}

// Outdoors and in the dungeons: a place of interest the party walks onto.
void Game::check_pois() {
    if (!pois_loaded_) load_pois();
    if (!box_.empty() || mode_ != Mode::World) return;
    for (auto& p : pois_) {
        if (poi_done_.count(p.name) || p.level != level_) continue;
        if (p.name == poi_block_) { if (level_ == 1 && (abs(px_ - p.x) > 48 || abs(py_ - p.y) > 48)) poi_block_.clear(); else continue; }
        bool hit = level_ == 1 ? (abs(px_ - p.x) < 20 && abs(py_ - p.y) < 20) : (abs(ix_ + 8 - p.x) < 28 && abs(iy_ + 8 - p.y) < 28);
        if (hit) { trigger_poi(p); return; }
    }
}

// Elistan, the wounded priest of Haven, among the women of Pax Tharkas (level 6). Goldmoon can heal him; he then takes Paladine as his god and the Medallion of Faith duplicates.
void Game::elistan_step(const Npc& p) {
    int px = ix_ + 8, py = iy_ + 8;
    bool near = abs(p.lx - px) < 48 && abs(p.ly - py) < 48;
    if (!near) { elistan_near_ = false; return; }
    if (elistan_near_ || !box_.empty()) return;
    elistan_near_ = true;
    bool goldmoon = false;
    for (auto& h : party_) if (h.hp > 0 && h.name.rfind("Goldmoon", 0) == 0) goldmoon = true;
    if (elistan_state_ == 1) {
        static const char* thanks[3] = {
            "Elistan: \"Thank you, my friends. Do not worry about me: I will see that they get out. Maritta and I will hold the women together. Go, go on your way, and hurry!\"",
            "Elistan: \"Paladine watch over you. Free the children and the others; I will guide the people when you do. Hurry on!\"",
            "Elistan: \"Go! You have done enough for one man. I shall make sure no one is left behind.\""};
        box_ = thanks[elistan_talk_++ % 3];
        return;
    }
    if (!goldmoon) {
        box_ = "A grey-haired man lies on a straw pallet among the women, bruised and feverish from a fall in the mines. Maritta the seamstress says he is Elistan, high priest of Haven, a cleric of the false gods. He is dying, and nothing the party carries can mend him. A true cleric might.";
        return;
    }
    box_ = "A grey-haired man lies on a straw pallet among the women, bruised and feverish from a fall in the mines. Maritta the seamstress says he is Elistan, high priest of Haven, a good man who taught the false gods, and the Dragonlords let the women tend him only because they think he will die.\n\nHe notices Goldmoon's medallion. 'Are you a cleric? Can you heal?'\n\nUse Goldmoon to try to heal his wounds?\n\n     A: Heal him      B: Leave him";
    choice_ = 1;
}

void Game::choice_answer(bool yes) {
    int c = choice_; choice_ = 0;
    box_.clear();
    if (c == 1) {
        if (!yes) { elistan_near_ = true; box_ = "You leave the man to Maritta's care. His breathing is shallow."; return; }
        elistan_state_ = 1; elistan_talk_ = 0;
        Hero* gm = nullptr;
        for (auto& h : party_) if (h.hp > 0 && h.name.rfind("Goldmoon", 0) == 0) gm = &h;
        box_ = "Goldmoon kneels and lays her hands on him. The Medallion of Faith glows, and the old man's fever breaks: the bruises fade, his eyes clear.\n\nElistan stares at her in wonder. 'I saw it... a light, and a presence beyond the false gods I preached. They are real. The true gods are real.' He weeps. 'I renounce what I taught. I give my life to Paladine, the god of light!'\n\nAs he speaks, Goldmoon's Medallion of Faith duplicates itself. The new medallion bears the symbol of Paladine, and hangs about Elistan's neck. 'Go,' he says. 'Free the children. I will see that the people are ready to follow.'";
        if (gm) { gm->xp += 1000; xp_ += 1000; }
    }
}

// The scrolls (Use, with the right leader): clerical ones for Goldmoon, magic-user ones for Raistlin. Returns true if the scroll was read (and is used up).
bool Game::use_scroll(int id) {
    const std::string& who = party_[leader_].name;
    bool gm = who.rfind("Goldmoon", 0) == 0, rl = who.rfind("Raistlin", 0) == 0;
    auto fight_spell = [&](int spell) {
        if (enemies_.empty()) { box_ = "There is nothing to\nstrike here."; return false; }
        int s = staff_; bool sb = staff_broken_; staff_ = 100; staff_broken_ = false;
        cast(1, spell);
        staff_ = s; staff_broken_ = sb;
        return true;
    };
    switch (id) {
        case 21: if (!gm) { box_ = "Only Goldmoon can read\nthis scroll."; return false; } apply_heal(rnd(3, 24), false); fx(kFxCure); return true;           // cure serious wounds
        case 22: if (!gm) { box_ = "Only Goldmoon can read\nthis scroll."; return false; } apply_heal(rnd(6, 48), false); fx(kFxCure); return true;           // cure critical wounds
        case 23: if (!gm) { box_ = "Only Goldmoon can read\nthis scroll."; return false; } apply_heal(0, true); fx(kFxCure); return true;                    // raise dead
        case 73: if (!gm) { box_ = "Only Goldmoon can read\nthis scroll."; return false; }                                                                  // cure serious wounds, prayer, find traps
            apply_heal(rnd(3, 24), false); ward_ = 600; ward_kind_ = 2; ward_pts_ = std::max(ward_pts_, 1); bless_ = 600; reveal_ = 1200; fx(kFxPrayer); return true;
        case 74: if (!gm) { box_ = "Only Goldmoon can read\nthis scroll."; return false; } apply_heal(rnd(1, 8), false); fx(kFxCure); return true;               // light, augury, cure light wounds
        case 25: if (!rl) { box_ = "Only Raistlin can read\nthis scroll."; return false; } return fight_spell(7);                                           // fireball
        case 26: if (!rl) { box_ = "Only Raistlin can read\nthis scroll."; return false; } return fight_spell(3);                                           // web
        case 28: if (!rl) { box_ = "Only Raistlin can read\nthis scroll."; return false; } return fight_spell(2);                                           // magic missile
        default: return false;
    }
}

}  // namespace dof
