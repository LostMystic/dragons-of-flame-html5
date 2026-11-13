#include <cstdlib>
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../third_party/json/json.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace dof {

static int melee_fx(int wc, int kind) { return wc == 3 ? kFxWyrmWhiff + kind : kFxWhiffSword + wc * 3 + kind; }   // wc: 0 sword, 1 staff, 2 axe, 3 Wyrmslayer
static bool is_draconian(const std::string& n) { return n == "Baaz" || n == "Kapak" || n == "Bozak"; }
static int baaz_statue_steps(const Enemy& e) { return 18 + (((e.x >> 3) + e.y) & 3) * 6; }   // 1 to 4 rounds as a statue (18..36 steps) before it crumbles
static bool is_undead(const std::string& n) { return n == "Zombie" || n == "Wight" || n == "Wraith" || n == "Shadow" || n == "Skeleton" || n == "Ghoul" || n == "Spectre" || n == "Ghost"; }
static void wrap_text(Screen& s, int x, int y, const std::string& t, int cols, int maxlines, int colour, int skip, int step = 10);
static int wrap_count(const std::string& t, int cols) {  // how many lines wrap_text makes of t
    int rows = 0; size_t line = 0, i = 0;
    while (i < t.size()) {
        size_t e = t.find(' ', i); if (e == std::string::npos) e = t.size();
        size_t w = e - i;
        if (line + w + 1 > (size_t)cols && line > 0) { rows++; line = 0; }
        line += (line ? 1 : 0) + w; i = e + 1;
    }
    if (line) rows++;
    return rows;
}


int Game::rnd(int lo, int hi) {
    rng_ = rng_ * 1664525u + 1013904223u;
    if (hi <= lo) return lo;
    return lo + (int)((rng_ >> 8) % (unsigned)(hi - lo + 1));
}

bool Game::init(const std::string& root) {
    if (!assets_.init(root)) return false;
    turbo_ = getenv("DOF_TURBO") != nullptr;
    using nlohmann::json;
    try {
        json r = json::parse(assets_.read("data/rules.json"));
        rules_.tick_hz = r.value("tick_hz", rules_.tick_hz);
        rules_.ticks_per_step = r.value("ticks_per_step", rules_.ticks_per_step);
        rules_.fast_walk = r.value("fast_walk_px_per_s", rules_.fast_walk);
        rules_.walk_step = r.value("walk_step_px", rules_.walk_step);
        if (r.contains("start")) { rules_.start_x = r["start"].value("x", rules_.start_x); rules_.start_y = r["start"].value("y", rules_.start_y); }
        if (r.contains("walk_frames")) rules_.walk_frames = r["walk_frames"].get<std::vector<int>>();
        if (r.contains("blocked")) { rules_.blocked_mask = r["blocked"].value("mask", 15); rules_.blocked_value = r["blocked"].value("value", 1); }
        if (r.contains("combat")) {
            auto& c = r["combat"];
            if (c.contains("hero_damage")) { rules_.hero_dmg_min = c["hero_damage"][0]; rules_.hero_dmg_max = c["hero_damage"][1]; }
            rules_.hero_reach = c.value("hero_reach", rules_.hero_reach);
            rules_.enemy_cooldown = c.value("enemy_cooldown", rules_.enemy_cooldown);
            rules_.enemy_aggro = c.value("enemy_aggro", rules_.enemy_aggro);
            rules_.encounter_count = c.value("encounter_count", rules_.encounter_count);
            if (c.contains("encounter_types")) rules_.encounter_types = c["encounter_types"].get<std::vector<std::string>>();
            if (c.contains("attack_frames")) rules_.attack_frames = c["attack_frames"].get<std::vector<int>>();
            rules_.seed = c.value("seed", rules_.seed);
        }
        json p = json::parse(assets_.read("data/panel.json"));
        for (auto& s : p["slots"]) slots_.push_back({s["x"], s["y"]});
        if (p.contains("bar2")) bar2_ = p["bar2"].get<std::vector<double>>();
        if (p.contains("compass")) { compass_ = {p["compass"]["x"], p["compass"]["y"]}; compass_clip_ = p["compass"]["clip"]; }
        json pj = json::parse(assets_.read("data/party.json"));
        for (auto& h : pj["heroes"]) {
            Hero hero;
            hero.name = h["name"]; hero.sprite = h["sprite"]; hero.hp = h["hp"]; hero.hp_max = h["hp_max"]; hero.clip = h["clip"];
            if (h.contains("abilities")) { hero.abilities = h["abilities"].get<std::vector<int>>(); if (hero.abilities.size() > 6) hero.armor = hero.abilities[6]; }
            hero.actor = assets_.actor(hero.sprite);
            if (hero.actor.frames.empty()) return false;
            party_.push_back(std::move(hero));
        }
        for (auto& hh : party_) equip(hh);
        all_heroes_ = party_;
        try {
            nlohmann::json nj = nlohmann::json::parse(assets_.read("data/nomads.json"));
            Actor nomad_actor = assets_.actor("nomad");
            for (auto& n : nj["nomads"]) {
                Hero nh; nh.name = n["name"]; nh.sprite = "nomad"; nh.hp = nh.hp_max = n["hp"]; nh.abilities = n["abilities"].get<std::vector<int>>();
                nh.armor = nh.abilities.size() > 6 ? nh.abilities[6] : 6; nh.nomad = true; nh.actor = nomad_actor;
                equip(nh);
                nh.clip = 18;  // the plain man's portrait: Eben joined with this one in the original
                all_heroes_.push_back(nh);
            }
        } catch (...) {}
        party_.resize(std::min<size_t>(party_.size(), pj.value("party_size", 8)));
        json mj = json::parse(assets_.read("data/monsters.json"));
        armor_table_ = mj["armor_table"].get<std::vector<int>>();
        std::vector<int> pool_slots = mj["encounter_pool_slots"].get<std::vector<int>>();
        for (auto& [id, m] : mj["types"].items()) {
            MonsterType t;
            t.id = id; t.hp = m.value("hp", 10); t.dmg_min = m["damage"][0]; t.dmg_max = m["damage"][1];
            t.speed = m.value("speed", 4); t.reach = m.value("reach", 32);
            t.slot = m.value("slot", 2); t.ac = m.value("field4", 6); t.hp_dice = m.value("hp_dice", 1); t.hp_bonus = m.value("hp_bonus", 0);
            t.dmg_sides = m.value("dmg_sides", 4); t.dmg_bonus = m.value("dmg_bonus", 1); t.to_hit = m.value("to_hit", 3);
            t.walk = m["walk_frames"].get<std::vector<int>>(); t.attack = m["attack_frames"].get<std::vector<int>>();
            if (m.contains("death_frames")) t.death = m["death_frames"].get<std::vector<int>>();
            t.actor = assets_.actor(m.value("sprite", id));
            if (int wide = m.value("wide", 0)) {  // wolves, dogs and the griffon are two 32-px pieces side by side (body, head): join them into 64-px frames
                std::vector<Image> joined;
                for (int k = 0; k < wide && 2 * k + 1 < (int)t.actor.frames.size(); k++) {
                    const Image& a = t.actor.frames[2 * k]; const Image& b = t.actor.frames[2 * k + 1];
                    Image j; j.w = a.w + b.w; j.h = std::max(a.h, b.h); j.px.assign((size_t)j.w * j.h, 0);
                    for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) j.px[(size_t)y * j.w + x] = a.px[(size_t)y * a.w + x];
                    for (int y = 0; y < b.h; y++) for (int x = 0; x < b.w; x++) j.px[(size_t)y * j.w + a.w + x] = b.px[(size_t)y * b.w + x];
                    joined.push_back(std::move(j));
                }
                if (!joined.empty()) t.actor.frames = std::move(joined);
            }
            t.name = m.value("name", id); t.xp = m.value("xp", 10);
            if (!t.actor.frames.empty()) mtypes_.push_back(std::move(t));
        }
    } catch (...) {
        fprintf(stderr, "data/*.json is missing or invalid\n");
        return false;
    }
    if (party_.empty() || mtypes_.empty()) return false;
    {  // creatures that can be met: the original's pool (table 0x2868) limited to the types whose sprite file exists
        json mj = json::parse(assets_.read("data/monsters.json"));
        for (int sl : mj["encounter_pool_slots"].get<std::vector<int>>())
            for (size_t i = 0; i < mtypes_.size(); i++)
                if (mtypes_[i].slot == sl) pool_.push_back((int)i);
        if (pool_.empty()) pool_.push_back(0);
        figs_ = assets_.figures("mpl1");
        figs0_ = assets_.figures("mpl0");
        figs2_ = assets_.figures("mpl2");
        utiles_ = assets_.tiles("utiles");
        auto d = assets_.game_file("utokens");
        for (size_t b = 0; b * 132 + 132 <= d.size(); b++) {
            std::array<uint16_t, 64> t{};
            for (int i = 0; i < 64; i++) t[i] = d[b * 132 + 4 + i * 2] | (d[b * 132 + 5 + i * 2] << 8);
            utokens_.push_back(t);
        }
        kills_by_type_.assign(mtypes_.size(), 0);
        try { json iv = json::parse(assets_.read("data/item_values.json")); item_values_ = iv["values"].get<std::vector<int>>(); } catch (...) {}
        try { json ep = json::parse(assets_.read("data/epilogue.json")); epi_complete_ = ep["complete"].get<std::vector<std::string>>(); epi_incomplete_ = ep["incomplete"].get<std::vector<std::string>>(); } catch (...) {}
        highscores_ = {{"Richard J Lilley", 55000}, {"Sarah", 45000}, {"Kevin", 35000}, {"Kate", 18000}, {"Graham", 1000}};
        try {
            json qm = json::parse(assets_.read("data/quest_map.json"));
            for (int r = 0; r < 24; r++) for (int c = 0; c < 20; c++) { qm_terrain_[r * 20 + c] = qm["terrain"][r][c]; qm_armies_[r * 20 + c] = qm["armies"][r][c]; }
            for (int i = 0; i < 128; i++) qm_factor_[i] = qm["factor"][i];
            sbits_ = assets_.tiles("sbits");
            build_fire_frames();
        } catch (...) {}
        god_ = getenv("DOF_GOD") != nullptr;
        if (getenv("DOF_ARMY_TICKS")) for (int i = atoi(getenv("DOF_ARMY_TICKS")); i > 0; i--) army_tick();
        try {
            json it = json::parse(assets_.read("data/interior_tables.json"));
            itab_ew_ = it["cell_to_block_ew"].get<std::vector<int>>(); itab_ns_ = it["cell_to_block_ns"].get<std::vector<int>>(); iexits_ = it["exits"].get<std::vector<int>>();
            if (it.contains("secret_flags")) isecret_ = it["secret_flags"].get<std::vector<int>>();
            itable_loaded_ = true;
        } catch (...) {}
        try {
            json lk = json::parse(assets_.read("data/level_links.json"));
            for (auto& l : lk["links"]) links_.push_back({l["from"], l["to"], l["mode"], l["flags"], l["x1"], l["y1"], l["x2"], l["y2"], l["dest_x"], l["dest_y"]});
        } catch (...) {}
        levels_.resize(8);
        try {
            json lj = json::parse(assets_.read("data/levels_2_to_7.json"));
            for (auto& [k, v] : lj["levels"].items()) {
                Level L; L.w = v["width"]; L.h = v["height"];
                for (auto& row : v["cells"]) for (auto& c : row) L.cells.push_back(c);
                int n = std::stoi(k);
                if (n >= 2 && n <= 7) levels_[n] = std::move(L);
            }
        } catch (...) {}
        try {
            json mm = json::parse(assets_.read("data/messages.json"));
            auto& arr = mm["messages"];
            if (arr.is_array()) { for (auto& t : arr) texts_.push_back(t.is_string() ? t.get<std::string>() : t.dump()); }
            else for (auto& [k, t] : arr.items()) texts_.push_back(t.is_string() ? t.get<std::string>() : t.dump());
        } catch (...) {}
        try {
            json r = json::parse(assets_.read("data/rules.json"));
            if (r.contains("wilderness")) {
                wild_count_ = r["wilderness"].value("creatures", wild_count_);
                wild_sight_ = r["wilderness"].value("sight", wild_sight_);
                wild_speed_ = r["wilderness"].value("speed", wild_speed_);
            }
            if (r.contains("combat")) attack_period_ = r["combat"].value("attack_period", attack_period_);
        } catch (...) {}
    }
    sound_.load(assets_);  // optional: the game runs without sound data
    if (!world_.load(assets_)) { fprintf(stderr, "world data failed to load\n"); return false; }
    if (!strip_.load_tables(assets_)) { fprintf(stderr, "data/strip_tables.json failed to load\n"); return false; }
    world_.blocked_mask = rules_.blocked_mask;
    world_.blocked_value = rules_.blocked_value;
    title_ = assets_.picture("title", &title_pal_);
    panel_ = assets_.picture("pannel");
    wtiles_ = assets_.tiles("wtiles");
    qtiles_ = assets_.tiles("qtiles");   // the scroll's paper, rollers, knobs and banner are tiles 0-23 of qtiles
    try {  // item rules: who can use what (shared pack, updated controls)
        json j = json::parse(assets_.read("data/item_stats.json"));
        for (auto it = j["items"].begin(); it != j["items"].end(); ++it) {
            ItemStat s; auto& o = it.value();
            s.slot = o.value("slot", ""); s.dice = o.value("dice", 1); s.sides = o.value("sides", 8); s.bonus = o.value("bonus", 0); s.shots = o.value("shots", 0);
            if (o.contains("heroes")) s.heroes = o["heroes"].get<std::vector<std::string>>();
            istats_[atoi(it.key().c_str())] = s;
        }
    } catch (...) {}
    {   // a treasure chest, closed and open (drawn here to go with the art: wood, gold bands, a lock; black is palette 4 because 0 is transparent)
        static const char* const closed[] = {"................", "..OOOOOOOOOOOO..", ".OWWWWWWWWWWWWO.", ".OWWWWWWWWWWWWO.", ".OGGGGGGGGGGGGO.", ".OWWWWGGGGWWWWO.", ".OWWWWGKKGWWWWO.", ".OWWWWWGGWWWWWO.", ".OWWWWWWWWWWWWO.", ".OSSSSSSSSSSSSO.", ".OGGGGGGGGGGGGO.", "..OOOOOOOOOOOO.."};
        static const char* const opened[] = {"..OOOOOOOOOOOO..", ".OSSSSSSSSSSSSO.", ".OSWWWWWWWWWWSO.", ".OSWWWWWWWWWWSO.", ".OGGGGGGGGGGGGO.", "..OOOOOOOOOOOO..", ".OGGGGGGGGGGGGO.", ".OGKGGGGGKGGGGO.", ".OWWWWWWWWWWWWO.", ".OWWWWWWWWWWWWO.", ".OWWWWGGGGWWWWO.", ".OSSSSSSSSSSSSO.", ".OGGGGGGGGGGGGO.", "..OOOOOOOOOOOO.."};
        auto make = [](const char* const* rows, int n) {
            Image im; im.w = 16; im.h = n; im.px.assign((size_t)16 * n, 0);
            for (int y = 0; y < n; y++) for (int x = 0; x < 16; x++) {
                char c = rows[y][x];
                im.px[(size_t)y * 16 + x] = c == 'O' ? 4 : c == 'W' ? 12 : c == 'S' ? 5 : c == 'G' ? 15 : c == 'K' ? 3 : 0;
            }
            return im;
        };
        chest_closed_ = make(closed, 12); chest_open_ = make(opened, 14);
    }
    {   // the original's projectile sprites: arrow, arrow +2, spear, axe (eight spinning frames), sling stone
        const int rec[5] = {10, 12, 8, 6, 9};
        missile_img_.resize(5);
        for (int k = 0; k < 5; k++) { missile_img_[k] = assets_.missile(rec[k]); if (k < 3 && missile_img_[k].size() > 1) missile_img_[k].resize(1); if (k == 4 && missile_img_[k].size() > 1) missile_img_[k].resize(1); }
    }
    try {  // frame numbers of the poses, per hero sheet
        json j = json::parse(assets_.read("data/hero_frames.json"));
        auto read = [](const json& o, HeroPoses& p) {
            if (o.contains("jump")) p.jump = o["jump"].get<std::vector<int>>();
            if (o.contains("ranged")) p.ranged = o["ranged"].get<std::vector<int>>();
            if (o.contains("high")) p.high = o["high"].get<std::vector<int>>();
            if (o.contains("low")) p.low = o["low"].get<std::vector<int>>();
            p.crouch = o.value("crouch", p.crouch); p.melee = o.value("melee", p.melee);
        };
        if (j.contains("default")) read(j["default"], poses_default_);
        if (j.contains("sheets")) for (auto it = j["sheets"].begin(); it != j["sheets"].end(); ++it) { HeroPoses p = poses_default_; read(it.value(), p); poses_.push_back({it.key(), p}); }
    } catch (...) {}
    try {  // jumps with the updated controls
        json j = json::parse(assets_.read("data/jumps.json"));
        auto read = [](const json& o, JumpSpec& sp) { sp.distance = o.value("distance", sp.distance); sp.climb = o.value("climb", sp.climb); sp.speed = o.value("speed", sp.speed); sp.height_scale = o.value("height_scale", sp.height_scale); };
        if (j.contains("default")) { read(j["default"], rules_.jump); if (j["default"].contains("profile")) rules_.jump_profile = j["default"]["profile"].get<std::vector<int>>(); }
        if (j.contains("heroes")) for (auto it = j["heroes"].begin(); it != j["heroes"].end(); ++it) { JumpSpec sp = rules_.jump; read(it.value(), sp); rules_.hero_jumps.push_back({it.key(), sp}); }
    } catch (...) {}
    load_settings();
    load_scores();
    if (const char* c = getenv("DOF_CONTROLS")) orig_controls_ = std::string(c) != "updated";  // test hook
    if (const char* c = getenv("DOF_MUSIC")) music_enhanced_ = std::string(c) != "original";   // test hook
    clips_ = assets_.clips();
    rng_ = rules_.seed;
    for (size_t i = 0; i < party_.size(); i++)
        if (party_[i].name == "Tanis") leader_ = (int)i;
    px_ = rules_.start_x;
    py_ = rules_.start_y;
    fix_start();
    spawn_wild();
    if (const char* w = getenv("DOF_WARP")) {  // test cheat: "w,x,y" = world position, "L,cx,cy" = level L (2-7) at cell cx,cy, "item,ID" = where item ID lies
        int a = 0, b = 0, c = 0; char kind[16] = {};
        if (sscanf(w, "%15[a-z],%d,%d", kind, &a, &b) >= 2 && std::string(kind) == "w") { px_ = a; py_ = b; fix_start(); }
        else if (sscanf(w, "%15[a-z],%d", kind, &a) == 2 && std::string(kind) == "item") {
            for (auto& g : items_) if (g.id == a && g.in_chest < 0) {
                if (g.level == 1) { px_ = g.x / 8; py_ = g.y / 8; fix_start(); }
                else { warp_level_ = g.level; warp_cx_ = g.x >> 7; warp_cy_ = g.y >> 7; }
                break;
            }
        } else if (sscanf(w, "%d,%d,%d", &a, &b, &c) == 3) { warp_level_ = a; warp_cx_ = b; warp_cy_ = c; }
    }
    if (getenv("DOF_TEST_SPELLS")) { ward_ = 500; ward_kind_ = 1; bless_ = 300; reveal_ = 200; detect_ = 3000; }   // test hook: the spell timers
    y_exit_any_ = getenv("DOF_Y_ANY") != nullptr;   // test hook: Y leaves any fight (the older tests were written for that)
    if (getenv("DOF_PARTY_ALL")) for (auto& hh : all_heroes_) join_party(hh);   // test hook: everyone in the party
    if (getenv("DOF_GIVE")) inv_.push_back(atoi(getenv("DOF_GIVE")));  // test cheat: an item in the pack
    if (getenv("DOF_TEST_WILD")) { wild_.clear(); Wild w; w.x = px_ + 28; w.y = py_; w.type = pool_[0]; if (const char* tn = getenv("DOF_TEST_TYPE")) for (size_t k = 0; k < mtypes_.size(); k++) if (mtypes_[k].name == tn) w.type = (int)k; wild_.push_back(w); }  // test hook
    screen_.pal = title_pal_;
    quick_title_ = getenv("DOF_QUICK_TITLE") != nullptr;
    quick_talk_ = getenv("DOF_QUICK_TALK") != nullptr;
    if (const char* c = getenv("DOF_BOX_HOLD")) box_hold_ = atoi(c);   // test hook: frames a message must stay up
    title_page_ = quick_title_ ? 1 : 0;
    return !title_.empty() && !wtiles_.empty();
}

// Move the start to the nearest spot where the whole 16x16 body is clear.
void Game::fix_start() {
    auto clear = [&](int x, int y) {
        return !world_.solid_at(x, y) && !world_.solid_at(x + 15, y) && !world_.solid_at(x, y + 15) && !world_.solid_at(x + 15, y + 15);
    };
    for (int r = 0; r < 600; r += 8)
        for (int dy = -r; dy <= r; dy += 8)
            for (int dx = -r; dx <= r; dx += 8) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                if (clear(px_ + dx, py_ + dy)) { px_ += dx; py_ += dy; return; }
            }
}

void Game::play(int id) {
    const bool is_music = id == 15 || id == 13;   // the title tune and the short game-over tune are music, every other sound is an effect
    if (is_music ? !music_on_ : !sfx_on_) return;
    if (plat_) plat_->lock_audio();
    sound_.play(id);
    if (plat_) plat_->unlock_audio();
}

void Game::stop_sound() {
    if (plat_) plat_->lock_audio();
    sound_.stop();
    if (plat_) plat_->unlock_audio();
}

void Game::show_box(int i) {
    if (i < 0 || i >= (int)texts_.size()) return;
    box_.clear();
    for (char c : texts_[i]) box_ += (c == '\r' ? '\n' : c);
}

void Game::draw_box() {
    if (box_.empty()) { box_shown_.clear(); return; }
    if (box_ != box_shown_) { box_shown_ = box_; box_since_ = render_frames_; }
    draw_scroll("", 7);
    std::string t;
    for (char c : box_) t += (c == '\n' ? ' ' : c);
    int lines = wrap_count(t, 22);
    if (lines <= 6) { wrap_text(screen_, 66, 34, t, 22, 6, 10, 0, 9); return; }
    // Too long for the scroll: six lines fit between its rollers, a blinking arrow says there is more, up / down scroll.
    if (box_ != box_seen_) { box_seen_ = box_; box_scroll_ = 0; }
    int maxs = lines - 6;
    box_scroll_ = std::clamp(box_scroll_, 0, maxs);
    wrap_text(screen_, 66, 34, t, 22, 6, 10, box_scroll_, 9);
    bool blink = (steps_ / 3) % 2 == 0;
    auto arrow = [&](int x, int y, bool down) {
        for (int r = 0; r < 4; r++) { int w = down ? 7 - 2 * r : 1 + 2 * r; screen_.fill_rect(x + (7 - w) / 2, y + r, w, 1, 4); }
    };
    if (box_scroll_ < maxs && blink) arrow(247, 88, true);   // more below: push the button or down
    if (box_scroll_ > 0) arrow(247, 33, false);              // more above
}

void Game::enter_world() {
    stop_sound();  // the title music ends when play begins
    mode_ = Mode::World;
    screen_.pal = kGamePalette;
    if (warp_level_ >= 2) { enter_level(warp_level_, warp_cx_, warp_cy_); warp_level_ = 0; }
    if (!intro_shown_) { intro_shown_ = true; if (!getenv("DOF_NO_INTRO") && !turbo_) show_box(36); }  // the opening message of the original
}

void Game::reset_party() {
    staff_ = disks_ = 100; staff_broken_ = false; entrance_known_ = false; detect_ = 0;   // a new game: a whole Staff of Magius and the Disks again
    // restore full health (hp_max from the original's records)
    for (auto& h : party_) h.hp = h.hp_max;
    for (size_t i = 0; i < party_.size(); i++)
        if (party_[i].name == "Tanis") leader_ = (int)i;
    enemies_.clear(); corpses_.clear();
    xp_ = kills_ = 0;
}

// F5 / F9. The save is a small JSON file in user/ (browser storage on the web: the page syncs /user to IndexedDB).
void Game::save_game() {
    using nlohmann::json;
    json j;
    j["version"] = 1;
    j["leader"] = party_[leader_].name;
    j["xp"] = xp_;
    j["entrance_known"] = entrance_known_;
    j["elistan"] = elistan_state_;
    j["pois_done"] = std::vector<std::string>(poi_done_.begin(), poi_done_.end());
    j["kills"] = kills_;
    j["position"] = {px_, py_};
    j["east_west"] = east_west_;
    for (auto& h : party_) j["hp"][h.name] = h.hp;
    bool ok = assets_.write("user/save.json", j.dump(1));
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.feSync) Module.feSync(); });
#endif
    message_ = ok ? "Game saved" : "Could not save";
    message_steps_ = 12;
}

void Game::load_game() {
    using nlohmann::json;
    auto raw = assets_.read("user/save.json");
    if (raw.empty()) { message_ = "No saved game"; message_steps_ = 12; return; }
    try {
        json j = json::parse(raw);
        for (auto& h : party_)
            if (j["hp"].contains(h.name)) h.hp = std::clamp<int>(j["hp"][h.name], 0, h.hp_max);
        for (size_t i = 0; i < party_.size(); i++)
            if (party_[i].name == j["leader"] && party_[i].hp > 0) leader_ = (int)i;
        xp_ = j["xp"]; kills_ = j["kills"]; entrance_known_ = j.value("entrance_known", false);
        elistan_state_ = j.value("elistan", 0);
        poi_done_.clear(); if (j.contains("pois_done")) for (auto& n : j["pois_done"]) poi_done_.insert(n.get<std::string>());
        px_ = j["position"][0]; py_ = j["position"][1];
        east_west_ = j["east_west"];
        fix_start();
        enemies_.clear(); corpses_.clear();
        mode_ = Mode::World;
        message_ = "Game restored";
    } catch (...) {
        message_ = "The saved game is damaged";
    }
    message_steps_ = 12;
}

void Game::set_leader(int i) {
    if (i < 0 || i >= (int)party_.size() || party_[i].hp <= 0) return;
    leader_ = i;
    message_ = party_[i].name + " leads";
    message_steps_ = 12;
}

int Game::roll_hp(const MonsterType& t) {
    int dice = t.hp_dice ? t.hp_dice : rnd(1, 4);  // game.exe 06A0
    int hp = t.hp_bonus;
    for (int i = 0; i < dice; i++) hp += rnd(1, 6);
    return std::max(1, hp);
}

// A creature for an overland encounter: the pool of data/monsters.json (zombies common, trolls rare), and griffons mostly near the fortress (the south end of the map) and in the mountains
// (the western passage), where one in three is a griffon.
void Game::hold_hero(int steps, int kind) {
    hold_ = steps; hold_who_ = leader_; hold_kind_ = kind;
    attacking_ = false; ranged_pose_ = 0;
    message_ = kind == 1 ? "The weapon is stuck! Switch heroes" : "Poisoned! Switch heroes"; message_steps_ = 30;
}

int Game::eff_armor(const Hero& h) const {
    int a = h.armor - (h.shield ? 1 : 0);
    if (h.name.rfind("Raistlin", 0) == 0 && h.hand_name == "Staff of Magius" && !staff_broken_) a -= 3;   // the staff protects at +3 (as long as it is not shattered)
    return std::max(0, a);
}

int Game::overland_type(int x, int y) {
    if (x < 0) { x = px_; y = py_; }
    int grif = -1;
    for (size_t k = 0; k < mtypes_.size(); k++) if (mtypes_[k].name == "Griffon") grif = (int)k;
    bool near_fortress = y > 2300, mountains = x < 420 && y > 1500;
    if (grif >= 0 && (near_fortress || mountains) && rnd(1, 3) == 1) return grif;
    return pool_[rnd(0, (int)pool_.size() - 1)];
}

// Original (fn 6572): 1 to 3 creatures appear around the contact point, +-20 px.
void Game::spawn_encounter_at(int count, const std::vector<int>& slots) {
    enemies_.clear(); corpses_.clear();
    if (!slots.empty()) count = (int)slots.size();
    const int main_side = getenv("DOF_FIXED_SPAWN") ? 1 : (rnd(0, 1) ? 1 : -1);
    for (int i = 0; i < count; i++) {
        Enemy e;
        e.type = overland_type();
        if (!slots.empty())
            for (size_t k = 0; k < mtypes_.size(); k++) if (mtypes_[k].slot == slots[i]) e.type = (int)k;
        if (const char* tn = getenv("DOF_TEST_TYPE")) for (size_t k = 0; k < mtypes_.size(); k++) if (mtypes_[k].name == tn) e.type = (int)k;   // test hook
        e.hp = roll_hp(mtypes_[e.type]);
        if (lair_active_) {   // in the cave they are already there, spread over the room to the right of the party
            int k = i / 2, side = (i % 2 == 0) ? 1 : -1;   // alternately to the right and to the left of the party, spread over the rooms
            e.x = std::clamp(cx_ + side * (150 + k * 85 + rnd(0, 30)), lair_x0_ + 30, lair_x1_ - 120);
            e.y = lair_ground_ - 16; e.left = e.x > cx_;
        } else if (getenv("DOF_FIXED_SPAWN")) {   // test hook: the old placement, all to the right (and no random numbers used for it)
            e.x = std::clamp(cx_ + 200 + i * 60 + rnd(-20, 20), 0, strip_.width_px() - 32);
            for (int tries = 0; water_at(e.x) && tries < 40; tries++) e.x = std::clamp(e.x + 16, 0, strip_.width_px() - 32);
            e.y = ground_y(e.x) - 16; e.left = true;
        } else {
            // they come from either side (a group mostly from one, some from the other: the party may find itself half surrounded), usually well out of reach, now and then a little closer
            int side = i == 0 ? main_side : (rnd(1, 100) <= 30 ? -main_side : main_side);
            int dist = rnd(150, 270) + (i / 2) * rnd(15, 40);
            if (rnd(1, 100) <= 25) dist = rnd(105, 150);
            e.x = std::clamp(cx_ + side * dist + rnd(-20, 20), 0, strip_.width_px() - 32);
            for (int tries = 0; water_at(e.x) && tries < 40; tries++) e.x = std::clamp(e.x + (e.x > cx_ ? -16 : 16), 0, strip_.width_px() - 32);   // never in a river
            e.y = ground_y(e.x) - 16;
            e.left = e.x > cx_;
            e.appear = e.frozen = 9;   // they fade in out of the distance first
        }
        e.cooldown = rnd(0, attack_period_);
        enemies_.push_back(e);
    }
    if (const char* d = getenv("DOF_DROP_AT")) drop_thrown(0, cx_ + atoi(d));   // test hook: an arrow lying this far ahead
}

void Game::spawn_encounter() { spawn_encounter_at(rnd(1, 3)); }

void Game::spawn_wild() {  // the people of the wilderness, at the original's start positions (data/wilderness_entities.json)
    load_items();
    wild_.clear();
    last_bx_ = last_by_ = -999;
    try {
        nlohmann::json j = nlohmann::json::parse(assets_.read("data/wilderness_entities.json"));
        for (auto& e : j["entities"]) {
            int kind = e["kind"];
            if (kind == 1) continue;
            Wild w; w.x = e["x"]; w.y = e["y"]; w.kind = kind; w.npc = true; w.msg = e.value("message", -1); w.joins = true; w.hero = e.value("npc_type", 0) - 20;  // touching joins hero (type - 20): 8 Gilthanas, 10-22 the nomads
            w.type = pool_[0];
            wild_.push_back(w);
        }
        if (quest_flags_ & 8) {  // if the elf princess has not been rescued yet she waits in the mountain passage on the last leg to Sla-Mori's secret entrance
            Wild w; w.x = 560; w.y = 2704; w.kind = 100; w.npc = true; w.joins = true; w.msg = 44; w.who = "Princess Laurana"; w.type = pool_[0];
            wild_.push_back(w);
        }
    } catch (...) {}
}

// Monsters roam where the dragon armies are strong: each time the view scrolls over a 128-px line, every block along the new edge gets
// (army strength >> 5) + 2 monsters (game.exe fn 67A2 / 689C / 68EE).
void Game::spawn_roaming_monsters(int bx, int by) {
    if (bx < 0 || by < 0 || bx >= 20 || by >= 24) return;
    int n = (qm_armies_[by * 20 + bx] >> 5) + 2;
    for (int k = 0; k < n; k++)
        for (int tries = 0; tries < 3; tries++) {
            int x = bx * 128 + rnd(0, 127) / 16 * 16, y = by * 128 + rnd(0, 127) / 16 * 16;
            if (abs(x - px_) < 120 && abs(y - py_) < 100) continue;
            if (!world_.can_move(x, y, 0, 0)) continue;
            Wild w; w.x = x; w.y = y; w.kind = 16; w.npc = false; w.type = overland_type(x, y);
            wild_.push_back(w);
            break;
        }
    int monsters = 0;
    for (auto& w : wild_) if (!w.npc) monsters++;
    while (monsters > 30) {  // drop the farthest
        size_t far = 0; int fd = -1;
        for (size_t i = 0; i < wild_.size(); i++) if (!wild_[i].npc) { int d = abs(wild_[i].x - px_) + abs(wild_[i].y - py_); if (d > fd) { fd = d; far = i; } }
        wild_.erase(wild_.begin() + far); monsters--;
    }
}

void Game::load_items() {
    items_.clear();
    try {
        nlohmann::json n = nlohmann::json::parse(assets_.read("data/item_names.json"));
        item_names_ = n["names"].get<std::vector<std::string>>();
        nlohmann::json j = nlohmann::json::parse(assets_.read("data/items_table.json"));
        auto& rows = j["rows"];
        for (auto& r : rows) {
            GroundItem g; g.id = r["id"]; g.qty = r["qty"]; g.level = r["level"]; g.x = r["x"]; g.y = r["y"]; g.row = r["row"]; g.invisible = r.value("invisible", false);
            if (g.id == 0 && g.level == 0) continue;
            if (g.level > 7) {  // contents of the chest in row `level`
                g.in_chest = g.level;
                const auto& c = rows[g.in_chest];
                g.level = c["level"]; g.x = c["x"]; g.y = c["y"];
            }
            items_.push_back(g);
        }
    } catch (...) {}
}

std::string Game::item_name(int id) const {
    std::string n = id >= 1 && id <= (int)item_names_.size() ? item_names_[id - 1] : "";  // ids are 1-based
    if (!n.empty() && (n[0] == '%' || n[0] == '&' || n[0] == '$')) n = n.substr(1);
    while (!n.empty() && n[0] == ' ') n.erase(0, 1);
    return n.empty() ? "Treasure" : n;
}

// Items within reach of the party (24 px), nearest first; with chests_only only closed chests.
std::vector<int> Game::items_near(bool chests_only) const {
    int px = interior() ? ix_ : px_, py = interior() ? iy_ : py_;
    std::vector<std::pair<int, int>> found;
    for (size_t i = 0; i < items_.size(); i++) {
        const GroundItem& g = items_[i];
        if (g.taken || g.in_chest >= 0 || g.level != level_) continue;
        bool chest = g.id >= 43 && g.id <= 46;
        if (chests_only != chest) continue;
        if (chests_only && g.opened) continue;   // an open chest is not offered again
        if (item_hidden(g)) continue;
        if (!chests_only && g.id >= 56 && g.id <= 59) continue;  // women, children, the south door and hidden treasure are not items to take
        int gx = interior() ? g.x : g.x / 8, gy = interior() ? g.y : g.y / 8;
        if (interior()) {   // only what lies in this passage, not behind the wall
            if (iv_ns_ ? (g.x >> 7) != (ix_ >> 7) : (g.y >> 7) != (iy_ >> 7)) continue;
            int sx = iv_ns_ ? levels_[level_].h * 128 - 1 - g.y : g.x;
            if (sx < istart_ || sx >= iend_) continue;
        }
        if (!interior() && mode_ == Mode::Combat) {   // in the side view reach is measured along the strip (8 strip pixels = 1 map pixel)
            int sx = strip_of_world(gx, gy), perp = east_west_ ? abs(gy - py_) : abs(gx - px_);
            if (abs(sx - cx_) <= 30 && perp <= 40) found.push_back({abs(sx - cx_), (int)i});
            continue;
        }
        int d = std::max(abs(gx - px), abs(gy - py));
        if (d <= 32) found.push_back({d, (int)i});
    }
    std::sort(found.begin(), found.end());
    std::vector<int> out;
    for (auto& f : found) out.push_back(f.second);
    return out;
}

void Game::apply_heal(int amount, bool revive) {
    int best = -1;
    if (revive) { for (size_t i = 0; i < party_.size(); i++) if (party_[i].hp <= 0) { best = (int)i; break; } if (best >= 0) party_[best].hp = std::max(1, party_[best].hp_max / 4); return; }
    int worst = 1 << 30;
    for (size_t i = 0; i < party_.size(); i++)
        if (party_[i].hp > 0 && party_[i].hp < party_[i].hp_max && party_[i].hp_max - party_[i].hp < worst + 0 && (best < 0 || party_[i].hp_max - party_[i].hp > party_[best].hp_max - party_[best].hp)) best = (int)i;
    if (best >= 0) party_[best].hp = std::min(party_[best].hp_max, party_[best].hp + amount);
}

void Game::play_title() { if (!music_enhanced_) play(15); }   // (play() itself says nothing when music is off)

// Which loop should be playing now (empty = none). Menus and the help keep whatever was playing.
std::string Game::desired_music() const {
    if (!music_enhanced_ || !music_on_) return "";
    switch (mode_) {
        case Mode::Title: return "title";
        case Mode::Ending: return "ending";
        case Mode::GameOver: return "";
        case Mode::Menu: case Mode::Help: return music_track_;
        default: break;
    }
    bool foes = false;   // a foe counts for the music only when it is near enough to be seen (the strip holds every creature in the party's row or column, however far)
    int hx = interior() ? istrip_x() : cx_;
    for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - hx) < 380) foes = true;
    if (interior()) return foes ? "dcombat" : "dungeon";
    if (mode_ == Mode::World) return "overland";
    if (foes) return "combat";
    for (auto& h : party_) if (h.name.rfind("Princess Laurana", 0) == 0) return "elven";   // the Elven Hymn (a harp) while Laurana walks with the party
    return "scroll";
}

void Game::update_music() {
    std::string want = desired_music();
    music_track_ = want;
    bool muted = sound_.muted();
#ifdef __EMSCRIPTEN__
    if (muted != music_muted_sent_) { music_muted_sent_ = muted; EM_ASM({ if (Module.feMusicMute) Module.feMusicMute($0); }, muted ? 1 : 0); }
    if (want != music_sent_) { music_sent_ = want; EM_ASM({ if (Module.feMusic) Module.feMusic(UTF8ToString($0)); }, want.c_str()); }
#else
    music_sent_ = want;
    music_muted_sent_ = muted;
#endif
}

void Game::set_music_enhanced(bool on) {
    music_enhanced_ = on;
    if (mode_ == Mode::Title) { stop_sound(); play_title(); }
}

void Game::request_exit() {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.feExit) Module.feExit(); });   // saves the browser storage, then leaves for exit.html
#else
    quit_ = true;
#endif
}

void Game::load_scores() {
    try {
        auto raw = assets_.read("user/scores.json");
        if (raw.empty()) return;
        nlohmann::json j = nlohmann::json::parse(std::string(raw.begin(), raw.end()));
        std::vector<std::pair<std::string, int>> v;
        for (auto& e : j) v.push_back({e[0].get<std::string>(), e[1].get<int>()});
        if (!v.empty()) highscores_ = v;
    } catch (...) {}
}

void Game::save_scores() {
    nlohmann::json j = nlohmann::json::array();
    for (auto& e : highscores_) j.push_back({e.first, e.second});
    assets_.write("user/scores.json", j.dump());
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.feSync) Module.feSync(); });
#endif
}

void Game::load_settings() {
    try {
        auto raw = assets_.read("user/settings.json");
        if (raw.empty()) return;
        nlohmann::json j = nlohmann::json::parse(std::string(raw.begin(), raw.end()));
        orig_controls_ = j.value("original_controls", false);
        god_mode_ = j.value("god_mode", false);
        music_enhanced_ = j.value("music", std::string("enhanced")) != "original";
        music_on_ = j.value("music_on", true); sfx_on_ = j.value("sound_on", true);
        gfx_mode_ = 0;  // 256-colour is not available yet
    } catch (...) {}
}

void Game::save_settings() {
    nlohmann::json j;
    j["original_controls"] = orig_controls_;
    j["god_mode"] = god_mode_;
    j["music"] = music_enhanced_ ? "enhanced" : "original";
    j["music_on"] = music_on_; j["sound_on"] = sfx_on_;
    j["graphics"] = gfx_mode_ == 0 ? "ega16" : "vga256";
    assets_.write("user/settings.json", j.dump(1));
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.feSync) Module.feSync(); });
#endif
}

// The dragon armies advance (fn 538C): every cell inside the map takes (terrain factor)% of the difference to each stronger neighbour; strength 127 is
// a source that never runs down. The result replaces the whole array, so the outermost ring becomes empty.
void Game::army_tick() {
    uint8_t out[480] = {};
    int dirs[4] = {-20, 20, -1, 1};
    for (int r = 1; r <= 22; r++)
        for (int c = 1; c <= 18; c++) {
            int i = r * 20 + c;
            int cur = qm_armies_[i], f = qm_factor_[qm_terrain_[i] & 127];
            int val = cur;
            for (int d : dirs) {
                int nb = qm_armies_[i + d];
                if (cur < nb) {
                    int delta = f * (nb - cur) / 100;
                    val += delta;
                    if (nb != 0x7f) qm_armies_[i + d] = (uint8_t)(qm_armies_[i + d] - delta);
                }
            }
            out[i] = (uint8_t)val;
        }
    memcpy(qm_armies_, out, sizeof out);
    army_ticks_++;
    // the fortress: the map cell at Sla-Mori's entrance (block 4,21); once the armies burn there it has fallen (my reading: the original's check was not found)
    if ((qm_armies_[21 * 20 + 4] & 0x60) && mode_ != Mode::Title && mode_ != Mode::GameOver && mode_ != Mode::Ending) armies_win();
}

int Game::str_bonus(const Hero& h) const {  // AD&D damage bonus from strength (19+ = 18/xx = +3)
    int st = h.abilities.empty() ? 12 : h.abilities[0];
    return st >= 19 ? 3 : st == 18 ? 2 : st == 17 ? 1 : st <= 6 ? -1 : 0;
}

void Game::equip(Hero& h) {
    try {
        nlohmann::json j = nlohmann::json::parse(assets_.read("data/equipment.json"));
        auto& e = j["heroes"];
        std::string key = h.nomad ? "nomad" : "";
        if (key.empty()) for (auto it = e.begin(); it != e.end(); ++it) if (h.name.rfind(it.key(), 0) == 0) key = it.key();
        if (key.empty()) key = "nomad";
        auto& x = e[key];
        h.thac0 = x["thac0"]; h.to_hit = x.value("to_hit", 0);
        h.hand_name = x["hand"][0]; h.hand_dice = x["hand"][1]; h.hand_sides = x["hand"][2]; h.hand_bonus = x["hand"][3];
        h.range_name = x["range"].is_null() ? "" : x["range"][0].get<std::string>();
        if (!x["range"].is_null()) { h.range_dice = x["range"][1]; h.range_sides = x["range"][2]; h.range_bonus = x["range"][3]; }
        h.ammo = x.value("ammo", 0);
    } catch (...) {}
}

bool Game::item_usable_by(const ItemStat& s, const Hero& h) const {
    for (auto& n : s.heroes) { if (n == "nomad" ? h.nomad : h.name.rfind(n, 0) == 0) return true; }
    return false;
}

// Updated controls: the pack is shared. A weapon goes to the hero it improves most (and he puts down his old one, which is offered to the others in turn);
// arrows and bullets go to the archer who has the fewest; a ring goes to the worst-protected hero who has none. Returns true if a hero took it.
bool Game::auto_equip(int id, std::string& msg, int shots) {
    auto first = [](const Hero& h) { return h.name.substr(0, h.name.find(' ')); };
    auto it = istats_.find(id);
    if (it == istats_.end()) return false;
    const ItemStat* st = &it->second;
    int cur = id; msg.clear();
    auto avg = [](int dice, int sides, int bonus) { return dice * (sides + 1) / 2.0 + bonus; };
    auto id_of = [&](const std::string& name) { for (size_t i = 0; i < item_names_.size(); i++) if (item_names_[i] == name) return (int)i + 1; return 0; };
    for (int round = 0; round < 4 && st; round++) {   // an item put down is offered to the others
        bool hand = st->slot == "hand", range = st->slot == "range";
        int best = -1; double gain = 0;
        if (hand || range) {
            if (cur == 19) {   // Wyrmslayer goes to Caramon if he is with the party, otherwise to Sturm (whatever he holds now)
                for (const char* nm : {"Caramon", "Sturm"}) {
                    for (size_t i = 0; i < party_.size() && best < 0; i++) if (party_[i].hp > 0 && party_[i].name.rfind(nm, 0) == 0) best = (int)i;
                    if (best >= 0) break;
                }
                gain = 1;
            }
            for (size_t i = 0; i < party_.size() && cur != 19; i++) {
                const Hero& h = party_[i];
                if (h.hp <= 0 || !item_usable_by(*st, h)) continue;
                double now = hand ? (h.hand_name.empty() ? 0 : avg(h.hand_dice, h.hand_sides, h.hand_bonus)) : (h.range_name.empty() ? 0 : avg(h.range_dice, h.range_sides, h.range_bonus));
                double g = avg(st->dice, st->sides, st->bonus) - now;
                if (g > gain + 1e-9) { gain = g; best = (int)i; }
            }
            if (best < 0) break;
            Hero& h = party_[best];
            std::string old_name = hand ? h.hand_name : h.range_name;
            std::string nm = item_name(cur);
            if (hand) { h.hand_name = nm; h.hand_dice = st->dice; h.hand_sides = st->sides; h.hand_bonus = st->bonus; }
            else { h.range_name = nm; h.range_dice = st->dice; h.range_sides = st->sides; h.range_bonus = st->bonus; }
            msg += (msg.empty() ? "" : " ") + first(h) + " equipped the " + nm + ".";
            int old_id = id_of(old_name);   // what he puts down (a weapon only he can use, like his own staff, is simply kept in mind and dropped)
            auto oi = old_id ? istats_.find(old_id) : istats_.end();
            if (oi == istats_.end() || old_id == 2) return true;
            cur = old_id; st = &oi->second;
            continue;
        }
        if (st->slot == "ammo") {
            for (size_t i = 0; i < party_.size(); i++) {
                const Hero& h = party_[i];
                if (h.hp <= 0 || !item_usable_by(*st, h)) continue;
                if (best < 0 || h.ammo < party_[best].ammo) best = (int)i;
            }
            if (best < 0) break;
            Hero& h = party_[best];
            h.ammo += shots >= 0 ? shots : st->shots; h.ammo_bonus = std::max(h.ammo_bonus, st->bonus);
            msg += (msg.empty() ? "" : " ") + first(h) + " took the " + item_name(cur) + " (" + std::to_string(h.ammo) + " left).";
            return true;
        }
        if (st->slot == "shield") {   // a shield: the fighters who can carry it, the worse protected first
            for (size_t i = 0; i < party_.size(); i++) {
                const Hero& h = party_[i];
                if (h.hp <= 0 || h.shield || !item_usable_by(*st, h)) continue;
                if (best < 0 || h.armor > party_[best].armor) best = (int)i;
            }
            if (best < 0) break;
            Hero& h = party_[best];
            h.shield = true;
            msg += (msg.empty() ? "" : " ") + first(h) + " took up the " + item_name(cur) + ".";
            return true;
        }
        if (st->slot == "armor") {   // plate armor: armor class 3, for Caramon or Sturm, whichever is worse protected
            for (size_t i = 0; i < party_.size(); i++) {
                const Hero& h = party_[i];
                if (h.hp <= 0 || h.plate || !item_usable_by(*st, h)) continue;
                if (best < 0 || h.armor > party_[best].armor) best = (int)i;
            }
            if (best < 0) break;
            Hero& h = party_[best];
            h.plate = true; h.armor = std::max(0, std::min(h.armor + h.ring_bonus, st->bonus) - h.ring_bonus);
            msg += (msg.empty() ? "" : " ") + first(h) + " put on the " + item_name(cur) + ".";
            return true;
        }
        if (st->slot == "ring") {
            for (size_t i = 0; i < party_.size(); i++) {
                const Hero& h = party_[i];
                if (h.hp <= 0 || h.ring) continue;
                if (best < 0 || h.armor > party_[best].armor) best = (int)i;   // the worst protected (the highest armor class)
            }
            if (best < 0) break;
            Hero& h = party_[best];
            h.ring = true; h.ring_bonus = st->bonus; h.armor = std::max(0, h.armor - st->bonus);   // a lower armor class is better
            msg += (msg.empty() ? "" : " ") + first(h) + " put on the " + item_name(cur) + ".";
            return true;
        }
        break;
    }
    // something was handed on during the rounds (msg set) but the last put-down item found no taker: it goes in the pack
    if (!msg.empty() && cur != id) { if (inv_.size() < kPackMax) inv_.push_back(cur); return true; }
    return !msg.empty();
}

// A person joins the party (fn 60D0): into a free slot (ten at most); if the party is full a nomad is replaced, otherwise he stays behind.
void Game::join_party(const Hero& h) {
    for (auto& m : party_) if (m.name == h.name) return;
    if (party_.size() < 10) { party_.push_back(h); return; }
    for (auto& m : party_) if (m.nomad) { m = h; return; }
}

// The dragon armies took Pax Tharkas before the party got there ("You failed in your quest", message 59): the game ends.
void Game::armies_win() {
    box_.clear();
    mode_ = Mode::GameOver; screen_.pal = kGamePalette; play(13);
}

void Game::join_by_index(int idx) {
    if (idx == 8) { for (auto& h : all_heroes_) if (h.name.rfind("Gilthanas", 0) == 0) join_party(h); }
    else if (idx >= 10 && idx < (int)all_heroes_.size()) join_party(all_heroes_[idx]);  // all_heroes_[10..] are the nomads
}

void Game::join_by_name(const std::string& name) {
    for (auto& h : all_heroes_) if (h.name.rfind(name, 0) == 0) join_party(h);
}

// The hero or nomad the waiting person is, for drawing.
const Hero* Game::talker_hero() const {
    if (!talk_.who.empty()) { for (auto& hh : all_heroes_) if (hh.name.rfind(talk_.who, 0) == 0) return &hh; return nullptr; }
    if (talk_.hero == 8) { for (auto& hh : all_heroes_) if (hh.name.rfind("Gilthanas", 0) == 0) return &hh; }
    else if (talk_.hero >= 10 && talk_.hero < (int)all_heroes_.size()) return &all_heroes_[talk_.hero];
    return nullptr;
}

void Game::start_talk(size_t wi) {
    Wild& w = wild_[wi];
    talk_ = Talker{}; talk_.active = true; talk_.msg = w.msg; talk_.hero = w.hero; talk_.who = w.who; talk_.wild_x = w.x; talk_.wild_y = w.y;
    strip_.build(world_, px_, py_, east_west_);
    cx_ = strip_.start_x(px_, py_, east_west_);
    cy_ = ground_y(cx_) - 16;
    enemies_.clear(); corpses_.clear();
    talk_.x = std::min(cx_ + 190, strip_.width_px() - 40);
    face_left_ = false;
    from_wild_ = false;
    mode_ = Mode::Combat;
    message_ = "Walk ahead"; message_steps_ = 16;
}

// In the side view with a person waiting: they dither in once the party is near, speak when it reaches them and join; the view then returns to the map.
void Game::step_talk() {
    if (!talk_.active) return;
    if (!box_.empty()) return;
    if (talk_.talked) {  // the message has been read
        talk_.active = false;
        for (size_t i = 0; i < wild_.size(); i++) if (wild_[i].npc && wild_[i].x == talk_.wild_x && wild_[i].y == talk_.wild_y) { wild_.erase(wild_.begin() + i); break; }
        from_wild_ = false; toggle_view();
        return;
    }
    int d = abs(talk_.x - cx_);
    if (d < 150 && talk_.appear < 8) talk_.appear += turbo_ ? 8 : 1;
    if (talk_.appear >= 8 && d <= 30) {
        show_box(talk_.msg);
        if (!talk_.who.empty()) {
            join_by_name(talk_.who);
            if (talk_.who == "Princess Laurana" && (quest_flags_ & 8)) { quest_flags_ &= ~8; party_[leader_].xp += 10000; xp_ += 10000; }   // her rescue counts as that step of the quest
        } else join_by_index(talk_.hero);
        talk_.talked = true;
    }
}

void Game::step_wild() {
    {   // the front of Pax Tharkas (the gatehouse door at about world (575, 2890)) is shut: the way in is Sla-Mori's secret entrance in the mountains to the north (message 43)
        bool at_gate = abs(px_ - 590) < 96 && py_ >= 2820 && py_ <= 2960;
        if (at_gate && !gate_told_) { gate_told_ = true; box_ = "You cannot gain entrance in this way. The fortress is far too well guarded. There must be another way..."; }
        if (!at_gate && (abs(px_ - 590) > 144 || py_ < 2760)) gate_told_ = false;
        // the dead end where the way in looks as if it should be: nothing here, but the party gets the idea where to look
        bool at_dead = abs(px_ - 480) < 24 && abs(py_ - 2880) < 24;
        if (at_dead && !dead_told_) { dead_told_ = true; entrance_known_ = true; box_ = "You search high and low, but there is certainly no entrance here. You have a feeling it lies further to the northeast. (The way is marked on your map.)"; }
        if (!at_dead && (abs(px_ - 480) > 80 || abs(py_ - 2880) > 80)) dead_told_ = false;
        for (auto& m : party_) if (m.name.rfind("Princess Laurana", 0) == 0) entrance_known_ = true;   // Laurana knows the way and marks it on the map
    }
    {  // the view crossed a 128-px line: new monsters appear along the edge it uncovered
        int vx = std::max(0, px_ - 160), vy = std::max(0, py_ - 64);
        int bx = (vx + 160) >> 7, by = (vy + 64) >> 7;
        if (last_bx_ != -999 && (bx != last_bx_ || by != last_by_)) {
            if (bx > last_bx_) for (int r = by - 1; r <= by + 1; r++) spawn_roaming_monsters(bx + 2, r);
            if (bx < last_bx_) for (int r = by - 1; r <= by + 1; r++) spawn_roaming_monsters(bx - 2, r);
            if (by > last_by_) for (int c = bx - 2; c <= bx + 2; c++) spawn_roaming_monsters(c, by + 1);
            if (by < last_by_) for (int c = bx - 2; c <= bx + 2; c++) spawn_roaming_monsters(c, by - 1);
        }
        last_bx_ = bx; last_by_ = by;
    }
    for (size_t i = 0; i < wild_.size(); i++) {
        Wild& w = wild_[i];
        int dx = px_ - w.x, dy = py_ - w.y;
        if (abs(dx) < 16 && abs(dy) < 16 && !(no_encounters_ && !w.npc)) {  // contact (game.exe 342B: both distances under 16); (the debug menu can switch the random monsters off)
            if (w.npc) {  // a person: a word of advice, or he joins
                if (w.who == "Princess Laurana" && !(quest_flags_ & 8)) { wild_.erase(wild_.begin() + i); return; }   // already rescued in Sla-Mori
                if (!w.seen && w.msg >= 0) {
                    w.seen = true;
                    if (w.joins && !quick_talk_) { start_talk(i); return; }   // the original switches to the side view and the person appears as you walk
                    show_box(w.msg);
                    if (w.joins) { if (!w.who.empty()) join_by_name(w.who); else join_by_index(w.hero); wild_.erase(wild_.begin() + i); }
                    return;
                }
                continue;
            }
            cx_ = 0;
            strip_.build(world_, px_, py_, east_west_);
            cx_ = strip_.start_x(px_, py_, east_west_);
            cy_ = ground_y(cx_) - 16;
            spawn_encounter_at(rnd(1, 3));
            wild_.erase(wild_.begin() + i);
            from_wild_ = true;
            mode_ = Mode::Combat;
            message_ = "Enemies!"; message_steps_ = 12;
            return;
        }
        if (w.npc && w.seen && (abs(dx) > 48 || abs(dy) > 48)) w.seen = false;  // may speak again later
        if (!w.npc && abs(dx) < wild_sight_ && abs(dy) < wild_sight_) { w.dx = dx > 0 ? 1 : (dx < 0 ? -1 : 0); w.dy = dy > 0 ? 1 : (dy < 0 ? -1 : 0); }
        else if (--w.timer <= 0) { w.dx = rnd(-1, 1); w.dy = rnd(-1, 1); w.timer = rnd(10, 40); if (w.npc && w.kind == 100) w.dx = w.dy = 0; }
        for (int s = 0; s < (w.npc ? 1 : wild_speed_); s++) {
            if (w.dx && world_.can_move(w.x, w.y, w.dx, 0)) w.x += w.dx;
            if (w.dy && world_.can_move(w.x, w.y, 0, w.dy)) w.y += w.dy;
        }
        w.anim++;
    }
}

// Interiors. Collision is an estimate: empty cells (0) are void; everything else can be walked on.
int Game::level_tile(int x, int y) const {
    const Level& L = levels_[level_];
    int cx = x >> 7, cy = y >> 7;
    if (cx < 0 || cy < 0 || cx >= L.w || cy >= L.h) return -1;
    int cell = L.cells[cy * L.w + cx];
    if (cell == 0 || cell >= (int)utokens_.size()) return -1;
    return utokens_[cell][((y & 0x7f) >> 4) * 8 + ((x & 0x7f) >> 4)];
}

bool Game::level_can_move(int x, int y, int dx, int dy) const {
    int nx = x + dx, ny = y + dy;
    for (int ox : {0, 15}) for (int oy : {0, 15})
        { int t = level_tile(nx + ox, ny + oy); if (t < 0 || world_.tile_solid(t)) return false; }  // the same attribute table as outdoors (0x1b3e)
    return true;
}

void Game::set_istrip_x(int sx) {
    if (iv_ns_) iy_ = (int)levels_[level_].h * 128 - 1 - sx; else ix_ = sx;
}

// Build the 4-row strip along the party's row (east/west) or column (north/south), as fn 54CC does: row 2 gets the block of every cell
// reachable in a straight line through open sides, row 1 the top of doors, row 3 the front strip (block 0x38) with openings where a
// cell has a passage crossing it. The extent of the line limits how far the party can walk.
void Game::build_istrip() {
    const Level& L = levels_[level_];
    int cx = ix_ >> 7, cy = iy_ >> 7;
    long key = ((long)level_ * 1000 + cx) * 1000 + cy + (iv_ns_ ? 1000000000L : 0);
    if (key == istrip_key_) return;
    istrip_key_ = key;
    int N = iv_ns_ ? L.h : L.w;
    istrip_len_ = std::min(N, 256);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 256; c++) istrip_[r][c] = r == 3 ? 0x38 : 0;
    auto cell = [&](int x, int y) { return (x < 0 || y < 0 || x >= L.w || y >= L.h) ? 0 : L.cells[y * L.w + x]; };
    auto ex = [&](int v) { return iexits_[std::min(v, (int)iexits_.size() - 1)]; };
    auto blk = [&](const std::vector<int>& t, int v) { return t[std::min(v, (int)t.size() - 1)]; };
    int lo, hi;
    if (iv_ns_) {
        int col0 = L.h - cy - 1;
        int c = col0, y = cy;
        for (;; ) {  // north: columns grow
            int v = cell(cx, y); int b = blk(itab_ns_, v);
            istrip_[2][c] = b;
            if (b == 0x39) istrip_[1][c] = 0x3b;
            if (v >= 0x55 && v <= 0x57) istrip_[3][c] = v - 0x0e;
            c++;
            if (!(ex(v) & 1) || y <= 0) break;
            y--;
        }
        hi = c;
        c = col0; y = cy;
        for (;; ) {  // south
            int v = cell(cx, y); int b = blk(itab_ns_, v);
            istrip_[2][c] = b;
            if (b == 0x39) istrip_[1][c] = 0x3b;
            if (v >= 0x55 && v <= 0x57) istrip_[3][c] = v - 0x0e;
            if (!(ex(v) & 4) || y >= L.h - 1 || c <= 0) break;
            c--; y++;
        }
        lo = c;
    } else {
        int c = cx;
        for (;; ) {  // east
            int v = cell(c, cy); int b = blk(itab_ew_, v);
            istrip_[2][c] = b;
            if (b == 0x39) istrip_[1][c] = 0x3b;
            if (b == 0x3c) istrip_[3][c] = 0x3b;
            c++;
            if (!(ex(v) & 2) || c >= L.w) break;
        }
        hi = c;
        c = cx;
        for (;; ) {  // west
            int v = cell(c, cy); int b = blk(itab_ew_, v);
            istrip_[2][c] = b;
            if (b == 0x39) istrip_[1][c] = 0x3b;
            if (b == 0x3c) istrip_[3][c] = 0x3b;
            if (!(ex(v) & 8) || c <= 0) break;
            c--;
        }
        lo = c;
    }
    istart_ = lo * 128;
    iend_ = hi * 128;
}

// The dungeon as the original shows it: a side view of the strip, hero on the floor, front strip below, black behind.
size_t Game::hero_frame(const Hero& h) {
    size_t f = rules_.walk_frames.empty() ? 0 : (size_t)rules_.walk_frames[anim_ % rules_.walk_frames.size()];
    if (anim_ == 0 || f >= h.actor.frames.size()) f = 0;
    {   // the original's own poses: jumping (rising, then falling), attacking straight at a foe in reach, at range, high and low (frame numbers per sheet: data/hero_frames.json)
        const HeroPoses& hp = poses_for(h.sprite);
        size_t nf = h.actor.frames.size();
        auto pick = [&](const std::vector<int>& v, size_t k, int fallback) { return v.empty() ? (size_t)fallback : (size_t)v[k % v.size()]; };
        size_t k = (size_t)(render_frames_ / 10);
        if (held()) f = hold_kind_ == 1 ? (size_t)hp.melee : (size_t)hp.crouch;   // stuck in the swing; poisoned: crouching
        else if (jump_t_ >= 0) f = (size_t)(hp.jump.size() > 1 && jump_p_ >= 0.36 ? hp.jump[1] : hp.jump.empty() ? 0 : hp.jump[0]);
        else if (ranged_pose_ > 0 && !orig_controls_) f = pick(hp.ranged, k, hp.melee);
        else if (attacking_) {
            if (aim_ > 0) f = pick(hp.high, k, hp.melee);
            else if (aim_ < 0) f = pick(hp.low, k, hp.melee);
            else f = foe_in_reach(false) ? (size_t)hp.melee : pick(hp.ranged, k, hp.melee);
        }
        if (f >= nf) f = 0;
    }
    return f;
}

// A small top-down map in the corner of the dungeon: it scrolls with the party. Cells that can be seen into along a passage are grey, cells the party has been in are blue
// with white lines where the walls are, and an arrow shows where the party is and which way it faces.
void Game::draw_minimap() {
    const Level& L = levels_[level_];
    if (L.w <= 0 || L.h <= 0) return;
    auto& st = dmap_[level_];
    if ((int)st.size() != L.w * L.h) st.assign((size_t)L.w * L.h, 0);
    auto exists = [&](int x, int y) { return x >= 0 && y >= 0 && x < L.w && y < L.h && L.cells[y * L.w + x] != 0; };
    auto exits = [&](int x, int y) { return exists(x, y) ? iexits_[std::min(L.cells[y * L.w + x], (int)iexits_.size() - 1)] : 0; };
    int cx = ix_ >> 7, cy = iy_ >> 7;
    static const int dx4[4] = {0, 1, 0, -1}, dy4[4] = {-1, 0, 1, 0}, bit4[4] = {1, 2, 4, 8};   // north, east, south, west
    if (exists(cx, cy)) {
        st[cy * L.w + cx] = 2;
        for (int dir = 0; dir < 4; dir++) {
            bool along = iv_ns_ ? (dir == 0 || dir == 2) : (dir == 1 || dir == 3);   // along the strip we look far, to the sides only one cell
            int x = cx, y = cy;
            for (int n = 0; n < (along ? 10 : 1); n++) {
                if (!(exits(x, y) & bit4[dir])) break;
                x += dx4[dir]; y += dy4[dir];
                if (!exists(x, y)) break;
                if (st[y * L.w + x] == 0) st[y * L.w + x] = 1;
            }
        }
    }
    const int cell = 6, cols = 9, rows = 5, bx = kW - cols * cell - 8, by = 3;
    screen_.fill_rect(bx - 3, by - 2, cols * cell + 6, rows * cell + 4, 0);
    screen_.fill_rect(bx - 3, by - 2, cols * cell + 6, 1, 7); screen_.fill_rect(bx - 3, by + rows * cell + 1, cols * cell + 6, 1, 7);
    screen_.fill_rect(bx - 3, by - 2, 1, rows * cell + 4, 7); screen_.fill_rect(bx + cols * cell + 2, by - 2, 1, rows * cell + 4, 7);
    int ox = cx - cols / 2, oy = cy - rows / 2;
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
            int x = ox + i, y = oy + j;
            if (!exists(x, y)) continue;
            int s = st[y * L.w + x];
            if (!s) continue;
            int sx = bx + i * cell, sy = by + j * cell;
            screen_.fill_rect(sx, sy, cell, cell, s == 2 ? 10 : 8);   // blue once explored, grey when only seen into
            if (s == 2) {   // white lines on the walls
                int e = exits(x, y);
                if (!(e & 1)) screen_.fill_rect(sx, sy, cell, 1, 3);
                if (!(e & 4)) screen_.fill_rect(sx, sy + cell - 1, cell, 1, 3);
                if (!(e & 8)) screen_.fill_rect(sx, sy, 1, cell, 3);
                if (!(e & 2)) screen_.fill_rect(sx + cell - 1, sy, 1, cell, 3);
            }
        }
    // the party: an arrow in the direction it faces (right along a north-south strip is north)
    int dir = iv_ns_ ? (face_left_ ? 2 : 0) : (face_left_ ? 3 : 1);
    int pxm = bx + (cols / 2) * cell + (ix_ & 127) * cell / 128, pym = by + (rows / 2) * cell + (iy_ & 127) * cell / 128;
    static const char* arrow[4][5] = {{"..X..", ".XXX.", "XXXXX", "..X..", "..X.."}, {"X....", "XXX..", "XXXXX", "XXX..", "X...."}, {"..X..", "..X..", "XXXXX", ".XXX.", "..X.."}, {"....X", "..XXX", "XXXXX", "..XXX", "....X"}};
    for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++)
        if (arrow[dir][r][c] == 'X') { int x = pxm - 2 + c, y = pym - 2 + r; if (x >= 0 && x < kW && y >= 0 && y < 128) screen_.px[y * kW + x] = (render_frames_ / 15) % 2 ? 15 : 3; }
}

void Game::draw_interior() {
    screen_.reset_clip();
    screen_.clear(0);
    const int vh = 128;
    screen_.set_clip(0, 0, kW, vh);
    build_istrip();
    int sx = istrip_x();
    int cx = std::max(0, sx - 160), cy = 256;  // fn 4DDA: 160 px left of the party; the view starts at the top of row 2 (hero y 0x170 - 0x70)
    for (int row = 2; row <= 2; row++)
        for (int col = cx >> 7; col <= (cx + kW - 1) >> 7 && col < istrip_len_; col++) {
            int b = istrip_[row][col];
            if (b <= 0 || b >= (int)utokens_.size()) continue;
            for (int i = 0; i < 64; i++) {
                int t = utokens_[b][i];
                if (t < 1 || t > (int)utiles_.size()) continue;
                int x = col * 128 + (i % 8) * 16 - cx, y = row * 128 + (i / 8) * 16 - cy;
                if (x > -16 && x < kW && y > -16 && y < vh) screen_.blit(utiles_[t - 1], x, y);
            }
        }
    const Level& L = levels_[level_];
    for (auto& it : items_) {  // items on this line of cells, as small squares on the floor
        if (it.taken || (it.id >= 56 && it.id <= 59) || item_hidden(it) || it.in_chest >= 0 || it.level != level_) continue;
        int icx = it.x >> 7, icy = it.y >> 7;
        if (iv_ns_ ? icx != (ix_ >> 7) : icy != (iy_ >> 7)) continue;
        int isx = iv_ns_ ? L.h * 128 - 1 - it.y : it.x;
        if (isx < istart_ || isx >= iend_) continue;   // beyond the end of this passage: behind a wall
        bool chest = it.id >= 43 && it.id <= 46;
        // treasure lying about is drawn as a small chest too (a chest that opens, or an item taken with A)
        { const Image& ci = (chest && it.opened) ? chest_open_ : chest_closed_; screen_.blit(ci, isx - cx - 8, 112 - ci.h, true); }
    }
    for (auto& sl : slabs_) if (sl.landed && sl.level == level_) draw_slab((int)sl.sx - cx + 8, 112, 1.0);   // a fallen slab lies under the party until it leaves the cell
    if (reveal_ > 0)
        for (auto& it : items_) {  // revealed traps shimmer on the floor: a column of light, rings of glitter and orbiting sparks, impossible to miss
            if (it.taken || it.id != 59 || it.in_chest >= 0 || it.level != level_) continue;
            if (iv_ns_ ? (it.x >> 7) != (ix_ >> 7) : (it.y >> 7) != (iy_ >> 7)) continue;
            int isx = iv_ns_ ? L.h * 128 - 1 - it.y : it.x, x = isx - cx, y = 112;
            int f = render_frames_;
            for (int yy = y - 76; yy < y; yy++) {   // the light column, wider and brighter near the floor
                int w = 4 + (y - yy) / 7;
                if ((yy + f / 2) % 2 == 0) screen_.fill_rect(x - w, yy, w * 2 + 1, 1, (yy + f) % 6 < 3 ? 15 : 3);
            }
            for (int r = 0; r < 3; r++) {   // pulsing rings on the floor
                int rr = 8 + ((f / 2 + r * 8) % 24);
                for (int k = 0; k < 24; k++) {
                    double a = k * 0.2618;
                    int px2 = x + (int)std::lround(std::cos(a) * rr * 1.6), py2 = y - 3 + (int)std::lround(std::sin(a) * rr * 0.4);
                    screen_.fill_rect(px2, py2, 2, 1, (k + r) % 2 ? 15 : 14);
                }
            }
            for (int k = 0; k < 8; k++) {   // sparks circling up the column, each with a glint
                double a = f * 0.09 + k * 0.785;
                int px2 = x + (int)std::lround(std::cos(a) * 22), py2 = y - 6 - ((f + k * 11) % 70);
                screen_.fill_rect(px2 - 3, py2, 7, 1, 3); screen_.fill_rect(px2, py2 - 3, 1, 7, 3);
                screen_.fill_rect(px2, py2, 1, 1, 15);
            }
        }
    for (auto& c : corpses_) {
        const MonsterType& t = mtypes_[c.e.type];
        int dl; Image im = corpse_image(c, dl);
        if (dl > 0) screen_.blit_dither(im, c.e.x - cx - 8 - (im.w - 32) / 2, 112 - t.actor.height, c.e.left, dl);
    }
    for (auto& e : enemies_) {
        const MonsterType& t = mtypes_[e.type];
        const auto& seq = e.striking ? t.attack : t.walk;
        size_t fr = seq.empty() ? 0 : (size_t)seq[(e.striking ? e.anim / 4 : e.anim) % seq.size()];
        if (fr >= t.actor.frames.size()) fr = 0;
        if (t.name == "Dragon") draw_dragon_neck(e, cx);
        screen_.blit(t.actor.frames[fr], e.x - cx - 8 - (t.actor.frames[fr].w - 32) / 2, 112 - t.actor.height, true, e.left);
    }
    for (auto& p : npcs_) {  // Laurana, waiting to be rescued
        if (p.done) continue;
        if (iv_ns_ ? (p.lx >> 7) != (ix_ >> 7) : (p.ly >> 7) != (iy_ >> 7)) continue;
        int psx = iv_ns_ ? L.h * 128 - 1 - p.ly : p.lx;
        if (p.kind == 1) { const Image& pi = prisoner_image(); screen_.blit(pi, psx - cx - 8, 112 - pi.h, true, false); continue; }
        if (p.kind == 2) { const Image& ei = elistan_image(); screen_.blit(ei, psx - cx - 8, 112 - ei.h, true, false); continue; }   // Elistan stays where he is, done or not
        for (auto& hh : all_heroes_) if (hh.name.rfind("Princess Laurana", 0) == 0 && !hh.actor.frames.empty()) screen_.blit(hh.actor.frames[0], psx - cx - 8, 112 - hh.actor.height, true, false);
    }
    const Hero& h = party_[leader_];
    size_t f = hero_frame(h);
    screen_.blit(h.actor.frames[f], sx - cx - 8, 112 - h.actor.height - jump_h_, true, face_left_);
    draw_ground_things(cx, true, 0);
    draw_missiles(cx, true, 0);
    draw_floaters(cx, true, 0);
    if (lair_active_) draw_lair_minimap(); else draw_minimap();
    draw_spell_timers();
    if (message_steps_ > 0) screen_.text(4, 4, message_.c_str(), 15);
    draw_panel();
}

// In the outdoor side view the party always stands on the ground: the first tile (scanning down in 16 px steps) whose attribute has bit 0 set,
// except attribute 5 (game.exe fn 672A). There is no free vertical walking.
int Game::ground_y(int x) const {
    if (lair_active_) return lair_ground_;   // the cave floor is flat
    for (int y = 0; y < 512; y += 16) {
        int t = strip_.tile_at(world_, x + 8, y);
        int a = world_.attr_of_tile(t);
        if (t > 0 && (a & 1) && a != 5) return y;
    }
    return 512;
}

// Creatures placed on entering a level (data/level_creatures.json = game.exe 0x2BB4): [type, cell x, cell y, level]; types 2-19 are monsters, 29 is Laurana.
void Game::spawn_level_creatures(int n) {
    ienemies_.clear(); npcs_.clear();
    try {
        nlohmann::json j = nlohmann::json::parse(assets_.read("data/level_creatures.json"));
        for (auto& r : j["records"]) {
            int type = r[0], cx = r[1], cy = r[2], lvl = r[3];
            if (lvl != n) continue;
            if (type == 29) { if (quest_flags_ & 8) { Npc p; p.lx = cx * 128 + 64; p.ly = cy * 128 + 64; p.level = n; npcs_.push_back(p); } continue; }
            for (size_t i = 0; i < mtypes_.size(); i++)
                if (mtypes_[i].slot == type) {
                    Enemy e; e.type = (int)i; e.hp = roll_hp(mtypes_[i]); e.lx = cx * 128 + 64; e.ly = cy * 128 + 64; e.hlx = e.lx; e.hly = e.ly; e.cooldown = rnd(0, attack_period_);
                    ienemies_.push_back(e);
                }
        }
        if (n == 4 || n == 6 || n == 7) {   // Pax Tharkas is crawling with draconians (the module's barracks, guardrooms and patrols): a lot more of them, along the passages away from where the party arrives
            const Level& L = levels_[n];
            auto exits = [&](int x, int y) { int v = L.cells[y * L.w + x]; return v ? iexits_[std::min(v, (int)iexits_.size() - 1)] : -1; };
            int ba = -1, ka = -1, bo = -1;
            for (size_t i = 0; i < mtypes_.size(); i++) { if (mtypes_[i].name == "Baaz") ba = (int)i; if (mtypes_[i].name == "Kapak") ka = (int)i; if (mtypes_[i].name == "Bozak") bo = (int)i; }
            for (int y = 0; y < L.h; y++)
                for (int x = 0; x < L.w; x++) {
                    int v = L.cells[y * L.w + x];
                    if (v != 5 && v != 15 && v != 20 && v != 2) continue;   // plain floor and passage cells
                    (void)exits;
                    bool near_arrival = false;
                    for (auto& l : links_) if (l.to == n && l.mode == 3 && abs(l.dx - x) + abs(l.dy - y) < 5) near_arrival = true;
                    if (near_arrival) continue;
                    if (n == 6 && abs(x - 13) + abs(y - 23) < 6) continue;   // the last stretch to the exit door is left clear (the debug menu drops the party there)
                    bool taken = false;
                    for (auto& r : j["records"]) if (r[3] == n && abs((int)r[1] - x) + abs((int)r[2] - y) < 2) taken = true;
                    if (taken) continue;
                    unsigned hs = (unsigned)(x * 73856093 ^ y * 19349663 ^ n * 83492791) * 2654435761u; hs ^= hs >> 15;
                    if (hs % 100 >= 17) continue;   // about one passage cell in six
                    unsigned pick = (hs / 100) % 100;
                    int type = pick < 52 ? ba : pick < 86 ? ka : bo;
                    if (type < 0) continue;
                    Enemy e; e.type = type; e.hp = roll_hp(mtypes_[type]); e.lx = x * 128 + 40 + (int)((hs >> 8) % 48); e.ly = y * 128 + 64; e.hlx = e.lx; e.hly = e.ly; e.cooldown = rnd(0, attack_period_);
                    ienemies_.push_back(e);
                }
        }
        if (n == 6) { Npc p; p.kind = 2; p.level = 6; p.lx = 17 * 128 + 64; p.ly = 16 * 128 + 64; npcs_.push_back(p); }   // Elistan, among the women of the prisons
        if (n == 2) {   // the cells of Sla-Mori's first level: a captive in some, gully dwarves in the others (the rooms that already hold a creature keep it)
            const Level& L = levels_[2];
            auto exits = [&](int x, int y) { int v = L.cells[y * L.w + x]; return v ? iexits_[std::min(v, (int)iexits_.size() - 1)] : -1; };
            int capt_count = 0;
            for (int y : {3, 6})
                for (int x = 19; x <= 29; x++) {
                    if (y + 1 >= L.h || x >= L.w || exits(x, y) != 4 || exits(x, y + 1) != 1) continue;   // a two-cell room: the back cell opens only south
                    bool taken = false;
                    for (auto& r : j["records"]) if (r[3] == 2 && r[1] == x && (r[2] == y || r[2] == y + 1)) taken = true;
                    if (taken) continue;
                    unsigned hs = (unsigned)(x * 73856093 ^ y * 19349663) * 2654435761u; hs ^= hs >> 15;
                    bool captive = hs % 100 < 65;
                    int capt_no = captive ? capt_count++ : -1;   // the first four captives (in room order, freed or not) give the hint about the secret door
                    if (freed_.count(2 * 10000 + y * 100 + x)) continue;
                    if (captive) { Npc p; p.kind = 1; p.variant = capt_no < 4 ? 30 + capt_no : (int)((hs / 100) % 30); p.lx = x * 128 + 64; p.ly = y * 128 + 22; p.level = 2; npcs_.push_back(p); }
                    else for (size_t i = 0; i < mtypes_.size(); i++) if (mtypes_[i].name == "Aghar")
                        for (int k = 0; k < 2; k++) { Enemy e; e.type = (int)i; e.hp = roll_hp(mtypes_[i]); e.lx = x * 128 + 64; e.ly = y * 128 + 24 + k * 40; e.cooldown = rnd(0, attack_period_); ienemies_.push_back(e); }
                }
        }
    } catch (...) {}
}

// A dungeon fight: the creatures standing in the party's row (east/west view) or column (north/south view) take part, seen along the strip.
void Game::step_interior_fight(const Input& in) {
    const Level& L = levels_[level_];
    int pcx = ix_ >> 7, pcy = iy_ >> 7;
    // what happened to the foes since the last step outside the fight itself (a spell cast from the menu) is carried back to the creatures of the level first
    for (auto& t : enemies_) if (t.tag >= 0 && t.tag < (int)ienemies_.size()) { Enemy& o = ienemies_[t.tag]; o.hp = t.hp; o.frozen = t.frozen; o.turned = t.turned; o.burned = t.burned; o.revived = t.revived; }
    enemies_.clear();
    for (size_t i = 0; i < ienemies_.size(); i++) {
        Enemy& e = ienemies_[i];
        if (e.hp <= 0) continue;
        if (iv_ns_ ? (e.lx >> 7) != pcx : (e.ly >> 7) != pcy) continue;
        Enemy t = e; t.tag = (int)i; t.y = 0;
        t.x = iv_ns_ ? L.h * 128 - 1 - e.ly : e.lx;
        t.ax = iv_ns_ ? L.h * 128 - 1 - e.hly : e.hlx;
        enemies_.push_back(t);
    }
    if (enemies_.empty() && corpses_.empty()) return;
    std::vector<int> before;
    for (auto& e : enemies_) before.push_back(e.tag);
    step_combat(in);
    std::vector<bool> alive(ienemies_.size(), false);
    for (auto& t : enemies_) {
        Enemy& e = ienemies_[t.tag];
        e.hp = t.hp; e.anim = t.anim; e.cooldown = t.cooldown; e.frozen = t.frozen; e.venom = t.venom; e.left = t.left; e.striking = t.striking;
        if (iv_ns_) e.ly = L.h * 128 - 1 - t.x; else e.lx = t.x;
        alive[t.tag] = true;
    }
    for (int tag : before) if (!alive[tag]) ienemies_[tag].hp = 0;  // killed
}

static const char* const kPrisonerThanks[34] = {
    "Thank you for releasing me. May Paladine watch over you.",
    "Praise Mishakal! I had given up hope of ever seeing the sun again.",
    "You have my thanks, stranger. The dragonmen took me from my farm at Solace.",
    "Free at last! Blessed be the Gods of Light for sending you.",
    "I am in your debt. Habbakuk will remember what you did this day.",
    "The Dragon Highlord's soldiers dragged me here in chains. Thank you!",
    "By the Oath and the Measure, I will not forget this kindness.",
    "I thought I should die in this damp cell. You are kind, and brave.",
    "Thank you, friend. My people in Qualinost will sing of this.",
    "Gods be thanked! I heard the fighting and dared not hope.",
    "You have unlocked more than a door. I thank you from my heart.",
    "Reorx's beard, what a welcome sight you are. I mean, thank you.",
    "They said no one would come. They were wrong, and I thank you.",
    "May the red moon and the white moon bless your road, my rescuer.",
    "I am a long way from Haven. Thank you for setting me free.",
    "Bless you, travellers. My children will hear of your courage.",
    "Draconians took my whole village. Thank you for letting me go.",
    "Rest well in the shade of the vallenwood, for you have earned it.",
    "I would give you gold, but they took it all. Take my thanks instead.",
    "Takhisis has no hold on me now. Thank you for the keys, and the courage.",
    "Hurry, they will come back. And thank you, with all my heart.",
    "Mishakal's light was in your hands when you opened that door.",
    "My brother is a Knight of Solamnia. He will honour what you did.",
    "I shall go to the Plainsfolk with word of your deeds. Thank you!",
    "I have prayed for a rescuer for weeks. The Queen of Light heard me.",
    "You have given me back my life. I will pay it forward, I swear.",
    "By Kiri-Jolith, I have never been so glad to see an honest face.",
    "The goblins laughed when they locked me in. Thank you for the last laugh.",
    "The Que-Shu will welcome you as kin after this. Thank you.",
    "Fare well, and watch for the dragons. Thank you for my freedom.",
    // these four also tell the way on (the secret door beside the false door in the long corridor of the first level)
    "Thank you! Listen well: you will meet a false door at the west end of the long corridor, and it leads nowhere. The true door is hidden in the wall right beside it. Stand in the corridor next to it and search with A, then press A again to open it.",
    "Bless you! The guards used a secret way I was never meant to see. At the west end of the long corridor, one step east of the false door, the north wall hides a door. Press A beside it to search, and A once more to open it and walk through.",
    "You have my thanks. If you seek the way on, remember: a false door is a lie, and the real door is the secret one next to it. Stand where the corridor ends, search the wall with A, then open what you find and go north.",
    "Free at last! One thing before you go: the corridor's end shows a door that is false. Do not trouble it. Search the wall just east of it (press A beside it), and a hidden door will show itself; open it with A and it leads on."};

// The sprite of a captive: Goldmoon's standing frame without her staff, the red dress turned to a torn grey-brown rag with a ragged hem, and chains at her wrists.
// Elistan: an old man in a grey-white priest's robe (a nomad's picture with the clothes recoloured).
const Image& Game::elistan_image() {
    if (!elistan_img_.empty()) return elistan_img_;
    for (auto& hh : all_heroes_) if (hh.nomad && !hh.actor.frames.empty()) { elistan_img_ = hh.actor.frames[0]; break; }
    if (elistan_img_.empty()) { elistan_img_.w = 32; elistan_img_.h = 40; elistan_img_.px.assign(32 * 40, 0); return elistan_img_; }
    for (uint8_t& v : elistan_img_.px) { if (v == 11 || v == 6 || v == 12) v = 3; else if (v == 5 || v == 13 || v == 14) v = 7; else if (v == 15) v = 7; }   // robes white and grey, white hair
    return elistan_img_;
}

const Image& Game::prisoner_image() {
    if (!prisoner_img_.empty()) return prisoner_img_;
    for (auto& hh : all_heroes_) if (hh.name.rfind("Goldmoon", 0) == 0 && !hh.actor.frames.empty()) prisoner_img_ = hh.actor.frames[0];
    Image& im = prisoner_img_;
    if (im.empty()) { im.w = 32; im.h = 40; im.px.assign(32 * 40, 0); return im; }
    for (int y = 0; y < im.h; y++)
        for (int x = 0; x < im.w; x++) {
            uint8_t& v = im.px[y * im.w + x];
            if (!v) continue;
            if (x >= 20 && x <= 22 && !(y >= 18 && y <= 24)) { v = 0; continue; }   // the staff
            if (x >= 23) { v = 0; continue; }
            if (y >= 14) {   // the dress: dull cloth, patched and torn
                if (v == 11) v = ((x * 3 + y * 5) % 7 == 0) ? 7 : 12;   // the red dress becomes brown cloth with grey patches (palette: 11 light red, 12 brown, 7 / 8 greys)
                else if (v == 12) v = 8;
                else if (v == 5) v = 8;
                else if (v == 6 && y >= 26) v = 12;
            }
            if (y >= 30 && ((x * 5 + y * 3) % 4 == 0 || (y >= 36 && (x + y) % 3 == 0))) v = 0;   // the ragged hem
            if (y >= 20 && y < 30 && ((x * 11 + y * 7) % 23 == 0)) v = 0;                           // holes
        }
    for (int y = 21; y <= 22; y++) for (int x = 18; x <= 22; x++) im.px[y * im.w + x] = (x + y) % 2 ? 7 : 8;   // manacles
    for (int y = 23; y <= 29; y++) if (im.px[y * im.w + 21] == 0) im.px[y * im.w + 21] = y % 2 ? 7 : 8;        // a chain hanging down
    return im;
}

// Story triggers inside the dungeons: the women (id 56) and children (id 57) to free, the south door (id 58) that ends the game, and Laurana.
// Elves have an eye for secret doors: each time the party comes into a cell with a hidden door in it or in the walls beside it, the elves in the party notice it
// (and reveal it as the Inspect command would) four times in five.
void Game::elves_search() {
    int cx = ix_ >> 7, cy = iy_ >> 7, key = level_ * 10000 + cy * 100 + cx;
    if (key == elf_cell_) return;   // one look per cell
    elf_cell_ = key;
    const char* who = nullptr;
    for (const char* nm : {"Gilthanas", "Princess Laurana", "Tanis"})
        for (auto& m : party_) if (m.hp > 0 && m.name.rfind(nm, 0) == 0 && !who) who = nm;
    if (!who) return;
    Level& L = levels_[level_];
    static const int d4[5][2] = {{0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (auto& d : d4) {
        int x = cx + d[0], y = cy + d[1];
        if (x < 0 || y < 0 || x >= L.w || y >= L.h) continue;
        int& v = L.cells[y * L.w + x];
        int fl = v < (int)isecret_.size() ? isecret_[v] : 0;
        if ((fl & 7) != 1) continue;
        if (rnd(1, 100) > 80) return;   // this time the elves miss it
        v++;
        auto bump = [&](int xx, int yy) { if (xx >= 0 && yy >= 0 && xx < L.w && yy < L.h) L.cells[yy * L.w + xx]++; };
        if (fl & 0x80) bump(x - 1, y);
        if (fl & 0x20) bump(x + 1, y);
        if (fl & 0x10) bump(x, y - 1);
        if (fl & 0x40) bump(x, y + 1);
        istrip_key_ = -1;
        std::string nm = who; if (nm.rfind("Princess ", 0) == 0) nm = nm.substr(9);
        message_ = nm + " spots a secret door!"; message_steps_ = 40;
        fx(kFxFindTraps);
        return;
    }
}

void Game::check_story_items() {
    int px = ix_ + 8, py = iy_ + 8;
    elves_search();
    for (size_t i = 0; i < slabs_.size();) { if (slabs_[i].level != level_ || slabs_[i].cx != (ix_ >> 7) || slabs_[i].cy != (iy_ >> 7)) slabs_.erase(slabs_.begin() + i); else i++; }   // a slab goes when the party leaves the cell
    if (reveal_ > 0) reveal_--;
    for (auto& g : items_) {  // hidden traps (id 59): Tasslehoff leading finds and removes one 45% of the time, otherwise it goes off
        if (g.taken || g.level != level_ || g.id != 59 || g.in_chest >= 0) continue;
        if (abs(g.x - px) >= 24 || abs(g.y - py) >= 24) continue;
        g.taken = true;
        // Tasslehoff leading disarms a trap he can see (Find traps or Detect treasure on) as he touches it, and one he cannot see 45% of the time; otherwise a slab falls
        if (party_[leader_].name.rfind("Tasslehoff", 0) == 0 && (reveal_ > 0 || rnd(0, 99) < 45)) { show_box(30); return; }
        Slab s; s.level = level_; s.cx = ix_ >> 7; s.cy = iy_ >> 7; s.sx = istrip_x(); s.dmg = rnd(2, 8);
        slabs_.push_back(s);
        return;
    }
    for (auto& g : items_) {
        if (g.taken || g.level != level_ || g.id < 56 || g.id > 58) continue;
        if (abs(g.x - px) >= 25 || abs(g.y - py) >= 25) continue;
        if (g.id == 56 && (quest_flags_ & 1)) { quest_flags_ &= ~1; g.taken = true; party_[leader_].xp += 10000; xp_ += 10000; show_box(34); return; }
        if (g.id == 57 && (quest_flags_ & 2)) { quest_flags_ &= ~2; g.taken = true; party_[leader_].xp += 10000; xp_ += 10000; show_box(21); return; }
        if (g.id == 58) { start_ending(); return; }
    }
    for (auto& p : npcs_) {
        if (p.kind == 2) { elistan_step(p); continue; }
        if (p.kind != 1 || p.done || abs(p.lx - px) >= 40 || abs(p.ly - py) >= 40) continue;
        p.done = true; freed_.insert(p.level * 10000 + (p.ly >> 7) * 100 + (p.lx >> 7));
        party_[leader_].xp += 150; xp_ += 150;
        box_ = kPrisonerThanks[p.variant % 34];
        return;
    }
    for (auto& p : npcs_) {
        if (p.kind != 0 || p.done || abs(p.lx - px) >= 40 || abs(p.ly - py) >= 40) continue;
        p.done = true; quest_flags_ &= ~8; party_[leader_].xp += 10000; xp_ += 10000;
        for (auto& h : all_heroes_) if (h.name.rfind("Princess Laurana", 0) == 0) {
            bool in = false;
            for (auto& m : party_) if (m.name == h.name) in = true;
            if (!in) party_.push_back(h);  // she joins the party
        }
        show_box(44);
        return;
    }
}

// The end: message (quest complete or not), each hero's score (experience + 2000), the high scores.
void Game::start_ending() { mode_ = Mode::Ending; ending_page_ = 0; end_cool_ = 4; box_.clear(); enemies_.clear(); corpses_.clear(); }

void Game::step_ending(const Input& in) {
    const auto& epi = quest_flags_ == 0 ? epi_complete_ : epi_incomplete_;
    int n = (int)epi.size();
    int total = 0;
    for (auto& h : party_) total += h.xp + 2000;
    bool name_page = qualifies_ && ending_page_ == n + 2;
    int high_page = qualifies_ ? n + 3 : n + 2;
    auto advance = [&]() {
        ending_page_++;
        if (ending_page_ == n + 2) {  // after the scores: does the party make the table?
            qualifies_ = highscores_.size() < 5 || total > highscores_.back().second;
            name_buf_.clear();
            if (!qualifies_) { /* straight to the table */ }
        }
        if (ending_page_ == high_page + 1 || ending_page_ > n + 4) { reset_party(); level_ = 1; quest_flags_ = 0x0f; spawn_wild(); load_items(); go_title(); set_title_page(2); }   // after the scores: the credits, and from them the main menu
    };
    if (name_page) {  // type a name for the high score table
        const std::string typed = pend_text_;   // everything typed since the last step (frame() gathers it every frame)
        for (char c : typed) if (c >= 0x20 && c < 0x7f && name_buf_.size() < 16) name_buf_ += c;
        for (int b = 0; b < pend_back_ && !name_buf_.empty(); b++) name_buf_.pop_back();
        bool typed_space = typed.find(' ') != std::string::npos;
        pend_text_.clear(); pend_back_ = 0;
        if (in.confirm && !confirm_prev_ && !typed_space) {
            highscores_.push_back({name_buf_.empty() ? "Your party" : name_buf_, total});
            std::stable_sort(highscores_.begin(), highscores_.end(), [](auto& a, auto& b) { return a.second > b.second; });
            if (highscores_.size() > 5) highscores_.resize(5);
            save_scores();
            ending_page_++;
        }
        confirm_prev_ = in.confirm;
        return;
    }
    // one page per press: Left / Up / Back go to the page before (not once the score has been entered), anything else goes on
    const bool back = edges_[0] || edges_[2] || edges_[4];
    const bool confirm_edge = in.confirm && !confirm_prev_;
    confirm_prev_ = in.confirm;
    if (end_cool_ > 0) { end_cool_--; return; }   // a single press can show up as a key event and as a confirm a step later: it turns one page, not two
    if (back) { if (ending_page_ > 0 && ending_page_ <= n + 1) { ending_page_--; end_cool_ = 1; } return; }
    if (in.any || confirm_edge || edges_[1] || edges_[3] || edges_[5]) { advance(); end_cool_ = 1; }
}

void Game::draw_ending() {
    char b[96];
    const auto& epi = quest_flags_ == 0 ? epi_complete_ : epi_incomplete_;
    int n = (int)epi.size();
    if (end_pax_.empty()) { end_pax_ = assets_.picture("pax"); end_child_ = assets_.picture("child"); }
    // A slide show: the original's two pictures go with the story, the fortress at the start and the freed prisoners where the epilogue speaks of them.
    const bool complete = quest_flags_ == 0;
    const Image* pic = nullptr;
    if (ending_page_ == 0 || ending_page_ == 1) pic = &end_pax_;
    else if (complete && ending_page_ == 2) pic = &end_child_;
    if (pic && !pic->empty() && ending_page_ <= n) {
        screen_.reset_clip();
        screen_.clear(0);
        screen_.blit(*pic, 0, 0);
        screen_.fill_rect(0, 126, kW, 74, 0);   // (the pictures are 120 and 126 rows high)
        screen_.fill_rect(0, 127, kW, 1, 7); screen_.fill_rect(0, 129, kW, 1, 8);
        std::string t = ending_page_ == 0 ? (quest_flags_ == 0 ? texts_[26] : texts_[27]) : epi[ending_page_ - 1];
        for (auto& ch : t) if (ch == '\r') ch = ' ';
        wrap_text(screen_, 8, 134, t, 38, 7, 10, 0, 9);
        screen_.text(kW - 8 * 21, kH - 10, "Left: back  Enter: on", 8, false);
        return;
    }
    if (ending_page_ == 0) {
        draw_scroll("", 7);
        int idx = quest_flags_ == 0 ? 26 : 27;
        if (idx < (int)texts_.size()) wrap_text(screen_, 66, 34, texts_[idx], 24, 8, 10, 0, 9);
    } else if (ending_page_ <= n) {  // epilogue (written for this version, after the novel)
        draw_scroll("", 7);
        wrap_text(screen_, 66, 32, epi[ending_page_ - 1], 24, 8, 10, 0, 8);
    } else if (ending_page_ == n + 1) {
        draw_scroll("SCORES", 7);
        int total = 0, y = 28;
        for (auto& h : party_) { int sc = h.xp + 2000; total += sc; snprintf(b, sizeof b, "%-12.12s %6d", h.name.c_str(), sc); screen_.text(88, y, b, 10); y += 7; }   // (seven px a line: nine heroes and the total fit the scroll)
        snprintf(b, sizeof b, "Total        %6d", total); screen_.text(88, y, b, 5);
    } else if (qualifies_ && ending_page_ == n + 2) {
        draw_scroll("NEW HIGH SCORE", 7);
        screen_.text(72, 44, "Type your name,", 10);
        screen_.text(72, 54, "then press Enter:", 10);
        snprintf(b, sizeof b, "%s%c", name_buf_.c_str(), (steps_ / 4) % 2 ? '_' : ' ');
        screen_.text(80, 70, b, 5);
    } else {
        draw_scroll("HIGH SCORES", 7);
        int y = 40;
        for (auto& e : highscores_) { snprintf(b, sizeof b, "%-16.16s %6d", e.first.c_str(), e.second); screen_.text(80, y, b, 10); y += 10; }
    }
    draw_panel();
}

// Level changes (game.exe fn 3CBE, table at data 0x287A).
void Game::check_links() {
    int x = interior() ? ix_ + 8 : px_, y = interior() ? iy_ + 8 : py_;
    if (!interior()) { x *= 8; y *= 8; }  // outdoors the table is in combat units (world x 8)
    for (auto& l : links_) {
        if (l.from != level_) continue;
        // outdoors the party's whole 16x16 body counts (the original tests its top-left corner only: 16 px less forgiving)
        if (!interior()) {  // outdoors the way in is the X on the map: the open cell of the entrance (the original's patch is two cells wide); touching it is enough
            int cx0 = l.x1 / 8 + 16, cy0 = l.y1 / 8;
            if (px_ < cx0 - 15 || px_ > cx0 + 15 || py_ < cy0 - 15 || py_ > cy0 + 15) continue;
        } else if (x < l.x1 || x > l.x2 || y < l.y1 || y > l.y2) continue;
        if (l.mode == 3) { enter_level(l.to, l.dx, l.dy); iv_ns_ = (l.flags & 1) != 0; }  // flag 1 starts looking north/south (b303A)
        else { level_ = 1; px_ = l.dx; py_ = l.dy; enemies_.clear(); corpses_.clear(); fix_start(); message_ = "Outdoors"; message_steps_ = 12; }
        return;
    }
}

void Game::enter_level(int n, int cell_x, int cell_y) {
    enemies_.clear(); corpses_.clear(); ienemies_.clear(); npcs_.clear();
    from_wild_ = false;
    if (n <= 1 || n > 7 || levels_[n].w == 0) {
        level_ = 1;
        message_ = "Outdoors"; message_steps_ = 12;
        return;
    }
    level_ = n;
    spawn_level_creatures(n);
    const Level& L = levels_[n];
    int best = -1;
    for (int i = 0; i < (int)L.cells.size(); i++) if (L.cells[i] == 5) { best = i; break; }  // first plain floor cell
    if (best < 0) for (int i = 0; i < (int)L.cells.size(); i++) if (L.cells[i]) { best = i; break; }
    ix_ = (best % L.w) * 128 + 56; iy_ = (best / L.w) * 128 + 56;
    if (cell_x < 0 && getenv("DOF_CELL")) sscanf(getenv("DOF_CELL"), "%d,%d", &cell_x, &cell_y);  // test hook
    if (cell_x >= 0) { ix_ = cell_x * 128 + 64 - 8; iy_ = cell_y * 128 + 64 - 8; }
    for (int r = 0; r < 200 && !level_can_move(ix_, iy_, 0, 0); r++) {  // nearest clear spot
        bool found = false;
        for (int dy = -r; dy <= r && !found; dy += 8) for (int dx = -r; dx <= r && !found; dx += 8)
            if (level_can_move(ix_ + dx, iy_ + dy, 0, 0)) { ix_ += dx; iy_ += dy; found = true; }
        if (found) break;
    }
    char b[32]; snprintf(b, sizeof b, "Level %d", n);
    message_ = b; message_steps_ = 18;
}

void Game::toggle_view() {
    missiles_.clear(); floaters_.clear();
    if (lair_active_ && mode_ == Mode::Combat) {   // out of the cave: on the very tile of the lair, wherever the party moved inside
        if (!lair_chest_taken_) { poi_done_.erase(lair_poi_name_); poi_block_ = lair_poi_name_; }   // left without the treasure: the lair is still there to try again (once the party has moved off its tile)
        lair_active_ = false; from_wild_ = false; enemies_.clear(); corpses_.clear(); mode_ = Mode::World; return;
    }
    if (mode_ != Mode::World) talk_.active = false;
    if (mode_ == Mode::World) {
        strip_.build(world_, px_, py_, east_west_);
        cx_ = strip_.start_x(px_, py_, east_west_);
        cy_ = ground_y(cx_) - 16;  // the party stands on the ground at once
        if (!no_encounters_) spawn_encounter();  // (estimate) the original places the monsters that were near the party in the wilderness
        if (getenv("DOF_GROUNDDUMP")) { for (int x = cx_ - 400; x <= cx_ + 400; x += 16) fprintf(stderr, "%d:%d ", x - cx_, ground_y(x)); fprintf(stderr, "\n"); }
        mode_ = Mode::Combat;
    } else {
        strip_.world_from_x(cx_, px_, py_, east_west_, &px_, &py_);
        fix_start();
        enemies_.clear(); corpses_.clear();
        mode_ = Mode::World;
    }
}


// God mode: everyone is at full hit points (the dead too), no spell is ever spent, the Disks and the Staff are full.
void Game::apply_god() {
    if (!god()) return;
    for (auto& hh : party_) hh.hp = std::max(hh.hp, hh.hp_max);
    for (bool& u : used_) u = false;
    disks_ = staff_ = 100; staff_broken_ = false;   // (God mode mends even the broken Staff)
}

void Game::step(const Input& in) {
    steps_++;
    if (const char* is = getenv("DOF_IMPACT")) if (steps_ == atoi(is) && mode_ == Mode::Combat) for (auto& e : enemies_) add_impact(e, atoi(strchr(is, ',') ? strchr(is, ',') + 1 : "0"));   // test hook: "step,kind"
    if (const char* cs = getenv("DOF_CAST")) { int st = 0, sc = 0, sp = 0; if (sscanf(cs, "%d,%d,%d", &st, &sc, &sp) == 3 && steps_ == st && mode_ == Mode::Combat) { leader_ = 3; cast(sc, sp); } }   // test hook: "step,school,spell"
    if (const char* ls = getenv("DOF_LIGHTNING")) if (steps_ == atoi(ls) && mode_ == Mode::Combat) cast_lightning();   // test hook: the Lightning Bolt scroll on that step
    if (const char* fs = getenv("DOF_FINAL")) if (steps_ == atoi(fs) && (mode_ == Mode::Combat || interior())) { leader_ = 3; cast(1, 8); }   // test hook: Raistlin's Final Strike on that step
    if (detect_ > 0) detect_--;
    if (ward_ > 0) ward_--;   // the spells run out with time (six steps a second), fighting or not
    if (bless_ > 0) bless_--;
    apply_god();
    if (message_steps_ > 0) message_steps_--;
    if (mode_ == Mode::Help) { step_help(in); return; }
    if (mode_ == Mode::Debug) { step_debug(in); return; }
    if (mode_ == Mode::Title) {
        if (quick_title_) { if (in.any || in.confirm) enter_world(); }
        else step_title(in);
        return;
    }
    if (mode_ == Mode::Ending) { step_ending(in); return; }
    if (mode_ == Mode::GameOver) {
        if (in.any || in.confirm) { reset_party(); level_ = 1; spawn_wild(); go_title(); }
        return;
    }
    if (!box_.empty()) {  // turbo: message boxes close by themselves
        attacking_ = false; ranged_pose_ = 0;   // nothing is being swung while a message is up
        std::string t; for (char c : box_) t += (c == '\n' ? ' ' : c);
        int lines = wrap_count(t, 22);
        if (box_ != box_seen_) { box_seen_ = box_; box_scroll_ = 0; }
        if (turbo_) { box_.clear(); return; }
        // A message stays up for at least box_hold_ frames (three quarters of a second) and only a new press closes it, so a double tap or a held key cannot skip it.
        bool held_enough = render_frames_ - box_since_ >= box_hold_;
        if (!held_enough) return;
        if (choice_) { if (edges_[5]) choice_answer(true); else if (edges_[4]) choice_answer(false); return; }   // a question: A says yes, B no
        if (lines <= 6) { if (in.any || edges_[5]) box_.clear(); return; }
        int maxs = lines - 6;   // a long message: up / down scroll a line, the confirm key pages down and, at the end, closes; Esc closes at once
        if (edges_[0]) box_scroll_ = std::max(0, box_scroll_ - 1);
        if (edges_[1]) box_scroll_ = std::min(maxs, box_scroll_ + 1);
        if (edges_[4]) { box_.clear(); return; }
        if (edges_[5]) { if (box_scroll_ < maxs) box_scroll_ = std::min(maxs, box_scroll_ + 6); else box_.clear(); }
        return;
    }
    if (mode_ == Mode::Menu) { attacking_ = false; ranged_pose_ = 0; step_menu(in); return; }
    // the attack pose follows the held button: it is renewed on every step, wherever the party is (the fight code used to be the only place that cleared it)
    attacking_ = in.attack && !(!orig_controls_ && in.ranged);
    if (hold_ > 0) hold_--;
    if (held()) attacking_ = false;
    if (ranged_pose_ > 0) ranged_pose_--;
    if (--qm_countdown_ <= 0) { qm_countdown_ = 360; army_tick(); }  // the armies move once every 360 logic steps (6 x 60)
    bool confirm_edge = in.confirm && !confirm_prev_;  // in.confirm is latched for taps between steps
    confirm_prev_ = in.confirm;
    if (in.select > 0) set_leader(in.select - 1);
    if (in.cycle) { int n = (int)party_.size(); for (int k = 1; k <= n; k++) { int i = ((leader_ + in.cycle * k) % n + n) % n; if (party_[i].hp > 0) { set_leader(i); break; } } }
    if (in.save) save_game();
    if (in.load) { load_game(); return; }
    if (in.level && mode_ == Mode::World) { enter_level(in.level); return; }
    if (in.map && (mode_ == Mode::World || mode_ == Mode::Combat)) {  // Back on the gamepad: the world map, straight from play
        menu_return_ = mode_; mode_ = Mode::Menu; menu_item_ = 0; menu_open_step_ = steps_;
        menu_page_ = kPageQuest; menu_direct_ = true; menu_scroll_ = 0;
        { int y = interior() ? 360 : py_ / 8; qm_top_ = y <= 64 ? 0 : std::min(256, y - 64); }
        return;
    }
    if (in.toggle && !interior()) {
        // Y / Tab / 0 switch between the map and the side view. In a fight it waits until it has calmed down: while the fight music plays (a live foe within 380 px) it is refused
        // (the debug menu can turn this off)
        bool foes = false;
        for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - cx_) < 380) foes = true;
        if (mode_ == Mode::Combat && foes && !y_exit_any_) {
            message_ = "Foes are all around: you cannot leave yet"; message_steps_ = 18;
            return;
        }
        toggle_view(); from_wild_ = false; return;
    }
    if ((confirm_edge && mode_ == Mode::World) || (in.cancel && !cancel_prev_ && (mode_ == Mode::World || mode_ == Mode::Combat))) { menu_return_ = mode_; mode_ = Mode::Menu; menu_item_ = 0; menu_page_ = -1; menu_open_step_ = steps_; menu_direct_ = false; cancel_prev_ = in.cancel; return; }
    cancel_prev_ = in.cancel;
    bool combat = mode_ == Mode::Combat;
    bool inside = !combat && interior();
    bool moved;
    if (!orig_controls_ && !turbo_) { moved = moved_frames_; moved_frames_ = false; }  // updated controls: the party was already moved, frame by frame
    else moved = held() ? false : move_party(in, rules_.walk_step * run_factor(), edges_[0], edges_[1], in.jump, false);
    if (combat) { step_combat(in); step_talk(); return; }
    if (inside && itable_loaded_) {
        step_interior_fight(in);
        if (mode_ == Mode::World) { check_story_items(); check_pois(); if (moved) check_links(); }
        return;
    }
    if (inside && (!enemies_.empty() || !corpses_.empty())) step_combat(in);
    else if (!inside) { step_wild(); check_pois(); }
    if (moved && mode_ == Mode::World) check_links();
}

// Updated controls: called every rendered frame (about 60 times a second) so the party answers a key at once. The logic step (6 a second, as in the original) only
// handles monsters and the rest. Walking is one pixel at a time at fast_walk px/s, the walking animation follows the distance walked, and a jump is an arc (data/jumps.json).
void Game::frame_move(const Input& in, double dt_ms) {
    bool up_edge = in.up && !fm_up_ && !in.pad_up, down_edge = in.down && !fm_down_ && !in.pad_down;   // the controller's stick neither jumps nor turns
    fm_up_ = in.up; fm_down_ = in.down;
    bool combat = mode_ == Mode::Combat;
    if (!(combat || mode_ == Mode::World) || !box_.empty()) { jump_t_ = -1; jump_h_ = 0; move_acc_ = 0; fm_moving_ = false; return; }
    if (held()) { jump_t_ = -1; jump_h_ = 0; move_acc_ = 0; fm_moving_ = false; return; }   // stuck or poisoned: no moving, no jumping
    const bool strip_side = combat || interior();   // the side view outdoors, or the dungeon strip
    if (!strip_side) { jump_t_ = -1; jump_h_ = 0; }
    if (strip_side && jump_t_ >= 0) {
        jump_t_ += dt_ms / jump_dur_;
        double p = std::min(1.0, jump_t_);
        jump_p_ = p;
        int xpos = jump_from_ + (int)std::lround((jump_to_ - jump_from_) * p);
        double s = p * p * (3 - 2 * p);
        int g = 0;
        if (combat) { cx_ = xpos; g = jump_g0_ + (int)std::lround((ground_y(jump_to_) - jump_g0_) * s); }
        else set_istrip_x(xpos);
        {  // the height above the straight line follows the ground profile measured in the original's jump
            const auto& pr = rules_.jump_profile;
            double f = p * (pr.size() - 1); size_t i0 = std::min((size_t)f, pr.size() - 2);
            double hgt = pr[i0] + (pr[i0 + 1] - pr[i0]) * (f - i0);
            jump_h_ = (int)std::lround(hgt * jump_scale_);
        }
        if (combat) cy_ = g - 16;
        anim_ = 2;
        if (p >= 1) { jump_t_ = -1; jump_h_ = 0; if (combat) { cx_ = jump_to_; cy_ = ground_y(cx_) - 16; } else set_istrip_x(jump_to_); anim_ = 0; }
        moved_frames_ = true;
        return;
    }
    bool want = in.left || in.right || in.up || in.down || in.kp1 || in.kp3 || in.kp7 || in.kp9;
    int n = 0;
    if (!want) { move_acc_ = 0; fm_moving_ = false; }
    else {
        if (!fm_moving_) { move_acc_ = 1; fm_moving_ = true; }  // the first pixel comes on the very frame the key goes down
        else move_acc_ += dt_ms * rules_.fast_walk * run_factor() / 1000.0;
        n = std::min(8 * run_factor(), (int)move_acc_); move_acc_ -= n;
        if (move_acc_ > 2 * run_factor()) move_acc_ = 2 * run_factor();
    }
    bool moved = move_party(in, n, up_edge, down_edge, in.jump, true);
    if (jump_t_ >= 0) return;  // a jump has just started
    if (moved) { walk_dist_ += n; anim_ = (combat || interior() ? 1 : 2) + walk_dist_ / 8; moved_frames_ = true; }
    else if (n > 0 || !want) { anim_ = 0; walk_dist_ = 0; }
}


const HeroPoses& Game::poses_for(const std::string& sprite) const {
    for (auto& p : poses_) if (p.first == sprite) return p.second;
    return poses_default_;
}

JumpSpec Game::jump_for(const Hero& h) const {
    for (auto& hj : rules_.hero_jumps) if (h.name.rfind(hj.first, 0) == 0) return hj.second;
    return rules_.jump;
}

bool Game::move_party(const Input& in, int px, bool up_edge, bool down_edge, int jump_in, bool fast) {
    int dx = (in.right ? 1 : 0) - (in.left ? 1 : 0), dy = (in.down ? 1 : 0) - (in.up ? 1 : 0);
    bool combat = mode_ == Mode::Combat;
    bool moved = false;
    if (!combat && !interior()) {  // outdoors the keypad diagonals walk diagonally (in the side view 7 and 9 are jumps)
        dx += (in.kp3 || in.kp9 ? 1 : 0) - (in.kp1 || in.kp7 ? 1 : 0);
        dy += (in.kp1 || in.kp3 ? 1 : 0) - (in.kp7 || in.kp9 ? 1 : 0);
        dx = std::clamp(dx, -1, 1); dy = std::clamp(dy, -1, 1);
        if (dx || dy) { move_dx_ = dx; move_dy_ = dy; }
    }
    bool inside = !combat && interior();
    int& x = combat ? cx_ : (inside ? ix_ : px_);
    int& y = combat ? cy_ : (inside ? iy_ : py_);
    auto can = [&](int ddx, int ddy) {
        if (ghost_) {   // debug: nothing blocks, only the edges of the map
            if (combat) return x + ddx >= 0 && x + ddx + 16 <= strip_.width_px();
            if (inside) return x + ddx >= 0 && y + ddy >= 0 && x + ddx + 16 <= levels_[level_].w * 128 && y + ddy + 16 <= levels_[level_].h * 128;
            return x + ddx >= 0 && y + ddy >= 0 && x + ddx + 16 <= world_.width_px() && y + ddy + 16 <= world_.height_px();
        }
        if (combat && lair_active_) return x + ddx >= lair_x0_ + 10 && x + ddx + 16 <= lair_x1_ - 10;
        if (combat) return strip_.can_move(world_, x, y, ddx, ddy);
        if (inside) return level_can_move(x, y, ddx, ddy);
        return world_.can_move(x, y, ddx, ddy);
    };
    // Corner correction (updated controls walk a pixel at a time, so the party is rarely lined up with the 16-px tiles): when a step in direction (mx, my) is blocked,
    // look up to 8 px to either side for a position from which the step is free and move one pixel that way. A passage exactly one tile wide can then be entered.
    auto slide = [&](int mx, int my) -> bool {
        bool across_x = my != 0;   // moving up or down: slide sideways
        for (int k = 1; k <= 8; k++)
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                int sx = x, sy = y; bool ok = true;
                for (int s = 0; s < k && ok; s++) {
                    int ddx = across_x ? sgn : 0, ddy = across_x ? 0 : sgn;
                    if (can(ddx, ddy)) { x += ddx; y += ddy; } else ok = false;
                }
                bool free_step = ok && can(mx, my);
                x = sx; y = sy;
                if (free_step) { if (across_x) x += sgn; else y += sgn; return true; }
            }
        return false;
    };
    if (combat) {  // the side view runs along one axis only: left/right walk, up/down (the original's 8 and 2) turn to the other axis
        bool turn = (orig_controls_ ? (up_edge || down_edge) : (down_edge || (fast && in.turn))) && !lair_active_;   // (a cave runs one way only: there is no other direction to turn to)  // original: 8 and 2 turn; otherwise only down turns and up jumps
        if (turn) {
            int owx = px_, owy = py_;
            strip_.world_from_x(cx_, px_, py_, east_west_, &px_, &py_);
            east_west_ = !east_west_;
            strip_.build(world_, px_, py_, east_west_);
            int nx = strip_.start_x(px_, py_, east_west_);
            if ((ghost_ ? (nx >= 0 && nx + 16 <= strip_.width_px()) : lair_active_ ? (nx >= lair_x0_ + 10 && nx + 16 <= lair_x1_ - 10) : strip_.can_move(world_, nx, cy_, 0, 0))) { cx_ = nx; for (auto& e : enemies_) e.x = std::clamp(nx + (e.x > cx_ ? 1 : -1) * std::min(abs(e.x - cx_), 400), 0, strip_.width_px() - 32); }
            else { east_west_ = !east_west_; px_ = owx; py_ = owy; strip_.build(world_, px_, py_, east_west_); }
        }
        bool moved_c = false;
        if (orig_controls_ && in.attack) dx = 0;   // + held: the direction aims the attack instead of walking
        int ground = ground_y(cx_);
        for (int s = 0; s < (dx ? px : 0); s++) {  // walking: the ground may rise by at most one tile
            int ng = ground_y(cx_ + dx);
            bool out = lair_active_ ? (cx_ + dx < lair_x0_ + 10 || cx_ + dx + 16 > lair_x1_ - 10) : (cx_ + dx < 0 || cx_ + dx > strip_.width_px() - 16);   // (the cave has walls at both ends)
            if (out || (!ghost_ && (ng < ground - (fast ? 0 : 16) || water_at(cx_ + dx + 8)))) break;   // (a river has to be jumped)  // updated controls: a ledge has to be jumped, nobody steps up it
            bool hit_enemy = false;
            for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - (cx_ + dx)) < 20 && abs(e.x - (cx_ + dx)) < abs(e.x - cx_)) hit_enemy = true;
            if (hit_enemy) break;
            cx_ += dx; ground = ng; moved_c = true;
        }
        if (dx) { face_left_ = dx < 0; facing_ = 0; }
        int jmp = jump_in;
        if (orig_controls_ && in.attack) {  // original controls: hold + and a keypad direction to attack that way; nothing walks or jumps meanwhile
            int adx = (in.right || in.kp9 || in.kp3 ? 1 : 0) - (in.left || in.kp7 || in.kp1 ? 1 : 0);
            if (adx) face_left_ = adx < 0;
            jmp = 0;
        }
        if (!orig_controls_ && up_edge && !jmp) jmp = face_left_ ? -1 : 1;  // without the original controls the up arrow jumps
        if (in.hop && !jmp) jmp = face_left_ ? -1 : 1;  // the controller's jump button
        if (jmp) {  // original controls: a jump covers 48 px and may land up to 48 px higher (hills, rivers)
            int dir = jmp;
            face_left_ = dir < 0;
            JumpSpec sp = jump_for(party_[leader_]);
            auto free_at = [&](int tx, int climb) {
                bool landing_free = true;
                for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - tx) < 20) landing_free = false;
                return landing_free && (lair_active_ ? (tx >= lair_x0_ + 10 && tx + 16 <= lair_x1_ - 10) : (tx >= 0 && tx <= strip_.width_px() - 16)) && (ghost_ || (ground_y(tx) >= ground - climb && !water_at(tx + 8)));
            };
            if (!fast) {
                int tx = cx_ + dir * 48;
                if (free_at(tx, 48)) { cx_ = tx; ground = ground_y(tx); moved_c = true; }
            } else {  // updated controls: this hero's own jump (data/jumps.json). No ledge is climbed unless the hero can jump that high.
                int maxd = sp.distance;
                for (int d = 8; d <= sp.distance; d += 8)  // a wall higher than this hero can reach stops the flight in front of it
                    if (ground_y(cx_ + dir * d) < ground - sp.climb) { maxd = d - 8; break; }
                bool done = false;
                for (int d = maxd; d >= 16 && !done; d -= 8) {
                    int tx = cx_ + dir * d;
                    if (!free_at(tx, sp.climb)) continue;
                    if (ground_y(tx) >= ground + 48) continue;  // water or a pit: not a place to land, try a shorter jump
                    jump_from_ = cx_; jump_to_ = tx; jump_g0_ = ground; jump_t_ = 0;
                    jump_dur_ = 1000.0 * d / (rules_.fast_walk * sp.speed); jump_scale_ = sp.height_scale;   // as fast sideways as the hero walks
                    moved_c = done = true;
                }
                if (!done) {  // nowhere to land: the hero hops on the spot, just before the edge they jumped from
                    jump_from_ = jump_to_ = cx_; jump_g0_ = ground; jump_t_ = 0;
                    jump_dur_ = 450; jump_scale_ = sp.height_scale * 0.5;
                    moved_c = true;
                }
            }
        }
        cy_ = ground - 16;
        if (!fast) { if (moved_c) anim_++; else anim_ = 0; }
        return moved_c;
    }
    if (inside && itable_loaded_) {  // the side view: walk along the strip; up/down (the original's 8 and 2) turn to the other axis
        const Level& L = levels_[level_];
        build_istrip();
        if (orig_controls_ ? (up_edge || down_edge) : (down_edge || in.turn)) {   // classic: 8 and 2 turn; updated: down and the trigger (up jumps)
            int cx = ix_ >> 7, cy = iy_ >> 7;
            int v = (cx >= 0 && cy >= 0 && cx < L.w && cy < L.h) ? L.cells[cy * L.w + cx] : 0;
            int ex = iexits_[std::min(v, (int)iexits_.size() - 1)];
            int along = (iv_ns_ ? iy_ : ix_) & 127;
            if (abs(along + 8 - 64) <= 40 && (iv_ns_ ? (ex & 10) : (ex & 5))) {  // a passage crosses this cell and the party is in the dark middle between the pillars
                iv_ns_ = !iv_ns_;
                ix_ = cx * 128 + 56; iy_ = cy * 128 + 56;  // step into the middle of the crossing passage
                build_istrip();
            }
        }
        if (dx) {
            face_left_ = dx < 0; facing_ = 0;
            int sx = istrip_x(), nx = std::clamp(sx + dx * px, istart_, std::max(istart_, iend_ - 16));
            for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - nx) < 20 && abs(e.x - nx) < abs(e.x - sx)) nx = sx;
            if (nx != sx) { set_istrip_x(nx); moved = true; }
        }
        {   // jumping along the dungeon strip (to cross a gap, to reach the chain): the classic 48 px hop, or this hero's own jump with the updated controls
            int jmp = jump_in;
            if (!orig_controls_ && up_edge && !jmp) jmp = face_left_ ? -1 : 1;
            if (in.hop && !jmp) jmp = face_left_ ? -1 : 1;
            if (jmp && jump_t_ < 0) {
                int sx = istrip_x(), dir = jmp;
                face_left_ = dir < 0;
                JumpSpec sp = fast ? jump_for(party_[leader_]) : JumpSpec{48, 48, 1.0, 1.0};
                for (int d = sp.distance; d >= 16; d -= 8) {
                    int tx = sx + dir * d;
                    if (tx < istart_ || tx > std::max(istart_, iend_ - 16)) continue;
                    bool blocked = false;
                    for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - tx) < 20) blocked = true;
                    if (blocked) continue;
                    if (fast) { jump_from_ = sx; jump_to_ = tx; jump_g0_ = 0; jump_t_ = 0; jump_dur_ = 1000.0 * d / (rules_.fast_walk * sp.speed); jump_scale_ = sp.height_scale; }
                    else set_istrip_x(tx);
                    moved = true;
                    break;
                }
            }
        }
        if (!fast) { if (moved) anim_++; else anim_ = 0; }
        return moved;
    }
    if (dy) facing_ = dy < 0 ? 1 : 2;
    if (dx) {
        facing_ = 0;
        face_left_ = dx < 0;
        if (!combat && !inside) east_west_ = true;
        for (int s = px; s > 0; s--) {  // go as far as the step allows before a wall
            if (can(dx, 0)) { x += dx; moved = true; }
            else if (fast && slide(dx, 0)) moved = true;   // updated controls: slip round the corner into a one-tile-high passage
            else break;
        }
    }
    if (dy) {
        if (!combat && !inside && !dx) east_west_ = false;
        for (int s = px; s > 0; s--) {
            if (can(0, dy)) { y += dy; moved = true; }
            else if (fast && slide(0, dy)) moved = true;
            else break;
        }
    }
    if (!fast) { if (moved) anim_++; else anim_ = 0; }
    return moved;
}

void Game::step_combat(const Input& in) {
    Hero& lead = party_[leader_];
    const bool in_level = interior();
    int hero_sx = in_level ? istrip_x() : 0, zero_y = 0;
    int& cx_ = in_level ? hero_sx : this->cx_;   // the same fight rules work in the outdoor strip and along a dungeon strip
    int& cy_ = in_level ? zero_y : this->cy_;
    auto can_move = [&](int x, int y, int dx, int dy) { (void)y; (void)dy; return in_level ? (x + dx >= istart_ && x + dx + 16 <= iend_) : lair_active_ ? (x + dx >= lair_x0_ + 10 && x + dx + 16 <= lair_x1_ - 10) : strip_.can_move(world_, x, y, dx, dy); };
    {   bool foes = false; for (auto& e : enemies_) if (e.hp > 0) foes = true; if (!foes) caster_step_ = 0; }
    if (lair_active_ && this->cx_ <= lair_x0_ + 14) {   // walking into the portal leaves the cave once the foes are quiet
        bool near = false; for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - this->cx_) < 380) near = true;
        if (!near || y_exit_any_) { toggle_view(); return; }
        message_ = "Foes are all around: you cannot leave yet"; message_steps_ = 18;
    }
    const bool ranged_now = !orig_controls_ && in.ranged;
    attacking_ = in.attack && !ranged_now;
    // classic: the keypad aims (7 / 9 high, 1 / 3 low, 4 / 6 straight). Updated: the stick up is a high attack, anything else is the low one; LT is the ranged attack.
    aim_ = orig_controls_ ? ((in.kp7 || in.kp9) ? 1 : (in.kp1 || in.kp3) ? -1 : 0) : (in.pad_up ? 1 : -1);
    if (swing_cd_ > 0) swing_cd_--;
    if (ranged_now && swing_cd_ == 0 && ranged_attack()) { swing_cd_ = 4; ranged_pose_ = 5; }
    if (attacking_ && swing_cd_ == 0) {
        swing_cd_ = 4;  // one swing every four steps while the keys are held
        const bool wyr_eq = lead.hand_name == "Wyrmslayer";   // wielded: a +3 sword like any other (item_stats.json), double damage against dragons
        bool wyr = false;
        if (!wyr_eq) for (int id : inv_) if (id == 19 && lead.name.rfind("Caramon", 0) == 0) wyr = true;  // classic controls: Caramon carries it in the pack (cluebook)
        const int wc = weapon_class(lead);   // 0 sword, 1 staff, 2 axe: a whiff, a hit and a glance sound each
        bool engaged = false;
        for (auto& e : enemies_) {
            if (e.hp <= 0) continue;
            int d = e.x - cx_;
            bool front = face_left_ ? d <= 0 : d >= 0;
            if (!(front && abs(d) <= rules_.hero_reach && abs(e.y - cy_) <= 24)) continue;
            const MonsterType& mt = mtypes_[e.type];
            int need = lead.thac0 - mt.ac - lead.to_hit - (bless_ > 0 ? 1 : 0) - (wyr ? 3 : 0) - (wyr_eq ? 3 : 0);  // roll d20 >= THAC0 - target AC
            engaged = true;
            if (rnd(1, 20) < need) { fx(melee_fx(wc, 2)); add_floater(e, -1); break; }  // a miss: in reach but the blow glances off
            int dmg = wyr ? rnd(1, 10) + 5 : 0;
            if (!wyr) for (int k = 0; k < lead.hand_dice; k++) dmg += rnd(1, lead.hand_sides);
            if (!wyr) dmg += lead.hand_bonus;
            dmg += str_bonus(lead);
            if ((wyr || wyr_eq) && (mt.name == "Dragon" || is_draconian(mt.name))) dmg *= 2;   // Wyrmslayer: double against dragons and draconians
            if (lead.hand_name == "Flame Tongue +1") { add_impact(e, 1); if (mt.name == "Troll") { dmg += 3; e.burned = true; } }   // a small flame where it strikes; +4 against trolls, which it burns   // +4 against trolls, and what it kills it burns: no regrowth
            e.hp -= std::max(1, dmg);
            add_floater(e, std::max(1, dmg));
            fx(melee_fx(wc, 1)); add_spark(e);   // the blow lands
            if (e.hp <= 0) {
                if (!e.revived) { xp_ += mt.xp; lead.xp += mt.xp; } kills_++; if (e.type < (int)kills_by_type_.size()) kills_by_type_[e.type]++;   // (a Bozak's bones explode when it dies: see reap_dead)
                if (mt.name == "Baaz" && !wyr_eq && !wyr) {   // the Baaz turns to a metal statue with the blade in it: Dexterity check at -3 or the weapon sticks until the statue crumbles
                    int dex = lead.abilities.size() > 3 ? lead.abilities[3] : 12;
                    if (rnd(1, 20) > dex - 3) hold_hero(baaz_statue_steps(e), 1);
                }
            }
            break;  // one target per swing
        }
        if (!engaged) fx(melee_fx(wc, 0));   // nothing there to hit: a whiff
    }
    if (lead.hand_name == "Wyrmslayer" && steps_ % 8 == 0)   // Wyrmslayer hums when a dragon is near
        for (auto& e : enemies_) if (e.hp > 0 && mtypes_[e.type].name == "Dragon" && abs(e.x - cx_) < 200) { fx(kFxBuzz, 0.6); break; }
    // Enemy behaviour follows fn 5D92's distance bands: touching (<32) strikes, <150 walks at the party, farther away
    // drifts, 384 and more stands still. Their strike uses the original's to-hit roll and damage dice (fn 6310).
    for (auto& e : enemies_) {
        if (e.hp <= 0) continue;
        if (!in_level) e.y = ground_y(e.x) - 16;
        const MonsterType& t = mtypes_[e.type];
        int dx = cx_ - e.x, dy = cy_ - e.y;
        int dist = abs(dy) > 24 ? std::max(abs(dx), abs(dy)) : abs(dx);
        e.striking = false;
        if (e.frozen > 0) { e.frozen--; if (!e.frozen) e.turned = false; continue; }  // held, charmed, asleep or webbed
        if (e.hpmax == 0) e.hpmax = e.hp;
        if (t.name == "Troll" && e.hp > 0 && e.hp < e.hpmax && steps_ % 15 == 0) e.hp++;  // trolls regenerate (cluebook)
        if (e.rcd > 0) e.rcd--;
        if (e.venom > 0) e.venom--;
        if (e.appear > 0) e.appear--;
        {  // ranged attacks (estimates from the cluebook): dragon fire, the bows of Men, Bozak spells
            int range = 0, lo = 0, hi = 0, cd = 0;
            if (t.name == "Dragon") { range = 260; lo = 14; hi = 30; cd = 90; }
            else if (t.name == "Man") { range = 320; lo = 1; hi = 6; cd = 70; }
            else if (t.name == "Bozak") { range = 170; lo = 2; hi = 8; cd = 110; }
            if (range && dist >= t.reach + 16 && dist < range && e.rcd <= 0) {
                e.rcd = cd; e.striking = true;
                int dm = rnd(lo, hi);
                if (lead.hand_name == "Wyrmslayer" && (t.name == "Dragon" || t.name == "Bozak") && rnd(1, 20) + 3 >= 12) dm /= 2;   // Wyrmslayer: +3 on saves against the breath and spells of dragons and draconians
                if (!god()) lead.hp -= dm;
                fx(kFxPcHit); add_spark_hero();
            }
        }
        if (dist >= 384) continue;
        e.left = dx < 0;
        if (dist < t.reach && abs(dy) <= 24) {
            e.striking = true;
            e.anim++;   // the strike animation runs (it used to stand on one frame)
            e.cooldown--;
            if (e.cooldown <= 0 && t.name == "Kapak" && e.venom <= 0) {   // a Kapak licks its blade first: a round to envenom it, good for three more
                e.venom = 24; e.cooldown = 6;
            } else if (e.cooldown <= 0) {
                e.cooldown = attack_period_;
                int idx = std::clamp(eff_armor(lead), 0, (int)armor_table_.size() - 1);
                int need = (armor_table_.empty() ? 10 : armor_table_[idx]) + t.to_hit - 1 + (ward_ > 0 ? ward_pts_ : 0);
                if (rnd(0, 19) >= need) {
                    if (!god()) lead.hp -= std::max(0, rnd(0, std::max(1, t.dmg_sides) - 1) + t.dmg_bonus);
                    fx(kFxPcHit); add_spark_hero();   // a hero is hit
                    if (t.name == "Kapak" && e.venom > 0 && !held()) {   // the venom: unless he saves vs poison the hero is paralysed (2-12 turns, here a few seconds); another hero can take over
                        int con = lead.abilities.size() > 4 ? lead.abilities[4] : 12;
                        if (rnd(1, 20) > con) hold_hero(rnd(2, 12) * 2, 2);
                    }
                }
            }
        } else {
            for (int s = 0; s < t.speed; s++) {
                int mx = abs(dx) > t.reach - 4 ? (dx > 0 ? 1 : -1) : 0, my = in_level ? (abs(dy) > 8 ? (dy > 0 ? 1 : -1) : 0) : 0;
                bool ok = false;
                bool clear = true;  // not on top of the party (they stop at striking distance) and not on top of each other
                int nx = e.x + mx;
                if (mx && abs(nx - cx_) < t.reach - 8) clear = false;
                for (auto& o : enemies_) if (&o != &e && o.hp > 0 && mx && abs(nx - o.x) < 22 && abs(nx - o.x) < abs(e.x - o.x)) clear = false;
                if (clear && mx && (in_level ? can_move(e.x, e.y, mx, 0) : (e.x + mx >= 0 && ground_y(e.x + mx) >= ground_y(e.x) - 16))) { e.x += mx; ok = true; }
                if (my && can_move(e.x, e.y, 0, my)) { e.y += my; ok = true; }
                if (!ok) break;
            }
            e.anim++;
        }
        if (lair_active_) e.x = std::clamp(e.x, lair_x0_ + 12, lair_x1_ - 34);
        if (in_level && t.name == "Dragon" && e.hlx >= 0) e.x = std::clamp(e.x, e.ax - 104, e.ax + 104);   // the head is tied to the body by its neck: it reaches only so far
        if (!in_level && water_at(e.x)) { e.hp = 0; e.nocorpse = true; Floater f; f.x = e.x; f.y = e.y; f.amount = -2; f.height = mt_height(e); floaters_.push_back(f); }   // water is death: they follow you in and drown (cluebook)
    }
    reap_dead();
    step_corpses();
    if (lair_active_) {   // the lair: no automatic return; the loot is in the chest, and Y (once the fight is over) leaves
        victory_steps_ = 0;
        if (enemies_.empty() && !lair_msg_) { lair_msg_ = true; message_ = lair_chest_taken_ ? "The lair is quiet" : "The lair is quiet: find the treasure, or press Y"; message_steps_ = 40; }
    } else if (from_wild_ && enemies_.empty() && (troll_pending() || final_t_ >= 0 || (staff_broken_ && !box_.empty()))) {
        victory_steps_ = 0;   // a troll lies in a heap: it will get up again
    } else if (from_wild_ && enemies_.empty() && thrown_on_strip()) {
        victory_steps_ = 0;   // fired ammunition is still lying out there: stay in the side view until it is picked up (or switch back yourself)
        if (steps_ % 40 == 0) { message_ = "Collect your ammunition"; message_steps_ = 12; }
    } else if (from_wild_ && enemies_.empty()) {
        if (++victory_steps_ >= (turbo_ ? 1 : 14)) { victory_steps_ = 0; from_wild_ = false; toggle_view(); message_ = "Victory"; message_steps_ = 14; give_poi_reward(); return; }
    } else victory_steps_ = 0;
    if (lead.hp <= 0) {
        lead.hp = 0;
        int next = -1;
        for (size_t i = 0; i < party_.size(); i++)
            if (party_[i].hp > 0) { next = (int)i; break; }
        if (next < 0) { mode_ = Mode::GameOver; screen_.pal = kGamePalette; play(13); return; }
        set_leader(next);
        message_ = party_[next].name + " takes the lead";
    }
}

// Updated controls only. The gamepad's A button and the mouse's left button do the same contextual thing: menus and message boxes take them as Enter; in play a foe in reach
// is attacked (held = repeated swings), otherwise the press picks up what lies here, otherwise opens a door or chest. A click on a portrait chooses that hero as leader.
// (The mouse does not walk the party.) Clicks inside menus are handled in step_menu.
void Game::translate_input(Input& in) {
    bool playing = (mode_ == Mode::World || mode_ == Mode::Combat) && box_.empty();
    if (in.start && playing) in.cancel = true;   // Start = pause: opens the main menu (on the map and in the side view; the cancel edge opens it)
    if (in.back && !playing) in.cancel = true;   // B backs out one step in menus, the title pages and message boxes (in play it jumps)
    bool over_panel = in.my >= 128;
    bool click_play = in.mouse_held && in.my >= 0 && in.my < 128 && playing;
    if (in.click && !playing && mode_ != Mode::Menu) in.confirm = true;                // a click goes through message boxes, the title and the end screens
    if (in.click && playing && over_panel)
        for (size_t i = 0; i < slots_.size() && i < party_.size(); i++)
            if (in.mx >= slots_[i].x && in.mx < slots_[i].x + 32 && in.my >= 128 + slots_[i].y && in.my < 128 + slots_[i].y + 32) in.select = (int)i + 1;
    if ((mode_ == Mode::Menu || mode_ == Mode::Title || mode_ == Mode::Help || mode_ == Mode::Debug || !box_.empty()) && in.wheel) { pend_dir_[in.wheel < 0 ? 0 : 1] = true; }  // the wheel scrolls menu lists
    bool act = in.act || click_play;
    if (act) {
        if (!playing) { if (in.act) in.confirm = true; }
        else {
            // Priority: a foe in reach is always attacked. Otherwise, on the press, what lies within reach is picked up, else a door or chest here is used;
            // if there is nothing to take or use, A just swings the sword.
            bool foe = foe_in_reach(true);
            if (!foe && !act_prev_) act_used_ = context_action();
            if (!act_prev_ && foe) act_used_ = false;
            if (!act_used_) { in.attack = true; pending_attack_ = true; }
        }
    } else act_used_ = false;
    act_prev_ = act;
}

bool Game::frame(Platform& p) {
    if (!plat_) {
        plat_ = &p;
        p.attach_audio(&sound_);
        play_title();  // the title music, as main does at start-up (game.exe 3FC0)
        if (const char* pg = getenv("DOF_PAGE")) if (mode_ == Mode::Title) set_title_page(atoi(pg));   // test hook: start on that title page (2 = credits)
    }
    if (quit_) return false;
    render_frames_++;
    update_music();
    if (mode_ == Mode::Title) { title_frames_++; title_page_frames_++; }
    p.set_pad_enabled(!orig_controls_);
    Input in = p.poll();
    if (orig_controls_ && in.qkey) { in.jump = 0; pending_map_ = true; }   // original controls: Q is the quest map (a jump with the updated ones)
    if (in.debug) { if (mode_ == Mode::Debug) mode_ = debug_return_; else if (mode_ == Mode::World || mode_ == Mode::Combat) { debug_return_ = mode_; mode_ = Mode::Debug; debug_sel_ = 0; screen_.pal = kGamePalette; } }
    if (in.help) { if (mode_ == Mode::Help) help_close_ = true; else if (mode_ != Mode::Ending && mode_ != Mode::GameOver) open_help(); }
    if (in.quit) return false;
    if (in.mute && mode_ != Mode::Ending) {   // (V is a letter in the name for the high scores)
        p.lock_audio();
        sound_.set_muted(!sound_.muted());
        p.unlock_audio();
    }
    if (!orig_controls_) translate_input(in);
    double now = p.now_ms();
    if (last_ms_ < 0) last_ms_ = now;
    double dt_ms = std::min(now - last_ms_, 250.0);
    acc_ms_ += dt_ms;
    if (mode_ == Mode::Combat || (mode_ == Mode::World && interior())) { if (!missiles_.empty()) update_missiles(std::min(dt_ms, 100.0)); if (!floaters_.empty() || !sparks_.empty() || !bolts_.empty() || !bursts_.empty() || !impacts_.empty() || !slabs_.empty() || final_t_ >= 0 || lightning_.t >= 0) update_floaters(std::min(dt_ms, 100.0)); }
    last_ms_ = now;
    double step_ms = 1000.0 / rules_.tick_hz * rules_.ticks_per_step;
    if (turbo_) step_ms /= 8;  // tool-assisted mode: the logic runs eight times faster
    pending_any_ = pending_any_ || in.any || in.confirm;  // a tap between two logic steps must not be lost
    if (mode_ == Mode::Ending) { pend_text_ += in.text; if (in.backspace) pend_back_++; } else { pend_text_.clear(); pend_back_ = 0; }
    pending_toggle_ = pending_toggle_ || in.toggle;
    pending_rest_ = pending_rest_ || in.rest;
    {  // key presses (edges) between two logic steps are latched so a short tap is never lost in menus
        bool cur[6] = {in.up, in.down, in.left, in.right, in.cancel, in.confirm};
        for (int k = 0; k < 6; k++) { if (cur[k] && !fprev_[k]) pend_dir_[k] = true; fprev_[k] = cur[k]; }
    }
    pending_confirm_ = pending_confirm_ || in.confirm;
    if (in.select) pending_select_ = in.select;
    if (in.cycle) pending_cycle_ = in.cycle;
    if (in.map && !orig_controls_) pending_map_ = true;
    if (in.start) pending_start_ = true;
    if (in.ranged && !orig_controls_) pending_ranged_ = true;
    if (!orig_controls_ && !turbo_) frame_move(in, dt_ms);  // updated controls: the party moves every frame, not every logic step
    pending_save_ = pending_save_ || in.save;
    pending_load_ = pending_load_ || in.load;
    if (in.level) pending_level_ = in.level;
    if (in.jump) pending_jump_ = in.jump;
    if (in.click) { pend_click_ = true; click_x_ = in.mx; click_y_ = in.my; }
    while (acc_ms_ >= step_ms) {
        Input use = in;
        use.any = pending_any_;
        use.toggle = pending_toggle_;
        use.select = pending_select_;
        use.save = pending_save_;
        use.cycle = pending_cycle_; pending_cycle_ = 0;
        use.map = pending_map_; pending_map_ = false;
        use.start = pending_start_; pending_start_ = false;
        use.load = pending_load_;
        use.level = pending_level_;
        use.jump = pending_jump_; pending_jump_ = 0;
        use.click = pend_click_; if (pend_click_) { use.mx = click_x_; use.my = click_y_; } pend_click_ = false;
        use.rest = pending_rest_;
        if (pending_attack_) { use.attack = true; pending_attack_ = false; }
        if (!orig_controls_) { use.ranged = in.ranged || pending_ranged_; pending_ranged_ = false; } else use.ranged = false;
        if (mode_ == Mode::Menu) { use.up = pend_dir_[0]; use.down = pend_dir_[1]; use.left = pend_dir_[2]; use.right = pend_dir_[3]; use.cancel = pend_dir_[4]; use.confirm = pend_dir_[5]; menu_edges_ = true; } else menu_edges_ = false;
        if (mode_ != Mode::Menu && pend_dir_[4]) use.cancel = true;   // a tap of Esc / the pad's Start between two logic steps still opens the menu
        for (int k = 0; k < 6; k++) { edges_[k] = pend_dir_[k]; pend_dir_[k] = false; }  // edges seen since the last step
        if (menu_edges_) {}
        pending_rest_ = false;
        if (mode_ != Mode::Menu) use.confirm = in.confirm || pending_confirm_;   // (in menus confirm is the single press set above: a held key used to count on every logic step)
        pending_confirm_ = false;
        pending_level_ = 0;
        pending_save_ = pending_load_ = false;
        pending_any_ = false;
        pending_select_ = 0;
        if (mode_ != Mode::Title) pending_toggle_ = false;  // keep a toggle pressed on the title for later
        step(use);
        apply_god();
        acc_ms_ -= step_ms;
    }
    switch (mode_) {
        case Mode::Title: draw_title(); break;
        case Mode::World: if (interior() && itable_loaded_) draw_interior(); else draw_world(); break;
        case Mode::Combat: draw_combat(); break;
        case Mode::GameOver: draw_gameover(); break;
        case Mode::Menu: draw_menu(); break;
        case Mode::Ending: draw_ending(); break;
        case Mode::Help: draw_help(); break;
        case Mode::Debug: draw_debug(); break;
    }
    if (mode_ != Mode::Title && mode_ != Mode::Ending && mode_ != Mode::Help && mode_ != Mode::Debug) draw_box();
    p.present(screen_);
    return true;
}

void Game::draw_gameover() {
    screen_.reset_clip();
    screen_.clear(0);
    screen_.text(88, 84, "THE PARTY HAS FALLEN", 12);
    char b[64];
    snprintf(b, sizeof b, "Experience: %d   Foes slain: %d", xp_, kills_);
    screen_.text(72, 108, b, 7);
    screen_.text(96, 140, "Press any key", 8);
}

static void hp_bar(Screen& s, int x, int y, int h, int hp, int hp_max, uint8_t colour) {
    s.fill_rect(x, y, 3, h, 0);
    if (hp_max <= 0 || hp <= 0) return;
    int fill = std::max(1, h * hp / hp_max);
    s.fill_rect(x, y + h - fill, 3, fill, colour);
}

void Game::draw_panel() {
    const int vh = 128;
    screen_.reset_clip();
    screen_.blit(panel_, 0, vh);
    // The panel picture only has the outline; the original's panel is granite with a compass cell and ten slots (bar well, portrait box).
    // Everything between the top and bottom edge is repainted here, matched against screenshots of the original.
    auto granite = [&](int x, int y) { return panel_.px[(40 + (y % 28)) * 320 + 14 + (x % 20)]; };  // plain granite of the picture (rows 40-67, x 14-33, clear of its frame lines)
    for (int y = 2; y < 70; y++) for (int x = 0; x < kW; x++) screen_.px[(vh + y) * kW + x] = granite(x, y);
    for (int y = 2; y < 70; y++) screen_.px[(vh + y) * kW + 3] = 7;               // left edge of the compass cell
    for (int x = 3; x <= 45; x++) { screen_.px[(vh + 2) * kW + x] = 7; screen_.px[(vh + 69) * kW + x] = 7; }  // its top and bottom edges
    for (int k = 0; k < 5; k++) for (int y = 2; y < 70; y++) screen_.px[(vh + y) * kW + 45 + 56 * k] = 3;  // bright line left of each column of slots
    for (size_t i = 0; i < slots_.size(); i++) {  // ten identical slots; the ones nobody occupies are grey
        int x = slots_[i].x, y = vh + slots_[i].y;
        // bar well: black with a thin grey frame
        screen_.fill_rect(x - 17, y - 1, 11, 33, 7);
        screen_.fill_rect(x - 16, y, 9, 31, 0);
        // portrait box: black, with the frame showing on its right and lower side
        screen_.fill_rect(x, y, 32, 32, 0);
        screen_.fill_rect(x + 32, y, 1, 33, 7);
        screen_.fill_rect(x, y + 32, 33, 1, 7);
        if (i >= party_.size()) { screen_.fill_rect(x, y, 32, 32, 8); continue; }
        const Hero& h = party_[i];
        int cl = h.clip;
        if (cl >= 0 && cl < (int)clips_.size()) screen_.blit(clips_[cl], x, y, true);
        // green: hit points, red when the hero is badly hurt (cluebook: "his damage bar turns red"); yellow: a second value nobody has identified
        uint8_t hp_col = h.hp * 3 < h.hp_max ? 5 : 13;
        hp_bar(screen_, x - 15, y + 2, 28, h.hp, h.hp_max, hp_col);
        hp_bar(screen_, x - 11, y + 2, 28, h.hp > 0 ? (int)(bar2_[i < bar2_.size() ? i : 0] * 100) : 0, 100, 15);
        if ((int)i == leader_) screen_.fill_rect(x - 15, y + 30, 7, 1, 15);  // the leader
        if (h.hp <= 0 && 20 < (int)clips_.size()) screen_.blit(clips_[20], x, y);  // the dead show the dithered portrait
    }
    if (mode_ == Mode::Menu && (menu_page_ == kPageEquip || menu_page_ == kPageEquipPick || menu_page_ == kPageInventory)) {
        // Equip and Inventory: every portrait but the chosen hero's is greyed, his has a border that pulses
        for (size_t i = 0; i < slots_.size() && i < party_.size(); i++) {
            int x = slots_[i].x, y = vh + slots_[i].y;
            if ((int)i != menu_hero_) { dim_rect(x, y, 32, 32, 1); continue; }
            uint8_t col = (render_frames_ / 12) % 2 ? 15 : 3;
            for (int k = 0; k < 2; k++) {
                screen_.fill_rect(x - 2 + k, y - 2 + k, 36 - 2 * k, 1, col); screen_.fill_rect(x - 2 + k, y + 33 - k, 36 - 2 * k, 1, col);
                screen_.fill_rect(x - 2 + k, y - 2 + k, 1, 36 - 2 * k, col); screen_.fill_rect(x + 33 - k, y - 2 + k, 1, 36 - 2 * k, col);
            }
        }
    }
    {  // the compass: clips 12-15 are the star with its north, west, south or east point red; the red point is the way the party faces
        int dir;  // 0 north, 1 west, 2 south, 3 east
        bool combat_like = mode_ == Mode::Combat || (interior() && mode_ != Mode::Title);
        if (combat_like) {
            bool ns = interior() ? iv_ns_ : !east_west_;
            dir = ns ? (face_left_ ? 2 : 0) : (face_left_ ? 1 : 3);  // along a north/south strip right is north, left south
        } else dir = facing_ == 1 ? 0 : facing_ == 2 ? 2 : (face_left_ ? 1 : 3);
        {  // crossed swords under the compass: bright when a foe is within reach of the hand weapon, dark otherwise (my reading of the manual)
            bool near_foe = false;
            for (auto& e : enemies_) if (e.hp > 0 && abs(e.x - (interior() ? ix_ : cx_)) < 48) near_foe = true;
            int sw = (mode_ == Mode::Combat || interior()) && near_foe ? 16 : 17;
            if (sw < (int)clips_.size()) screen_.blit(clips_[sw], 0, vh + 44, true);
        }
        int ci = compass_clip_ + dir;
        if (compass_clip_ >= 0 && ci < (int)clips_.size()) screen_.blit(clips_[ci], compass_.x, vh + compass_.y, true);
    }
}

// Direction index of the mpl figures (game.exe table at data 0x19e0): 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW.
static int dir8(int dx, int dy) {
    static const int tab[3][3] = {{7, 0, 1}, {6, 4, 2}, {5, 4, 3}};  // [dy+1][dx+1]; (0,0) keeps facing south
    return tab[std::clamp(dy, -1, 1) + 1][std::clamp(dx, -1, 1) + 1];
}

void Game::draw_world() {
    screen_.reset_clip();
    screen_.clear(0);
    const int vh = 128;
    const bool in_ = interior();
    int wx = in_ ? ix_ : px_, wy = in_ ? iy_ : py_;
    int mw = in_ ? levels_[level_].w * 128 : world_.width_px(), mh = in_ ? levels_[level_].h * 128 : world_.height_px();
    int ax0 = 0, ay0 = 0, ax1 = mw, ay1 = mh;
    if (!in_) world_.play_area(&ax0, &ay0, &ax1, &ay1);  // the camera stops at the edge of the playable map instead of showing the grey ring round it
    int cx = std::clamp(wx - 160, ax0, std::max(ax0, ax1 - kW));  // game.exe 4DDA: the view starts 160 px left of and 64 px above the party; figures are drawn 2 px lower
    int cy = std::clamp(wy - 64, ay0, std::max(ay0, ay1 - vh));
    screen_.set_clip(0, 0, kW, vh);
    for (int ty = cy >> 4; ty <= (cy + vh - 1) >> 4; ty++)
        for (int tx = cx >> 4; tx <= (cx + kW - 1) >> 4; tx++) {
            if (in_) {
                int t = level_tile(tx * 16, ty * 16);
                if (t >= 1 && t <= (int)utiles_.size()) screen_.blit(utiles_[t - 1], tx * 16 - cx, ty * 16 - cy);
            } else {
                int t = world_.tile_at(tx * 16, ty * 16);
                if (t >= 1 && t <= (int)wtiles_.size()) screen_.blit(wtiles_[t - 1], tx * 16 - cx, ty * 16 - cy);  // tile numbers are 1-based
            }
        }
    if (!in_ && !pois_loaded_) load_pois();
    if (!in_) {   // the places of interest: a little picture for each
        for (auto& p : pois_) {
            if (p.level != 1) continue;
            int sx = p.x - cx, sy = p.y - cy;
            if (sx < -20 || sx > kW + 4 || sy < -8 || sy > vh + 4) continue;
            draw_poi_icon(p, sx, sy);
        }
    }
    if (!in_ && entrance_known_) {  // a red-and-yellow X marks the way into Sla-Mori on the map, once the party knows where it is
        for (auto& l : links_) {
            if (l.from != 1 || l.mode != 3) continue;
            int sx = l.x1 / 8 + 16 - cx, sy = l.y1 / 8 - cy;
            if (sx < -16 || sx > kW || sy < -16 || sy > vh) continue;
            uint8_t in_col = (render_frames_ / 15) % 2 ? 3 : 15;
            for (int i = 1; i < 15; i++)
                for (int k = 0; k < 2; k++) {
                    int xs[2] = {sx + i, sx + 15 - i}, ys[2] = {sy + i, sy + i};
                    for (int d = 0; d < 2; d++) {
                        int xx = xs[d] + (k ? 1 : 0), yy = ys[d];
                        if (xx >= 0 && xx < kW && yy >= 0 && yy < vh) screen_.px[yy * kW + xx] = k ? 5 : in_col;
                    }
                }
        }
    }
    for (auto& it : items_) {  // items lie about as small squares (manual), chests a little larger
        if (it.taken || (it.id >= 56 && it.id <= 59) || item_hidden(it) || it.in_chest >= 0 || it.level != level_) continue;
        int ix = in_ ? it.x : it.x / 8, iy = in_ ? it.y : it.y / 8;
        bool chest = it.id >= 43 && it.id <= 46;
        if (chest) { const Image& ci = it.opened ? chest_open_ : chest_closed_; screen_.blit(ci, ix - cx, iy - cy + 11 - ci.h, true); continue; }
        int w = 6, hh = 6, x = ix - cx + 3, y = iy - cy + 8;
        screen_.fill_rect(x, y, w, hh, 0);
        screen_.fill_rect(x + 1, y + 1, w - 2, hh - 2, 15);
    }
    auto figure = [&](const std::vector<Image>& fs, int frame, int view, int x, int y, bool flip) {
        size_t f = (size_t)(frame % 8) * 3 + view;
        if (f < fs.size()) screen_.blit(fs[f], x, y, true, flip);
    };
    if (!in_)  // the creatures of the wilderness are the small mpl figures
        for (auto& w : wild_) {
            const auto& fs = w.npc ? figs_ : figs2_;  // people are the blue mpl1 figures, monsters the green mpl2 ones
            if (w.dx || w.dy) w.face = dir8(w.dx, w.dy);
            static const int seq[4] = {0, 1, 0, 2};
            figure(fs, w.face, (w.dx || w.dy) ? seq[(w.anim / 3) % 4] : 0, w.x - cx, w.y - cy + 2, false);
        }
    if (in_)
        for (auto& c : corpses_) {
            const MonsterType& t = mtypes_[c.e.type];
            int dl; Image im = corpse_image(c, dl);
            if (dl > 0) screen_.blit_dither(im, c.e.x - cx - 8 - (im.w - 32) / 2, c.e.y - cy + 16 - t.actor.height, c.e.left, dl);
        }
    if (in_)  // creatures of an interior fight on the spot, drawn as in the combat view
        for (auto& e : enemies_) {
            const MonsterType& t = mtypes_[e.type];
            const auto& seq = e.striking ? t.attack : t.walk;
            size_t fr = seq.empty() ? 0 : (size_t)seq[(e.striking ? e.anim / 4 : e.anim) % seq.size()];
            if (fr >= t.actor.frames.size()) fr = 0;
            screen_.blit(t.actor.frames[fr], e.x - cx - 8 - (t.actor.frames[fr].w - 32) / 2, e.y - cy + 16 - t.actor.height, true, e.left);
        }
    {  // the party: mpl0. The eight frames are the eight directions (clockwise from north); the three views are the walking steps
        static const int seq[4] = {0, 1, 0, 2};
        figure(figs0_, dir8(move_dx_, move_dy_), anim_ ? seq[(anim_ / 2) % 4] : 0, wx - cx, wy - cy + 2, false);
    }
    if (message_steps_ > 0) screen_.text(4, 4, message_.c_str(), 15);
    draw_panel();
}

static void wrap_text(Screen& s, int x, int y, const std::string& t, int cols, int maxlines, int colour, int skip, int step) {
    std::string line; int row = 0; size_t i = 0;
    auto flush = [&]() { if (row >= skip && row - skip < maxlines) s.text(x, y + (row - skip) * step, line.c_str(), colour); row++; line.clear(); };
    while (i < t.size()) {
        size_t e = t.find(' ', i); if (e == std::string::npos) e = t.size();
        std::string w = t.substr(i, e - i);
        if ((int)(line.size() + w.size() + 1) > cols && !line.empty()) flush();
        if (!line.empty()) line += ' ';
        line += w; i = e + 1;
    }
    if (!line.empty()) flush();
}

// The main menu (Space or Enter in the wilderness, Esc in combat), laid out like the original's: a scroll with eight lines showing at a time.
static const char* kMenu[] = {"Exit menu", "Hero select", "Clerical spells", "Magic user spells", "Use", "Rest", "Take", "Open", "Give",
                              "Inspect", "Drop", "Put", "Shut", "Quest map", "Save game", "Restore game", "Xp", "Settings", "Exit"};
static const int kMenuN = 19;
struct SpellInfo { const char* name; char mark; int cost; };
// The cleric's list as the original shows it (strings at data 0x1414-0x14f5): each memorized spell is its own entry (three Cure light wounds,
// two Hold person, two Dispel Magic); spent ones move under a "--- used ---" line. Kinds: 0 CLW, 1 Protection, 2 Find traps, 3 Hold person,
// 4 Spiritual hammer, 5 Prayer, 6 Bless, 7 Dispel, 8 Turn undead, 9 Cure critical, 10 Raise dead, 11 Heal.
static const SpellInfo kClericalKinds[] = {{"Cure light wounds", '#', 0}, {"Protection from evil", '#', 0}, {"Find traps", '#', 0}, {"Hold person", '#', 0},
    {"Spiritual hammer", '#', 0}, {"Prayer", '#', 0}, {"Bless", '#', 0}, {"Dispel magic", '#', 0}, {"Turn undead", '*', 0},
    {"Cure critical wounds", 'o', 2}, {"Raise dead", 'o', 10}, {"Heal", 'o', 5}};
static const int kClericalEntry[16] = {0, 0, 0, 1, 2, 3, 3, 4, 5, 6, 5, 5, 8, 9, 10, 11};   // (the two Dispel Magic entries are more Prayers)
#define kClerical kClericalKinds
static const SpellInfo kMagic[] = {{"Charm", ' ', 1}, {"Sleep", ' ', 1}, {"Magic missile", ' ', 1}, {"Web", ' ', 2}, {"Magic missile x3", ' ', 2},
    {"Detect treasure", ' ', 2}, {"Burning hands", ' ', 1}, {"Fire Ball", ' ', 3}, {"Final strike", ' ', 100}};

// Spell effects. Healing works anywhere; the attack and hold spells need enemies in sight (combat view). The spell lists, charges and which
// spells are memorized come from the manual; damage numbers and durations are estimates.
void Game::cast(int school, int spell) {
    const Hero& caster = party_[leader_];
    (void)caster;
    auto nearest = [&]() -> Enemy* {
        Enemy* best = nullptr; int bd = 1 << 30;
        for (auto& e : enemies_) { int d = abs(e.x - (interior() ? ix_ : cx_)); if (e.hp > 0 && d < bd) { bd = d; best = &e; } }
        return best;
    };
    auto hit = [&](Enemy& e, int dmg) {
        e.hp -= dmg;
        add_spark(e);
        add_floater(e, dmg);
        if (e.hp <= 0) { const MonsterType& mt = mtypes_[e.type]; if (!e.revived) xp_ += mt.xp; kills_++; if (e.type < (int)kills_by_type_.size()) kills_by_type_[e.type]++; }
    };
    bool fighting = false;   // a foe within sight
    for (auto& en : enemies_) if (en.hp > 0 && abs(en.x - (interior() ? istrip_x() : cx_)) < 175) fighting = true;
    const size_t f0 = floaters_.size(), s0 = sparks_.size(), c0 = corpses_.size();
    struct Snap { Enemy* e; int hp, frozen; };
    std::vector<Snap> snaps;   // what each foe was before the spell, to see afterwards whom it touched
    for (auto& en : enemies_) snaps.push_back({&en, en.hp, en.frozen});
    if (school == 0) {  // clerical: `spell` is the entry number in the 16-entry list
        int entry = spell;
        if (entry < 12 && used_[entry]) { box_ = "That spell has been\\ncast already.\\nRest to regain it."; return; }
        spell = kClericalEntry[entry];
        int cost = kClerical[spell].cost;
        if (kClerical[spell].mark == 'o' && disks_ < cost) { box_ = "Disks of Mishakal is\\nexhausted"; return; }
        switch (spell) {
            case 0: apply_heal(rnd(1, 8), false); break;
            case 1: ward_ = 800; ward_kind_ = 1; ward_pts_ = 2; break;  // Protection from Evil: -2 to evil monsters' attacks
            case 3: {  // Hold Person: up to three humanoids stand frozen
                if (!fighting) { box_ = "There is nothing to\\nhold here."; return; }
                int held = 0;
                for (auto& en : enemies_) { const std::string& n = mtypes_[en.type].name; if (held < 3 && en.hp > 0 && (n == "Baaz" || n == "Bozak" || n == "Kapak" || n == "Goblin" || n == "Hobgoblin" || n == "Man" || n == "Aghar")) { en.frozen = 150; held++; } }
                if (!held) { box_ = "Nothing is held."; return; }
                break; }
            case 4: { Enemy* e = nearest(); if (!fighting || !e) { box_ = "There is nothing to\\nstrike here."; return; } hit(*e, rnd(2, 12)); break; }
            case 5: ward_ = 600; ward_kind_ = 2; ward_pts_ = std::max(ward_pts_, 1); bless_ = 600; break;  // Prayer: +1 for the party, -1 for the foes
            case 6: bless_ = 800; break;                                                 // Bless: +1 to hit
            case 2: reveal_ = 1200; box_ = "Traps glitter on the\\nfloor."; break;
            case 8: fx(kFxTurn); for (auto& e : enemies_) if (e.hp > 0 && is_undead(mtypes_[e.type].name)) { if (mtypes_[e.type].name == "Zombie") hit(e, 1000); else { e.frozen = 250; e.turned = true; } } break;  // Turn undead freezes any undead (it is an ability, never used up); they neither flee nor die
            case 9: apply_heal(rnd(3, 24), false); break;
            case 10: apply_heal(0, true); break;
            case 11: apply_heal(999, false); break;
            default: box_ = "Nothing happens."; return;
        }
        if (kClerical[spell].mark == 'o') disks_ -= cost;
        if (entry < 12) used_[entry] = true;
    } else {  // magic user: charges of the Staff of Magius
        if (staff_broken_) { box_ = "The Staff of Magius is\nbroken and is gone."; return; }
        int cost = kMagic[spell].cost == 100 ? staff_ : kMagic[spell].cost;
        if (staff_ < cost || staff_ <= 0) { box_ = "The Staff of Magius has\\nno power at present"; return; }
        Enemy* e = nearest();
        switch (spell) {
            case 0: {  // Charm: humanoids only
                if (!fighting || !e) { box_ = "There is nothing to\\naffect here."; return; }
                int n = 0; for (auto& en : enemies_) { const std::string& nm = mtypes_[en.type].name; if (en.hp > 0 && n < 2 && (nm == "Baaz" || nm == "Bozak" || nm == "Kapak" || nm == "Goblin" || nm == "Hobgoblin" || nm == "Man" || nm == "Aghar")) { en.frozen = 200; n++; } }
                if (!n) { box_ = "Nothing is charmed."; return; }
                break; }
            case 1: {  // Sleep: creatures of up to 4 hit dice, not the undead
                if (!fighting || !e) { box_ = "There is nothing to\\naffect here."; return; }
                int n = 0; for (auto& en : enemies_) { const MonsterType& m2 = mtypes_[en.type]; if (en.hp > 0 && n < rnd(2, 8) && m2.hp_dice <= 4 && m2.name != "Zombie" && m2.name != "Wight" && m2.name != "Wraith" && m2.name != "Dragon") { en.frozen = 220; n++; } }
                if (!n) { box_ = "Nothing falls asleep."; return; }
                break; }
            case 3: { if (!fighting || !e) { box_ = "There is nothing to\\naffect here."; return; } e->frozen = mtypes_[e->type].hp_dice >= 6 ? 25 : 100; break; }  // Web: strong monsters break out quickly
            case 2: if (!fighting || !e) { box_ = "There is nothing to\\nstrike here."; return; } hit(*e, rnd(1, 4) + 1 + rnd(1, 4) + 1); break;  // two magic missiles, d4+1 each
            case 6: if (!fighting) { box_ = "There is nothing to\\nburn here."; return; } for (auto& en : enemies_) if (abs(en.x - (interior() ? ix_ : cx_)) < 90) { en.burned = true; hit(en, rnd(1, 6) + rnd(1, 6)); } break;
            case 7: if (!fighting) { box_ = "There is nothing to\\nburn here."; return; } for (auto& en : enemies_) if (abs(en.x - (interior() ? ix_ : cx_)) < 260) { en.burned = true; hit(en, rnd(1, 6) + rnd(1, 6) + rnd(1, 6) + rnd(1, 6) + rnd(1, 6)); } break;  // Fireball: 5d6
            case 8:   // the Final Strike: one blast for everything in the view, as many points of damage as the staff has charges left; it breaks the staff and kills Raistlin (Raise Dead brings him back, but not the staff)
                for (auto& en : enemies_) { en.burned = true; hit(en, cost); }
                for (auto& h : party_) if (h.name.rfind("Raistlin", 0) == 0) h.hp = 0;
                staff_broken_ = true;   // (the message comes when the blast has died away)
                break;
            case 4: {   // three magic missiles, one for each of the nearest three foes (all at the nearest if there are fewer), d4+1 each
                if (!fighting || !e) { box_ = "There is nothing to\\nstrike here."; return; }
                std::vector<Enemy*> order;
                for (auto& en : enemies_) if (en.hp > 0) order.push_back(&en);
                std::sort(order.begin(), order.end(), [&](Enemy* a, Enemy* b) { int hx = interior() ? ix_ : cx_; return abs(a->x - hx) < abs(b->x - hx); });
                for (int k = 0; k < 3; k++) { Enemy* t = order[k < (int)order.size() ? k : 0]; if (t->hp > 0 || k >= (int)order.size()) hit(*t, rnd(1, 4) + 1); }
                break; }
            case 5: detect_ = 1800; reveal_ = 1200; box_ = "Hidden treasure and\\ntraps glitter."; break;   // Detect treasure: the invisible things show for 100 seconds
            default: box_ = "Nothing is revealed."; return;
        }
        staff_ -= cost;
    }
    {   // sound and sprites: each spell has its own; whoever the spell touched gets the flying sprite (bolt) and its burst
        std::vector<Enemy*> touched;
        for (auto& s : snaps) if (s.e->hp < s.hp || s.e->frozen > s.frozen) touched.push_back(s.e);
        if (school == 0) {
            switch (spell) {
                case 0: case 9: case 10: case 11: fx(kFxCure); break;           // every cure sounds the same
                case 1: fx(kFxProtection); break;
                case 2: fx(kFxFindTraps); break;
                case 3: fx(kFxHold); launch_spell(18, 19, touched, false); break;
                case 4: fx(kFxHammer); launch_spell(16, 17, touched, false, -1, 4); break;
                case 5: fx(kFxPrayer); break;
                case 6: fx(kFxBless); break;
                case 7: fx(kFxDispel); break;
                case 8: launch_spell(30, 31, touched, false); break;             // (Turn Undead's sound is played where it is cast)
                default: break;
            }
        } else {
            switch (spell) {
                case 0: fx(kFxCharm); launch_spell(18, 19, touched, false); break;
                case 1: fx(kFxSleep); launch_spell(20, 21, touched, false); break;
                case 2: fx(kFxMagicMissile); launch_spell(28, 29, touched, false, -1, 0); break;
                case 3: fx(kFxWeb); launch_spell(24, 25, touched, false); break;
                case 4: fx(kFxMagicMissile); launch_spell(28, 29, touched, false, -1, 0); break;
                case 5: fx(kFxDetectInvisible); break;
                case 6: fx(kFxBurningHands); launch_spell(26, 27, touched, false, -1, 1); break;
                case 7: fx(kFxFireball); launch_spell(22, 23, touched, false, kFxFireballBoom, 2); break;
                case 8: fx(kFxFinalStrike); final_t_ = 0; break;
                default: break;
            }
        }
    }
    reap_dead();
    {   // the damage shows when the spell arrives (not for the Final Strike, which is everywhere at once)
        bool flies = school == 0 ? (spell == 3 || spell == 4 || spell == 8) : (spell <= 4 || spell == 6 || spell == 7);
        if (flies) delay_effects(f0, s0, c0, interior() ? istrip_x() : cx_);
    }
}

// The Lightning Bolt scroll: a bolt from the caster through every foe on the side of the nearest one, off the edge of the view; each takes 6d6 and is stunned.
bool Game::cast_lightning() {
    int hx = interior() ? istrip_x() : cx_;
    Enemy* near = nullptr; int bd = 1 << 30;
    for (auto& e : enemies_) { int d = abs(e.x - hx); if (e.hp > 0 && d < bd) { bd = d; near = &e; } }
    if (!near) return false;
    bool left = near->x < hx;
    const size_t f1 = floaters_.size(), s1 = sparks_.size();
    for (auto& e : enemies_) {
        if (e.hp <= 0 || (left ? e.x > hx : e.x < hx)) continue;
        damage_enemy(e, rnd(1, 6) + rnd(1, 6) + rnd(1, 6) + rnd(1, 6) + rnd(1, 6) + rnd(1, 6));
        add_impact(e, 3, std::abs(e.x - hx) / 640.0 * 0.14);   // a zap on each foe as the bolt reaches it
        if (e.hp > 0) e.frozen = std::max(e.frozen, 40);   // they stop, struck
    }
    for (size_t i = f1; i < floaters_.size(); i++) floaters_[i].t = -std::abs(floaters_[i].x - hx) / 640.0 * 0.14;
    for (size_t i = s1; i < sparks_.size(); i++) sparks_[i].t = -std::abs(sparks_[i].x - hx) / 640.0 * 0.14 / 0.3;
    lightning_.x0 = hx; lightning_.y = interior() ? 0 : cy_; lightning_.t = 0; lightning_.left = left;
    face_left_ = left;
    fx(kFxLightning);
    const size_t c1 = corpses_.size();
    reap_dead();
    for (size_t i = c1; i < corpses_.size(); i++) corpses_[i].t = -(int)std::lround(std::abs(corpses_[i].e.x - hx) / 640.0 * 0.84);   // (the bolt takes 0.14 s: a step is a sixth of a second)
    return true;
}

// Doors: a closed door cell and its open twin (cell values; each is drawn per axis by the cell tables). Open/Shut swap them.
static int door_open_of(int v) { switch (v) { case 23: case 91: return 24; case 26: return 27; case 35: return 36; case 38: return 39; case 47: return 48; case 50: return 51; default: return 0; } }
static int door_shut_of(int v) { switch (v) { case 24: return 23; case 27: return 26; case 36: return 35; case 39: return 38; case 48: return 47; case 51: return 50; default: return 0; } }

// Rows of the cleric list: kind 0 = "Exit menu", 1 = available spell, 2 = the "--- used ---" line, 3 = spent spell.
static std::vector<std::pair<int, int>> cleric_rows(const bool* used) {
    std::vector<std::pair<int, int>> rows{{-1, 0}};
    for (int e = 0; e < 16; e++) if (!(e < 12 && used[e])) rows.push_back({e, 1});
    bool any_used = false;
    for (int e = 0; e < 12; e++) any_used |= used[e];
    if (any_used) {
        rows.push_back({-1, 2});
        for (int e = 0; e < 12; e++) if (used[e]) rows.push_back({e, 3});
    }
    return rows;
}

bool Game::take_item(int idx, bool from_menu) {
    GroundItem& g = items_[idx];
    const std::string who = party_[leader_].name;
    auto stop = [&](int msg) { show_box(msg); if (from_menu) mode_ = menu_return_; return false; };
    if (inv_.size() >= kPackMax) return stop(4);                                       // "You cannot carry any more items"
    if (g.id == 1 && who.rfind("Goldmoon", 0) != 0) return stop(7);              // the Disks burn anyone but Goldmoon
    if (g.id == 2 && who.rfind("Raistlin", 0) != 0) return stop(6);              // the Staff burns anyone but Raistlin
    if (!orig_controls_) {   // updated controls: the shared pack hands it to whoever can use it
        std::string m;
        if (auto_equip(g.id, m, g.thrown ? 1 : -1)) {
            g.taken = true;
            if (g.id < (int)item_values_.size() && item_values_[g.id] > 0) { party_[leader_].xp += item_values_[g.id]; xp_ += item_values_[g.id]; item_values_[g.id] = 0; }
            if (g.id == 19) { quest_flags_ &= ~4; show_box(20); box_ += "\n\n" + m; fx(kFxFanfare); if (from_menu) mode_ = menu_return_; }   // Wyrmslayer: a portion of the quest, and who took it
            else if (from_menu) { box_ = m; mode_ = menu_return_; } else { message_ = m; message_steps_ = 40; }
            return true;
        }
    }
    inv_.push_back(g.id); g.taken = true;
    if (g.id < (int)item_values_.size() && item_values_[g.id] > 0) {  // experience for the find, once (data 0x3050)
        party_[leader_].xp += item_values_[g.id]; xp_ += item_values_[g.id]; item_values_[g.id] = 0;
    }
    if (g.id == 19) { quest_flags_ &= ~4; show_box(20); fx(kFxFanfare); if (from_menu) mode_ = menu_return_; }  // Wyrmslayer: "a portion of your quest", with a fanfare
    else if (!from_menu) { message_ = "Took " + item_name(g.id); message_steps_ = 14; }
    return true;
}

bool Game::open_or_shut(bool open, std::string& msg) {
    if (interior()) {
        Level& L = levels_[level_];
        int cx = ix_ >> 7, cy = iy_ >> 7;
        // Doors can be in the cell the party stands in, in the one ahead along the strip, in the two beside it (a door in the background wall, a door "below") or behind.
        int ahead = face_left_ ? -1 : 1;
        int ax = iv_ns_ ? 0 : ahead, ay = iv_ns_ ? -ahead : 0;   // along a north-south strip right is north
        const int cand[5][2] = {{cx, cy}, {cx + ax, cy + ay}, {cx + ay, cy + ax}, {cx - ay, cy - ax}, {cx - ax, cy - ay}};
        bool false_door = false;
        int k = -1;
        const int hero_axis = iv_ns_ ? iy_ + 8 : ix_ + 8;   // where the party is along the strip
        for (auto& c : cand) {
            k++;
            if (c[0] < 0 || c[1] < 0 || c[0] >= L.w || c[1] >= L.h) continue;
            // reach: a door in the cell the party stands in or in the wall beside it is in reach anywhere in that cell (64 px from its middle, as before); a door in the cell ahead
            // or behind is only in reach within 96 px of that cell's middle (it was 192): the party has to be at the end of its own cell, right up to it
            int centre = (iv_ns_ ? c[1] : c[0]) * 128 + 64;
            if (abs(hero_axis - centre) > ((k == 1 || k == 4) ? 96 : 64)) continue;
            int& v = L.cells[c[1] * L.w + c[0]];
            int fl = v >= 0 && v < (int)isecret_.size() ? isecret_[v] : 0;
            if (v == 91) { false_door = true; continue; }
            if (open && (fl & 7) == 1) {   // a hidden door: pressing A beside it searches (the Inspect command of the original) and finds it; the next press opens it
                v++;
                auto bump = [&](int x, int y) { if (x >= 0 && y >= 0 && x < L.w && y < L.h) L.cells[y * L.w + x]++; };
                if (fl & 0x80) bump(c[0] - 1, c[1]);
                if (fl & 0x20) bump(c[0] + 1, c[1]);
                if (fl & 0x10) bump(c[0], c[1] - 1);
                if (fl & 0x40) bump(c[0], c[1] + 1);
                istrip_key_ = -1;
                msg = "You discover a secret door.";
                return true;
            }
            if ((fl & 7) != (open ? 2 : 3)) continue;   // closed doors (kind 2) open, open ones (kind 3) shut; the bits 0x10 / 0x20 / 0x40 / 0x80 name the other half of the door
            int d = open ? 1 : -1;
            v += d;
            auto half = [&](int x, int y) { if (x >= 0 && y >= 0 && x < L.w && y < L.h) L.cells[y * L.w + x] += d; };
            if (fl & 0x80) half(c[0] - 1, c[1]);
            if (fl & 0x20) half(c[0] + 1, c[1]);
            if (fl & 0x10) half(c[0], c[1] - 1);
            if (fl & 0x40) half(c[0], c[1] + 1);
            istrip_key_ = -1;
            msg = open ? "The door is open." : "The door is shut.";
            return true;
        }
        if (open && false_door) { msg = "This is a false door."; return true; }
    }
    if (!open) return false;
    auto ch = items_near(true);
    if (ch.empty()) return false;
    int n = 0;
    for (auto& g : items_) if (g.in_chest == items_[ch[0]].row) { g.in_chest = -1; n++; }
    items_[ch[0]].opened = true;   // the chest stays, open, and cannot be opened again
    msg = n ? "The chest is open.\nSomething is inside." : "The chest is empty.";
    return true;
}

bool Game::foe_in_reach(bool face) {
    bool in_level = interior();
    if (mode_ != Mode::Combat && !(in_level && !enemies_.empty())) return false;
    int hx = in_level ? istrip_x() : cx_, hy = in_level ? 0 : cy_;
    const Enemy* best = nullptr;
    for (const auto& e : enemies_)
        if (e.hp > 0 && abs(e.x - hx) <= rules_.hero_reach && abs(e.y - hy) <= 24 && (!best || abs(e.x - hx) < abs(best->x - hx))) best = &e;
    if (best && face && jump_t_ < 0 && best->x != hx) face_left_ = best->x < hx;
    return best != nullptr;
}

bool Game::context_action() {
    if (lair_active_ && !lair_chest_taken_ && abs(cx_ - lair_chest_x_) < 48) {   // the lair's chest: its loot is the lair's reward
        lair_chest_taken_ = true;
        give_poi_reward();
        if (box_.empty()) box_ = "The chest is empty.";
        return true;
    }
    auto near = items_near(false);
    if (!near.empty()) { take_item(near[0], false); return true; }
    std::string m;
    if (open_or_shut(true, m)) { box_ = m; return true; }
    return false;
}

// Where the strip's x is on the map: the strip was built at the party's start position (px_, py_) at start_x; every 8 strip pixels is one map pixel along the line.
int Game::strip_of_world(int gx, int gy) const {
    int s = strip_.start_x(px_, py_, east_west_);
    return s + (east_west_ ? (gx - px_) : -(gy - py_)) * 8;
}
void Game::world_of_strip(int sx, int* gx, int* gy) const {
    int d = (sx - strip_.start_x(px_, py_, east_west_)) / 8;
    *gx = east_west_ ? px_ + d : px_;
    *gy = east_west_ ? py_ : py_ - d;
}

// Outdoors a river is a pit in the strip whose bed is 32 px below the usual ground (368 against 336).
bool Game::water_at(int sx) const { return !interior() && !lair_active_ && sx >= 0 && sx < strip_.width_px() && ground_y(sx) >= 368; }

bool Game::thrown_on_strip() const {
    if (interior() || mode_ != Mode::Combat) return false;
    for (auto& g : items_) {
        if (!g.thrown || g.taken || g.level != level_) continue;
        int gx = g.x / 8, gy = g.y / 8, sx = strip_of_world(gx, gy);
        int perp = east_west_ ? abs(gy - py_) : abs(gx - px_);
        if (perp <= 40 && sx >= 0 && sx < strip_.width_px()) return true;
    }
    return false;
}

// A spent missile lands on the ground as an item (a quiver of one): walk over it and press A to pick it up again.
void Game::drop_thrown(int kind, int sx) {
    static const int ids[5] = {14, 15, 9, 17, 16};
    GroundItem g; g.id = ids[kind]; g.qty = 1; g.level = level_; g.row = -1; g.thrown = true;
    if (interior()) {
        if (iv_ns_) { g.x = ix_; g.y = (int)levels_[level_].h * 128 - 1 - sx; } else { g.x = sx; g.y = iy_; }
    } else {
        int gx, gy; world_of_strip(sx, &gx, &gy);
        g.x = gx * 8; g.y = gy * 8;
    }
    items_.push_back(g);
}

void Game::fx(int id, double gain) {
    if (!sfx_on_) return;
    if (plat_) plat_->lock_audio();
    sound_.play_fx(id, gain);
    if (plat_) plat_->unlock_audio();
}

// Which kind of weapon the hero swings: staves (Goldmoon's, Raistlin's, Tasslehoff's hoopak) one sound set, Flint's axe another, every sword the same.
int Game::weapon_class(const Hero& h) const {
    const std::string& w = h.hand_name;
    if (w == "Wyrmslayer") return 3;
    if (w.find("Axe") != std::string::npos) return 2;
    if (w.find("taff") != std::string::npos || w.find("oopak") != std::string::npos || h.name.rfind("Goldmoon", 0) == 0 || h.name.rfind("Raistlin", 0) == 0 || h.name.rfind("Tasslehoff", 0) == 0) return 1;
    if (h.name.rfind("Flint", 0) == 0) return 2;
    return 0;
}

const std::vector<Image>& Game::spell_frames(int rec) {
    auto it = spell_img_.find(rec);
    if (it == spell_img_.end()) it = spell_img_.emplace(rec, assets_.missile(rec)).first;
    return it->second;
}

// Send the spell's sprite from the caster to each target (a bolt flies straight at the speed of a thrown weapon), then burst there.
void Game::launch_spell(int fly_rec, int hit_rec, const std::vector<Enemy*>& targets, bool big, int boom_fx, int impact) {
    double hx = interior() ? istrip_x() : cx_, hy = interior() ? 0 : cy_;
    int n = 0;
    for (Enemy* e : targets) {
        if (n++ >= 8) break;
        Bolt b; b.x0 = hx; b.y0 = hy; b.x1 = e->x; b.y1 = interior() ? 0 : e->y; b.th = mt_height(*e);
        b.left = e->x < hx; b.fly = fly_rec; b.hit = hit_rec; b.big = big; b.boom = boom_fx; b.impact = impact;
        b.dur = bolt_time(b.x1 - b.x0);
        bolts_.push_back(b);
    }
}

// What a spell does to a foe is worked out when it is cast, but shown when the spell gets there: the numbers, sparks and deaths that came from this cast (everything added since
// f0 / s0 / c0) start a little later, as late as the flight takes.
void Game::delay_effects(size_t f0, size_t s0, size_t c0, double hx) {
    for (size_t i = f0; i < floaters_.size(); i++) floaters_[i].t = -bolt_time(floaters_[i].x - hx);
    for (size_t i = s0; i < sparks_.size(); i++) sparks_[i].t = -bolt_time(sparks_[i].x - hx) / 0.3;
    for (size_t i = c0; i < corpses_.size(); i++) corpses_[i].t = -(int)std::lround(bolt_time(corpses_[i].e.x - hx) * 6.0);   // (steps: six a second)
}

void Game::add_impact(const Enemy& e, int kind, double delay) {
    Impact i; i.x = e.x; i.y = interior() ? 0 : e.y; i.th = mt_height(e); i.kind = kind; i.t = -delay / (kind == 2 ? 0.9 : 0.45);
    i.dur = kind == 2 ? 0.9 : kind == 3 ? 0.4 : kind == 5 ? 0.6 : 0.45;
    impacts_.push_back(i);
}

void Game::add_spark(const Enemy& e) {
    Spark s; s.x = e.x; s.y = interior() ? 0 : e.y; s.height = mt_height(e); sparks_.push_back(s);
}

void Game::add_spark_hero() {
    last_engaged_ = steps_;
    Spark s; s.pc = true; s.height = 32;
    if (interior()) { s.x = istrip_x(); s.y = 0; } else { s.x = cx_; s.y = cy_; }
    sparks_.push_back(s);
}

void Game::add_floater(const Enemy& e, int amount) {
    last_engaged_ = steps_;
    Floater f; f.x = e.x; f.y = interior() ? 0 : e.y; f.amount = amount;
    f.height = e.type >= 0 && e.type < (int)mtypes_.size() ? mtypes_[e.type].actor.height : 32;
    floaters_.push_back(f);
}

void Game::update_floaters(double dt_ms) {
    for (size_t i = 0; i < bolts_.size();) {
        Bolt& b = bolts_[i];
        b.t += dt_ms / 1000.0 / b.dur;
        if (b.t < 1) { i++; continue; }
        Burst u; u.x = b.x1; u.y = b.y1; u.th = b.th; u.rec = b.hit; bursts_.push_back(u);
        if (b.boom >= 0) fx(b.boom);
        if (b.impact >= 0) { Impact i; i.x = b.x1; i.y = b.y1; i.th = b.th; i.kind = b.impact; i.dur = b.impact == 2 ? 0.9 : 0.45; impacts_.push_back(i); }
        bolts_.erase(bolts_.begin() + i);
    }
    for (size_t i = 0; i < impacts_.size();) { impacts_[i].t += dt_ms / 1000.0 / impacts_[i].dur; if (impacts_[i].t >= 1) impacts_.erase(impacts_.begin() + i); else i++; }
    for (size_t i = 0; i < bursts_.size();) { bursts_[i].t += dt_ms / 1000.0 / bursts_[i].dur; if (bursts_[i].t >= 1) bursts_.erase(bursts_.begin() + i); else i++; }
    for (auto& s : slabs_) {
        if (s.landed) continue;
        s.t += dt_ms / 1000.0 / 0.28;
        if (s.t >= 1) {   // it hits: the damage of the trap, to whoever stands under it
            s.landed = true;
            if (!god()) party_[leader_].hp = std::max(0, party_[leader_].hp - s.dmg);
            add_spark_hero(); fx(kFxSlab); fx(kFxPcHit, 0.7);
        }
    }
    if (lightning_.t >= 0) { lightning_.t += dt_ms / 1000.0 / 0.7; if (lightning_.t >= 1) lightning_.t = -1; }
    if (final_t_ >= 0) { final_t_ += dt_ms / 1000.0; if (final_t_ > 2.4) { final_t_ = -1; box_ = "The Staff of Magius is\nshattered by the blast."; } }
    for (size_t i = 0; i < sparks_.size();) { sparks_[i].t += dt_ms / 1000.0 / 0.3; if (sparks_[i].t >= 1) sparks_.erase(sparks_.begin() + i); else i++; }
    for (size_t i = 0; i < floaters_.size();) {
        floaters_[i].t += dt_ms / 1000.0 / 1.0;   // one second of life
        if (floaters_[i].t >= 1) floaters_.erase(floaters_.begin() + i); else i++;
    }
}

// Top left of the side views: a line for each of Find Traps, Protection from Evil, Prayer and Bless that is in effect, with a bar that runs down and the seconds left
// (the spells count logic steps, six a second: Protection 800 steps = 133 s, Prayer 600 = 100 s, Bless 800 = 133 s, Find Traps 400 = 67 s).
void Game::draw_spell_timers() {
    struct Row { const char* name; int left, total; uint8_t col; };
    std::vector<Row> rows;
    if (reveal_ > 0 && interior()) rows.push_back({"Find Traps", reveal_, 1200, 11});
    if (detect_ > 0 && interior()) rows.push_back({"Detect Treasure", detect_, 1800, 13});
    if (ward_ > 0) rows.push_back(ward_kind_ == 2 ? Row{"Prayer", ward_, 600, 14} : Row{"Protection", ward_, 800, 9});
    if (bless_ > 0 && !(ward_kind_ == 2 && ward_ > 0)) rows.push_back({"Bless", bless_, 800, 10});
    int y = 14;
    for (auto& r : rows) {
        char b[32]; snprintf(b, sizeof b, "%s %ds", r.name, (r.left + 5) / 6);
        screen_.fill_rect(2, y - 1, (int)strlen(b) * 8 + 4, 12, 0);
        screen_.text(4, y, b, r.col, false);
        screen_.fill_rect(4, y + 9, 60, 1, 8);
        screen_.fill_rect(4, y + 9, std::max(1, 60 * r.left / r.total), 1, r.col);
        y += 14;
    }
}

void Game::draw_slab(int sx, int feet, double fall_t) {
    const int w = 46, hgt = 11;
    int y = fall_t >= 1 ? feet - hgt + 2 : (int)(-hgt + (feet - 2 + hgt) * fall_t * fall_t);
    int x = sx - w / 2;
    screen_.fill_rect(x - 1, y - 1, w + 2, hgt + 2, 0);
    screen_.fill_rect(x, y, w, hgt, 7);
    screen_.fill_rect(x, y, w, 2, 3);                    // the lit top edge
    screen_.fill_rect(x, y + hgt - 3, w, 3, 8);          // the shaded underside
    screen_.fill_rect(x + w / 3, y + 2, 1, 4, 8); screen_.fill_rect(x + w / 3 + 1, y + 5, 2, 1, 8);   // cracks
    screen_.fill_rect(x + 2 * w / 3, y + 3, 1, 5, 8); screen_.fill_rect(x + 2 * w / 3 - 2, y + 6, 2, 1, 8);
    for (int k = 0; k < 6; k++) screen_.fill_rect(x + 3 + k * 7, y + 3 + (k * 5) % 4, 2, 1, 8);   // grain
    if (fall_t >= 1) for (int k = 0; k < 5; k++) screen_.fill_rect(x - 4 + k * 12, feet + 1, 3, 1, 7);   // dust at its edges
}

// Bolts, bursts and the Final Strike. Sprites trail to the left in the file, so they are flipped when flying left.
void Game::draw_spell_fx(int cam_x, bool interior_view, int cam_y) {
    auto feet_of = [&](double y) { return interior_view ? 112 + (int)y : (int)y - cam_y + 16; };
    for (auto& s : slabs_) if (!s.landed) draw_slab((int)s.sx - cam_x + 8, interior_view ? 112 : (int)cy_ - cam_y + 16, s.t);   // a slab on its way down
    for (auto& b : bolts_) {
        const auto& fr = spell_frames(b.fly);
        if (fr.empty()) continue;
        size_t n = std::min<size_t>(4, fr.size());
        const Image& im = fr[(size_t)(b.t * b.dur * 16) % n];
        double p = std::min(1.0, b.t), x = b.x0 + (b.x1 - b.x0) * p, y = b.y0 + (b.y1 - b.y0) * p, hgt = 20 + (b.th / 2.0 - 20) * p;
        {   // drawn at double size so the flight can be followed
            const int sc = 2;
            int ox0 = (int)x - cam_x - im.w * sc / 2, oy0 = feet_of(y) - (int)hgt - im.h * sc / 2;
            for (int yy = 0; yy < im.h * sc; yy++) for (int xx = 0; xx < im.w * sc; xx++) {
                int sxp = b.left ? im.w - 1 - xx / sc : xx / sc;
                uint8_t v = im.px[(yy / sc) * im.w + sxp];
                int dx = ox0 + xx, dy = oy0 + yy;
                if (v && dx >= 0 && dx < kW && dy >= 0 && dy < kH) screen_.px[dy * kW + dx] = v;
            }
        }
    }
    for (auto& u : bursts_) {
        const auto& fr = spell_frames(u.rec);
        if (fr.empty()) continue;
        size_t n = u.rec == 2 ? std::min<size_t>(7, fr.size()) : fr.size();
        size_t k = std::min(n - 1, (size_t)(u.t * n));
        const Image& im = fr[u.rec == 2 ? k : (k < 4 ? k : k)];
        screen_.blit(im, (int)u.x - cam_x - im.w / 2, feet_of(u.y) - u.th / 2 - im.h / 2, true, false);
    }
    for (auto& im : impacts_) {   // on-impact effects (see Impact)
        if (im.t < 0) continue;
        double p = im.t;
        int feet = interior_view ? 112 + (int)im.y : (int)im.y - cam_y + 16;
        int mx = (int)im.x - cam_x, my = feet - im.th / 2;
        unsigned seed = (unsigned)((int)(p * 14) * 7919 + im.kind * 104729 + (int)im.x * 31);
        auto rn = [&]() { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 16) & 0xff); };
        auto dot = [&](int x, int y, int w, uint8_t c) { screen_.fill_rect(x, y, w, w, c); };
        if (im.kind == 0) {   // energy splash: a ring of sparks flying out, pale blue and white with a hint of pink
            int r = 3 + (int)(p * 13);
            if (p < 0.25) dot(mx - 3, my - 3, 7, 3);
            for (int k = 0; k < 10; k++) { double a = k * 0.6283 + p; dot(mx + (int)(std::cos(a) * r), my + (int)(std::sin(a) * r * 0.8), p < 0.6 ? 2 : 1, k % 3 == 0 ? 2 : k % 3 == 1 ? 1 : 3); }
        } else if (im.kind == 1) {   // small flames licking up from the foe
            for (int k = 0; k < 6; k++) {
                int fx0 = mx - 11 + k * 5 + (rn() % 3) - 1, base = my + im.th / 3 - (int)(p * 6), hgt = std::max(3, 15 - (int)(p * 11) - (k % 2) * 4);
                for (int y = 0; y < hgt; y++) {
                    int w = std::max(1, 5 - y * 5 / hgt), sway = (int)(std::sin(p * 12 + k + y * 0.5) * 1.5);
                    uint8_t c = y < hgt / 3 ? 15 : y < hgt * 2 / 3 ? 6 : 5;
                    screen_.fill_rect(fx0 - w / 2 + sway, base - y, w, 1, c);
                }
            }
        } else if (im.kind == 2) {   // a huge explosion: the big blast sprite at double size, embers flying out
            const auto& fr = spell_frames(2);
            if (!fr.empty()) {
                const Image& s = fr[std::min<size_t>(std::min<size_t>(7, fr.size()) - 1, (size_t)(p * 7))];
                int sc = 3;
                for (int y = 0; y < s.h * sc; y++) for (int x = 0; x < s.w * sc; x++) {
                    uint8_t v = s.px[(y / sc) * s.w + (x / sc)];
                    int dx = mx - s.w * sc / 2 + x, dy = my - s.h * sc / 2 + y;
                    if (v && dx >= 0 && dx < kW && dy >= 0 && dy < 128) screen_.px[dy * kW + dx] = v;
                }
            }
            for (int k = 0; k < 12; k++) { double a = k * 0.5236 + 0.3; int r = 14 + (int)(p * 46); dot(mx + (int)(std::cos(a) * r), my + (int)(std::sin(a) * r * 0.8), 2, k % 2 ? 15 : 6); }
        } else if (im.kind == 3) {   // a zap of electricity crackling over the foe
            for (int k = 0; k < 6; k++) {
                double a = k * 1.0472 + rn() * 0.01;
                int x = mx, y = my;
                for (int s = 0; s < 6; s++) {
                    int nx = x + (int)(std::cos(a) * 3) + (rn() % 5) - 2, ny = y + (int)(std::sin(a) * 3) + (rn() % 5) - 2;
                    screen_.fill_rect(nx - 1, ny - 1, 3, 3, 10); screen_.fill_rect(nx, ny, 1, 1, s % 2 ? 3 : 1);
                    x = nx; y = ny;
                }
            }
            if ((int)(p * 14) % 2 == 0) dot(mx - 2, my - 2, 5, 3);
        } else if (im.kind == 5) {   // a Bozak's bones burst: a smaller blast and shards of bone flying every way
            const auto& fr = spell_frames(2);
            if (!fr.empty()) {
                const Image& s = fr[std::min<size_t>(std::min<size_t>(7, fr.size()) - 1, (size_t)(p * 7))];
                int sc = 2;
                for (int y = 0; y < s.h * sc; y++) for (int x = 0; x < s.w * sc; x++) {
                    uint8_t v = s.px[(y / sc) * s.w + (x / sc)];
                    int dx = mx - s.w * sc / 2 + x, dy = my - s.h * sc / 2 + y;
                    if (v && dx >= 0 && dx < kW && dy >= 0 && dy < 128) screen_.px[dy * kW + dx] = v;
                }
            }
            for (int k = 0; k < 12; k++) { double a = k * 0.5236 + 0.2; int r = 6 + (int)(p * 34); int bx = mx + (int)(std::cos(a) * r), by = my + (int)(std::sin(a) * r * 0.9) + (int)(p * p * 14); screen_.fill_rect(bx, by, 3, 1, 3); screen_.fill_rect(bx + 1, by - 1, 1, 3, 7); }
        } else {   // a heavy thump of light (the hammer): a star of rays
            int r = 4 + (int)(p * 12);
            for (int k = 0; k < 8; k++) { static const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1}; for (int s = 2; s <= r; s += 2) dot(mx + dx8[k] * s, my + dy8[k] * s, 2, s < r - 2 ? 3 : 15); }
            if (p < 0.3) dot(mx - 3, my - 3, 7, 15);
        }
    }
    if (lightning_.t >= 0) {   // the bolt: a jagged line of light from the caster off the edge of the view, flickering; it reaches the edge in the first fifth
        int feet = interior_view ? 112 + (int)lightning_.y : (int)lightning_.y - cam_y + 16, my = feet - 22;
        int sx = (int)lightning_.x0 - cam_x, ex = lightning_.left ? -4 : kW + 4, dir = lightning_.left ? -1 : 1;
        double reach = std::min(1.0, lightning_.t / 0.2);
        int len = (int)(std::abs(ex - sx) * reach);
        unsigned seed = (unsigned)(lightning_.t * 26) * 2654435761u;
        auto rnd8 = [&]() { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 16) & 0xff); };
        int px = sx, py = my;
        for (int d = 0; d < len; d += 6) {
            int nx = sx + dir * (d + 6), ny = std::clamp(my + (rnd8() % 15) - 7, my - 14, my + 14);
            for (int pass = 0; pass < 3; pass++) {   // outer glow, then the white core
                uint8_t col = pass == 0 ? 10 : pass == 1 ? 1 : 3;
                int w = pass == 0 ? 3 : pass == 1 ? 2 : 1;
                int steps = std::max(std::abs(nx - px), std::abs(ny - py));
                for (int k = 0; k <= steps; k++) {
                    int x = px + (nx - px) * k / std::max(1, steps), y = py + (ny - py) * k / std::max(1, steps);
                    screen_.fill_rect(x - w / 2, y - w / 2, w, w, col);
                }
            }
            if (rnd8() % 4 == 0) {   // a fork
                int fx = nx, fy = ny, fdir = rnd8() % 2 ? 1 : -1;
                for (int k = 0; k < 4; k++) { int x2 = fx + dir * 3, y2 = fy + fdir * 3; screen_.fill_rect(x2, y2, 1, 1, 1); fx = x2; fy = y2; }
            }
            px = nx; py = ny;
        }
        if (lightning_.t < 0.12) screen_.fill_rect(0, 0, kW, 2, 3);   // a flash at the top of the view
    }
    if (final_t_ >= 0) {   // the whole view goes up: the big explosion frames scaled to fill it, a white flash first, red and yellow embers after
        const auto& fr = spell_frames(2);
        screen_.set_clip(0, 0, kW, 128);
        if (final_t_ < 0.12) screen_.fill_rect(0, 0, kW, 128, 3);
        else if (!fr.empty()) {
            double p = (final_t_ - 0.12) / 2.2;
            size_t k = std::min<size_t>(std::min<size_t>(7, fr.size()) - 1, (size_t)(p * 7));
            const Image& im = fr[k];
            int s = 3 + (int)(std::min(1.0, p * 2.2) * 7);   // 3x growing to 10x
            static const int offs[5][2] = {{0, 0}, {-90, -20}, {90, -25}, {-50, 30}, {60, 28}};
            for (int o = 0; o < 5; o++) {
                int sc = o == 0 ? s : std::max(2, s * 2 / 3);
                int ox = 160 + offs[o][0] - im.w * sc / 2, oy = 64 + offs[o][1] - im.h * sc / 2;
                for (int y = 0; y < im.h * sc; y++) {
                    int dy = oy + y; if (dy < 0 || dy >= 128) continue;
                    for (int x = 0; x < im.w * sc; x++) {
                        int dx = ox + x; if (dx < 0 || dx >= kW) continue;
                        uint8_t v = im.px[(y / sc) * im.w + (x / sc)];
                        if (v) screen_.px[dy * kW + dx] = v;
                    }
                }
            }
            if (p > 0.7) for (int y = 0; y < 128; y += 2) for (int x = (y / 2) % 2; x < kW; x += 2) if (((x * 7 + y * 13 + (int)(final_t_ * 40)) % 5) == 0) screen_.px[y * kW + x] = 0;   // it dies away in a scatter of dark
        }
        screen_.reset_clip();
    }
}

// A dragon's head is tied to its body in the lair by a neck: a chain of red scales from the body (where the head started) to the head, arching.
void Game::draw_dragon_neck(const Enemy& e, int cx) {
    if (e.hlx < 0) return;
    const MonsterType& t = mtypes_[e.type];
    double bx = e.ax - cx, hx = e.x - cx;
    double by = 112 - 26, hy = 112 - t.actor.height / 2.0;
    double dist = std::abs(hx - bx);
    if (dist < 10) return;
    int n = std::max(3, (int)(dist / 6));
    for (int i = 0; i <= n; i++) {
        double p = (double)i / n, x = bx + (hx - bx) * p, y = by + (hy - by) * p - std::sin(3.14159 * p) * 7;
        int r = 6 - (int)(p * 1);
        for (int yy = -r; yy <= r; yy++) for (int xx = -r; xx <= r; xx++) {
            int d2 = xx * xx + yy * yy;
            if (d2 > r * r) continue;
            int px2 = (int)x + xx, py2 = (int)y + yy;
            if (px2 < 0 || px2 >= kW || py2 < 0 || py2 >= 128) continue;
            screen_.px[py2 * kW + px2] = d2 > (r - 1) * (r - 1) ? 5 : ((xx + yy + i) % 4 == 0 ? 12 : 6);   // dark red rim, scales in light red and a few brown
        }
    }
}

void Game::draw_floaters(int cam_x, bool interior_view, int cam_y) {
    draw_spell_fx(cam_x, interior_view, cam_y);
    if (held()) {   // "Stuck" / "Paralyzed" over the hero's head
        int hx = (interior_view ? istrip_x() : cx_) - cam_x, feet = interior_view ? 112 : cy_ - cam_y + 16;
        const char* t = hold_kind_ == 1 ? "Stuck" : "Paralyzed";
        screen_.text(hx + 8 - (int)strlen(t) * 4, feet - 50, t, (steps_ / 3) % 2 ? 15 : 12, true);
    }
    for (auto& sp : sparks_) {   // hit sparks
        if (sp.t < 0) continue;   // hit sparks: eight rays flying out from the middle of the body, a bright flash in the first moment
        int feet = interior_view ? 112 + (int)sp.y : (int)sp.y - cam_y + 16;
        int mx = (int)sp.x - cam_x + (interior_view ? 0 : 0), my = feet - sp.height / 2;
        static const int dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const uint8_t cols[3] = {sp.pc ? (uint8_t)15 : (uint8_t)15, sp.pc ? (uint8_t)6 : (uint8_t)14, sp.pc ? (uint8_t)5 : (uint8_t)7};
        int r = 2 + (int)(sp.t * 10), step = sp.t < 0.35 ? 0 : sp.t < 0.7 ? 1 : 2;
        if (sp.t < 0.25) screen_.fill_rect(mx - 2, my - 2, 5, 5, cols[0]);
        for (int k = 0; k < 8; k++) {
            int px = mx + dx8[k] * r * (k % 2 ? 1 : 2) / (k % 2 ? 1 : 1), py = my + dy8[k] * r;
            screen_.fill_rect(px, py, step == 2 ? 1 : 2, step == 2 ? 1 : 2, cols[step]);
        }
    }
    for (auto& e : enemies_) {   // "Turned" over the head of an undead creature held by Goldmoon's Turn Undead
        if (e.hp <= 0 || !e.turned || e.frozen <= 0) continue;
        const MonsterType& t = mtypes_[e.type];
        int top = interior_view ? 112 - t.actor.height : e.y - cam_y + 16 - t.actor.height;
        int x = e.x - cam_x + 8 - 3 * 8;
        screen_.text(x, top - 10, "Turned", (steps_ / 3) % 2 ? 15 : 14, true);
    }
    for (auto& f : floaters_) {
        if (f.t < 0) continue;   // not yet: the spell is still on its way
        char b[16];
        if (f.amount == -2) snprintf(b, sizeof b, "Drowned"); else if (f.amount < 0) snprintf(b, sizeof b, "Miss"); else snprintf(b, sizeof b, "%d", f.amount);
        int w = (int)strlen(b) * 8;
        double rise = 22 * f.t;
        int feet = interior_view ? 112 + (int)f.y : (int)f.y - cam_y + 16;
        int sx = (int)f.x - cam_x - w / 2, sy = feet - f.height - 6 - (int)rise;
        // fade: bright, then dim, then dark, then gone
        static const uint8_t damage_cols[3] = {15, 12, 8}, miss_cols[3] = {3, 7, 8};
        int step = f.t < 0.5 ? 0 : f.t < 0.75 ? 1 : 2;
        screen_.text(sx, sy, b, (f.amount < 0 ? miss_cols : damage_cols)[step], step < 2);
    }
}

void Game::update_missiles(double dt_ms) {
    for (size_t i = 0; i < missiles_.size();) {
        Missile& m = missiles_[i];
        m.t += dt_ms / 1000.0 / m.dur;
        if (m.t < 1) { i++; continue; }
        if (m.target >= 0 && m.target < (int)enemies_.size() && enemies_[m.target].hp > 0) { if (m.hit) { damage_enemy(enemies_[m.target], m.dmg); fx(kFxMissile + 3 * (m.kind <= 1 ? 0 : m.kind == 2 ? 1 : m.kind == 3 ? 2 : 3) + 1); } else add_floater(enemies_[m.target], -1); }
        {   // it comes down on the ground out there; if that is a river it stops on the near bank instead (ammunition is never lost in the water)
            int lx = std::clamp((int)m.x1, 16, strip_.width_px() - 16), back = m.x1 > m.x0 ? -16 : 16;
            for (int tries = 0; water_at(lx) && tries < 60; tries++) lx = std::clamp(lx + back, 16, strip_.width_px() - 16);
            drop_thrown(m.kind, lx);
            if (!m.hit) fx(kFxMissile + 3 * (m.kind <= 1 ? 0 : m.kind == 2 ? 1 : m.kind == 3 ? 2 : 3) + 2, 0.8);   // it comes down on the ground: a soft sound
        }
        missiles_.erase(missiles_.begin() + i);
    }
}

void Game::draw_missiles(int cam_x, bool interior_view, int cam_y) {
    for (auto& m : missiles_) {
        if (m.kind < 0 || m.kind >= (int)missile_img_.size() || missile_img_[m.kind].empty()) continue;
        double p = std::min(1.0, m.t);
        double x = m.x0 + (m.x1 - m.x0) * p, y = m.y0 + (m.y1 - m.y0) * p;
        double arc = 0;   // as in the original they fly straight out
        const auto& frames = missile_img_[m.kind];
        const Image& im = frames[m.kind == 3 ? (size_t)(m.t * 14 * (m.left ? -1 : 1) + 64) % frames.size() : 0];
        int feet = interior_view ? 112 + (int)y : (int)y - cam_y + 16;
        screen_.blit(im, (int)x - cam_x - im.w / 2, feet - 22 - (int)arc - im.h / 2, true, m.left);
    }
}

// Things lying on the ground along the side view: the fired arrows, spears and axes (their own sprites, resting) and other items as small gold squares.
void Game::draw_ground_things(int cam_x, bool interior_view, int cam_y) {
    for (auto& g : items_) {
        if (g.taken || g.in_chest >= 0 || g.level != level_ || (g.id >= 56 && g.id <= 59) || item_hidden(g) || (g.id >= 43 && g.id <= 46)) continue;
        int sx, perp;
        if (interior_view) {
            sx = iv_ns_ ? (int)levels_[level_].h * 128 - 1 - g.y : g.x;
            perp = iv_ns_ ? abs(g.x - ix_) : abs(g.y - iy_);
        } else {
            int gx = g.x / 8, gy = g.y / 8;
            sx = strip_of_world(gx, gy);
            perp = east_west_ ? abs(gy - py_) : abs(gx - px_);
        }
        if (perp > 40 || sx - cam_x < -16 || sx - cam_x > kW + 16) continue;
        int ground = interior_view ? 112 : ground_y(sx) - cam_y;   // the top of the ground there (the hero's feet are at cy_ + 16 = ground)
        int kind = g.id == 14 ? 0 : g.id == 15 ? 1 : g.id == 9 ? 2 : g.id == 17 ? 3 : g.id == 16 ? 4 : -1;
        if (kind >= 0 && !missile_img_[kind].empty()) {
            const auto& fr = missile_img_[kind];
            const Image& im = fr[kind == 3 ? 2 % fr.size() : 0];
            screen_.blit(im, sx - cam_x - im.w / 2, ground - im.h, true, false);
        } else screen_.fill_rect(sx - cam_x - 3, ground - 6, 6, 6, 15);
    }
}

void Game::damage_enemy(Enemy& e, int dmg) {
    e.hp -= dmg;
    add_spark(e);
    add_floater(e, dmg);
    if (e.hp <= 0) { const MonsterType& mt = mtypes_[e.type]; if (!e.revived) { xp_ += mt.xp; party_[leader_].xp += mt.xp; } kills_++; if (e.type < (int)kills_by_type_.size()) kills_by_type_[e.type]++; }
}

// The casters' ranged attack. Raistlin works up his damaging spells from the weakest (Magic missile, Burning hands, Fire ball); Final strike only when chosen from the list.
// Goldmoon: with foes about, turn undead (only if there are undead), the Spiritual hammer, Hold person (only on humanoids not already held); with none, she heals the hurt with her
// memorized Cure light wounds, then Bless, Protection from evil (if they are not still in effect) and Find traps (only underground). The Disks of Mishakal are never used on her own.
bool Game::caster_attack(const Hero& h) {
    bool foes = false, undead = false, humanoid = false;
    const int hxv = interior() ? istrip_x() : cx_;
    for (auto& e : enemies_) {
        if (e.hp <= 0 || abs(e.x - hxv) >= 175) continue;   // only foes in sight (the view reaches about 160 px each way): a spell needs something to fly at
        foes = true;
        const std::string& n = mtypes_[e.type].name;
        if (is_undead(n) && e.frozen <= 0) undead = true;   // only worth turning those that are not frozen already
        if (e.frozen <= 0 && (n == "Baaz" || n == "Bozak" || n == "Kapak" || n == "Goblin" || n == "Hobgoblin" || n == "Aghar" || n == "Man")) humanoid = true;
    }
    if (h.name.rfind("Raistlin", 0) == 0) {
        if (!foes || staff_broken_) { if (!foes) { message_ = "No target in sight"; message_steps_ = 10; } return false; }
        static const int order[3] = {2, 6, 7};
        for (int k = std::min(caster_step_, 2); k < 3; k++)
            if (staff_ >= kMagic[order[k]].cost && staff_ > 0) { cast(1, order[k]); caster_step_ = k + 1; return true; }
        for (int k = 0; k < std::min(caster_step_, 2); k++)
            if (staff_ >= kMagic[order[k]].cost && staff_ > 0) { cast(1, order[k]); return true; }
        message_ = "The staff is spent"; message_steps_ = 10;
        return false;
    }
    auto ready = [&](int entry) { return entry >= 12 || !used_[entry]; };
    if (foes) {
        if (undead) { cast(0, 12); return true; }
        if (ready(7)) { cast(0, 7); return true; }
        if (humanoid) { if (ready(5)) { cast(0, 5); return true; } if (ready(6)) { cast(0, 6); return true; } }
        message_ = "No spell to cast"; message_steps_ = 10;
        return false;
    }
    bool hurt = false;
    for (auto& m : party_) if (m.hp > 0 && m.hp < m.hp_max) hurt = true;
    if (hurt) for (int entry = 0; entry < 3; entry++) if (ready(entry)) { cast(0, entry); return true; }
    if (bless_ <= 0 && ready(9)) { cast(0, 9); return true; }
    if (ward_ <= 0 && ready(3)) { cast(0, 3); return true; }
    if (interior() && reveal_ <= 0 && ready(4)) { cast(0, 4); return true; }
    return false;
}

bool Game::ranged_attack() {
    if (held()) return false;
    Hero& h = party_[leader_];
    if (h.name.rfind("Raistlin", 0) == 0 || h.name.rfind("Goldmoon", 0) == 0) return caster_attack(h);
    auto say = [&](const char* t) { message_ = t; message_steps_ = 12; };
    if (h.range_name.empty()) { say("No ranged weapon"); return false; }
    if (h.ammo <= 0) { say("Out of ammunition"); return false; }
    int hx = interior() ? istrip_x() : cx_;
    Enemy* t = nullptr; int bd = 1 << 30;
    for (auto& e : enemies_) { int d = abs(e.x - hx); if (e.hp > 0 && d < bd) { bd = d; t = &e; } }
    if (!t || bd > 360) { say("No target in range"); return false; }
    face_left_ = t->x < hx;
    h.ammo--;
    int need = h.thac0 - mtypes_[t->type].ac - h.to_hit - (bless_ > 0 ? 1 : 0);
    bool hit = rnd(1, 20) >= need;
    int dmg = h.range_bonus + h.ammo_bonus;
    for (int k = 0; k < h.range_dice; k++) dmg += rnd(1, h.range_sides);
    Missile m;
    const std::string& rn = h.range_name;
    m.kind = rn.find("Bow") != std::string::npos ? (h.ammo_bonus >= 2 ? 1 : 0) : rn.find("Spear") != std::string::npos ? 2 : rn.find("Axe") != std::string::npos ? 3 : 4;
    m.left = t->x < hx;
    m.x0 = hx + (m.left ? -10 : 10); m.y0 = interior() ? 0 : cy_;
    // straight out: a hit stops at the foe; a miss flies on right off the screen and comes down on the ground out there (it turns up when you walk to it)
    if (hit) { m.x1 = t->x; m.y1 = interior() ? 0 : t->y; }
    else { m.x1 = hx + (m.left ? -1 : 1) * rnd(380, 470); m.y1 = m.y0; }
    m.dur = std::max(0.18, std::abs(m.x1 - m.x0) / (m.kind == 2 ? 520.0 : m.kind == 3 ? 380.0 : 700.0));
    m.hit = hit; m.dmg = std::max(1, dmg); m.target = (int)(t - enemies_.data());
    missiles_.push_back(m);
    fx(kFxMissile + 3 * (m.kind <= 1 ? 0 : m.kind == 2 ? 1 : m.kind == 3 ? 2 : 3));   // thrown
    return true;
}

// The main menu. With the updated controls the entries the gamepad / mouse / A button make unnecessary are left out: Hero select (LB / RB), Take and Open (A), Give (the pack is shared),
// Quest map (Back) and Exit menu (B); a "Controls (F1)" entry is added.
std::vector<int> Game::menu_entries() const {
    std::vector<int> v;
    for (int i = 0; i < kMenuN; i++) {
        if (!orig_controls_ && (i == 0 || i == 1 || i == 6 || i == 7 || i == 8 || i == 13)) continue;
        v.push_back(i);
        if (!orig_controls_ && i == 4) { v.push_back(20); v.push_back(21); }   // Equip, Inventory after Use
        if (!orig_controls_ && i == 17) v.push_back(19);
    }
    return v;
}

void Game::step_menu(const Input& in) {
    const std::vector<int> entries = menu_entries();
    const int en = (int)entries.size();
    menu_item_ = std::clamp(menu_item_, 0, en - 1);
    // in menus the inputs are already single presses (see frame())
    bool edge = in.confirm, up = in.up, down = in.down, left = in.left, right = in.right, cancel = in.cancel;
    const bool equip_page = menu_page_ == kPageEquip || menu_page_ == kPageEquipPick || menu_page_ == kPageInventory;
    if (!orig_controls_ && in.mx >= 0 && in.my >= 0 && menu_page_ != kPageSettings && !equip_page) {  // updated controls: the mouse points at rows and clicks them
        bool moved = in.mx != last_mx_ || in.my != last_my_;
        last_mx_ = in.mx; last_my_ = in.my;
        bool on_paper = in.mx >= 48 && in.mx < 272 && in.my >= 29 && in.my < 104;
        int n = 0, row_h = 9, y0 = 31, x0 = 80, first = 0, *sel = nullptr;
        if (menu_page_ < 0) { n = en; sel = &menu_item_; }
        else if (menu_page_ == kPageClerical) { n = (int)cleric_rows(used_).size(); sel = &menu_scroll_; }
        else if (menu_page_ == kPageMagic) { n = 10; sel = &menu_scroll_; }
        else if (menu_page_ == kPageUse || menu_page_ == kPageDrop) { n = (int)inv_.size(); sel = &menu_scroll_; row_h = 10; y0 = 24; x0 = 56; }
        else if (menu_page_ == kPageTake) { n = (int)items_near(false).size(); sel = &menu_scroll_; row_h = 10; y0 = 24; x0 = 56; }
        if (sel) first = std::clamp(*sel - 7, 0, std::max(0, n - 8));
        int row = (in.my >= y0 && in.mx >= x0 && in.mx < 270) ? (in.my - y0) / row_h : -1;
        if (sel && row >= 0 && row < 8 && first + row < n) {
            if (moved) *sel = first + row;
            if (in.click) { *sel = first + row; edge = true; }
        } else if (in.click) {
            if (menu_page_ == kPageHero && on_paper) edge = true;      // choose the hero shown
            else if (!on_paper || menu_page_ == kPageQuest || menu_page_ == kPageXp) cancel = true;  // a click outside the scroll backs out
        }
    }
    if (in.map && menu_page_ == kPageQuest) cancel = true;   // Back again closes the map
    if (in.start && menu_page_ == kPageQuest) { menu_page_ = -1; menu_direct_ = false; return; }   // Start on the overlay map shows the main menu
    confirm_prev_ = true;  // the key that opened the menu must not also act inside it
    auto close = [&]() { mode_ = menu_return_; };
    auto list_nav = [&](int n) {
        if (n <= 0) return;
        if (up) menu_scroll_ = (menu_scroll_ + n - 1) % n;
        if (down) menu_scroll_ = (menu_scroll_ + 1) % n;
    };
    if (equip_page) { step_equip(in, edge, up, down, cancel); return; }
    if (menu_page_ < 0) {
        if (up) menu_item_ = (menu_item_ + en - 1) % en;
        if (down) menu_item_ = (menu_item_ + 1) % en;
        if (cancel) { close(); return; }
        if (!edge) return;
        menu_scroll_ = 0;
        const int entry = entries[menu_item_];
        switch (entry) {
            case 0: close(); return;
            case 1: menu_page_ = kPageHero; menu_hero_ = leader_; return;
            case 2: menu_page_ = kPageClerical; return;
            case 3: menu_page_ = kPageMagic; return;
            case 4: menu_page_ = kPageUse; return;
            case 5:  // Rest: "heals all damaged characters one HP" (manual); regains the memorized spells
                for (auto& h : party_) if (h.hp > 0 && h.hp < h.hp_max) h.hp++;
                for (bool& u : used_) u = false;
                close(); show_box(23); return;
            case 6: if (items_near(false).empty()) { close(); show_box(3); return; } menu_page_ = kPageTake; return;
            case 7: case 12: {  // Open / Shut: a door in the cell the party stands in, or a chest within reach
                std::string m;
                bool ok = open_or_shut(entry == 7, m);
                close();
                box_ = ok ? m : (entry == 7 ? "There is nothing here\\nto open." : "There is nothing here\\nto shut.");
                return;
            }
            case 9: {  // Inspect (fn 2ECC): in a dungeon, a hidden door in the cell the party stands in is found (it and the other half of the door become ordinary
                       // closed doors); a "false door" (cell 91) says so; anywhere else nothing is found. Only reachable from the menu in the original.
                close();
                int msg = 40;  // "You find nothing of interest"
                if (interior()) {
                    Level& L = levels_[level_];
                    int cx = ix_ >> 7, cy = iy_ >> 7;
                    if (cx >= 0 && cy >= 0 && cx < L.w && cy < L.h) {
                        int& v = L.cells[cy * L.w + cx];
                        int fl = v < (int)isecret_.size() ? isecret_[v] : 0;
                        if ((fl & 7) == 1) {
                            v++;
                            auto bump = [&](int x, int y) { if (x >= 0 && y >= 0 && x < L.w && y < L.h) L.cells[y * L.w + x]++; };
                            if (fl & 0x80) bump(cx - 1, cy);
                            if (fl & 0x20) bump(cx + 1, cy);
                            if (fl & 0x10) bump(cx, cy - 1);
                            if (fl & 0x40) bump(cx, cy + 1);
                            istrip_key_ = -1;
                            msg = 41;  // "You discover a secret door"
                        } else if (v == 91) msg = 62;  // "This is a false door"
                    }
                }
                show_box(msg);
                return;
            }
            case 10: if (inv_.empty()) { close(); show_box(5); return; } menu_page_ = kPageDrop; return;
            case 13: menu_page_ = kPageQuest; { int y = interior() ? 360 : py_ / 8; qm_top_ = y <= 64 ? 0 : std::min(256, y - 64); } return;
            case 14: close(); save_game(); return;
            case 15: close(); load_game(); return;
            case 16: menu_page_ = kPageXp; return;
            case 17: menu_page_ = kPageSettings; settings_focus_ = 0; return;
            case 18: close(); request_exit(); return;   // Exit (the browser's exit page / quit)
            case 19: open_help(); return;               // Controls (F1): the list of commands
            case 20: menu_page_ = kPageEquip; menu_hero_ = leader_; equip_slot_ = 0; return;   // Equip: the leader's character screen
            case 21: menu_page_ = kPageInventory; menu_scroll_ = 0; return;
            default: close(); box_ = "That command is not\\nin this version yet."; return;
        }
    }
    if (menu_page_ == kPageSettings) { step_settings(in); return; }
    if (cancel) { menu_page_ = -1; if (menu_direct_) { menu_direct_ = false; close(); } return; }
    switch (menu_page_) {
        case kPageHero:
            if (left || up) menu_hero_ = (menu_hero_ + (int)party_.size() - 1) % party_.size();
            if (right || down) menu_hero_ = (menu_hero_ + 1) % party_.size();
            if (edge) { set_leader(menu_hero_); menu_page_ = -1; }
            break;
        case kPageClerical: {
            auto rows = cleric_rows(used_);
            int n = (int)rows.size();
            if (menu_scroll_ >= n) menu_scroll_ = 0;
            if (up) { do menu_scroll_ = (menu_scroll_ + n - 1) % n; while (rows[menu_scroll_].second == 2); }
            if (down) { do menu_scroll_ = (menu_scroll_ + 1) % n; while (rows[menu_scroll_].second == 2); }
            if (edge) {
                auto r = rows[menu_scroll_];
                if (r.second == 0) menu_page_ = -1;
                else if (r.second == 1) { cast(0, r.first); close(); }
            }
            break;
        }
        case kPageMagic: {
            list_nav(10);  // Exit menu + 9 spells
            if (edge) { if (menu_scroll_ == 0) menu_page_ = -1; else { cast(1, menu_scroll_ - 1); close(); } }
            break;
        }
        case kPageUse: case kPageDrop: {
            list_nav((int)inv_.size());
            if (!edge || inv_.empty()) { if (inv_.empty()) menu_page_ = -1; break; }
            int id = inv_[menu_scroll_];
            if (menu_page_ == kPageUse) {
                if (id == 39 || id == 51) { apply_heal(id == 39 ? 10 : 25, false); inv_.erase(inv_.begin() + menu_scroll_); close(); show_box(16); }
                else if (id == 52 || id == 53) { party_[leader_].armor = std::max(0, party_[leader_].armor - (id == 52 ? 1 : 2)); party_[leader_].ring = true; party_[leader_].ring_bonus = id == 52 ? 1 : 2; inv_.erase(inv_.begin() + menu_scroll_); close(); show_box(17); }
                else if (id == 21 || id == 22 || id == 23 || id == 25 || id == 26 || id == 28 || id == 73 || id == 74) {   // the other scrolls
                    close();
                    if (use_scroll(id)) inv_.erase(inv_.begin() + menu_scroll_);
                }
                else if (id == 20) {   // the Lightning Bolt scroll: only Raistlin can read it, and only at a foe
                    if (party_[leader_].name.rfind("Raistlin", 0) != 0) { close(); box_ = "Only Raistlin can read\\nthis scroll."; }
                    else if (enemies_.empty()) { close(); box_ = "There is nothing to\\nstrike here."; }
                    else { close(); cast_lightning(); inv_.erase(inv_.begin() + menu_scroll_); }
                }
                else { close(); box_ = item_name(id) + " is ready."; }
            } else {
                GroundItem g; g.id = id; g.qty = 1; g.level = level_; g.row = -1;
                g.x = interior() ? ix_ : px_ * 8; g.y = interior() ? iy_ : py_ * 8;
                items_.push_back(g); inv_.erase(inv_.begin() + menu_scroll_); close();
            }
            break;
        }
        case kPageTake: {
            auto near = items_near(false);
            if (near.empty()) { menu_page_ = -1; close(); break; }
            if (menu_scroll_ >= (int)near.size()) menu_scroll_ = 0;
            list_nav((int)near.size());
            if (edge) {
                if (take_item(near[menu_scroll_], true) && menu_scroll_ > 0) menu_scroll_--;
            }
            break;
        }
        case kPageQuest:
            if (up) qm_top_ = std::max(0, qm_top_ - 16);
            if (down) qm_top_ = std::min(256, qm_top_ + 16);
            if (edge) menu_page_ = -1;
            break;
        default: if (edge) menu_page_ = -1; break;
    }
}

// A parchment scroll with a banner title, as in the original's menus and status screens. `lines` sets its height.
// The ends are rolled and the paper is speckled, like the original's; `steps_` makes the paper open when a menu appears.
void Game::draw_scroll(const std::string& title, int lines) {
    (void)lines;
    screen_.reset_clip();
    screen_.fill_rect(0, 0, kW, 128, 0);
    // The scroll is built from the original's qtiles: 0-3 ragged left edge, 4-7 paper and right edge, 8-11 roller, 16-19 / 20-23 left and
    // right knob, 13-15 banner. Geometry measured on the original: paper x 48-271, y 29-103; rollers at y 14 and below the paper; banner at the top.
    auto tile = [&](int n, int x, int y, bool flip = false) { if (n >= 0 && n < (int)qtiles_.size()) screen_.blit(qtiles_[n], x, y, false, flip); };
    int full = 80;  // five tile rows
    int open = turbo_ ? full : std::min(full, 16 + (int)(steps_ - menu_open_step_) * 8);  // the paper unrolls
    screen_.set_clip(0, 29, kW, open > 74 ? 74 : open);
    for (int r = 0, y = 29; y < 29 + open; r++, y += 16) {
        tile(r % 4, 48, y);
        screen_.fill_rect(64, y, 192, 16, 15);  // the paper between the edge tiles is plain yellow
        tile(5 + (r % 3), 256, y);
    }
    screen_.reset_clip();
    auto roller = [&](int y) {
        for (int c = 0; c < 14; c++) tile(8 + (c % 4), 48 + c * 16, y);
        int f = turbo_ ? 0 : (int)((steps_ / 3) % 4);
        tile(16 + f, 32, y); tile(20 + f, 272, y);
    };
    roller(14);
    roller(29 + std::min(open, 74) - 4);
    if (!title.empty()) {
        // The banner is three qtiles: 13 the left cap, 14 the plain middle (repeated), 15 the right cap. (The earlier version alternated 14 and 15 across the
        // middle, which put right-cap pieces, like bars, through the title.)
        tile(13, 72, -2); for (int c = 0; c < 9; c++) tile(14, 88 + c * 16, -2); tile(15, 232, -2);
        int tx = 160 - (int)title.size() * 4;
        screen_.text(tx, 2, title.c_str(), 0);
    }
}

static void draw_flame(Screen& s, int x, int y, uint8_t col) {  // the small flame/hand mark in front of each spell
    static const char* art[] = {"...X....", "..XX....", "..XXX...", ".XXXXX..", ".XXXXX..", ".XXXXX..", "..XXX..."};
    for (int r = 0; r < 7; r++) for (int c = 0; c < 8; c++) if (art[r][c] == 'X') s.px[(y + r) * kW + x + c] = col;
}

// Settings: a plain modern page that works with the mouse (click) or the keyboard (up/down, Enter). Row layout is shared by drawing and hit testing.
struct SRow { int y, h; int id; };  // ids: 0 original controls, 1 16-colour, 2 256-colour (disabled), 3 back
static const SRow kSRows[] = {{22, 12, 0}, {46, 12, 1}, {60, 12, 5}, {74, 12, 6}, {88, 12, 7}, {106, 14, 3}};   // ids: 0 controls, 1 16-colour, 5 music kind, 6 music on / off, 7 sound effects on / off, 3 back

void Game::step_settings(const Input& in) {
    bool edge = in.confirm, up = in.up, down = in.down, cancel = in.cancel;
    int hit = -1;
    if (in.mx >= 0) {
        mouse_x_ = in.mx; mouse_y_ = in.my;
        for (auto& r : kSRows) if (in.my >= r.y - 2 && in.my < r.y + r.h && in.mx >= 20 && in.mx < 300) hit = r.id;
        if (hit >= 0) settings_focus_ = hit;
    }
    auto activate = [&](int id) {
        if (id == 0) { orig_controls_ = !orig_controls_; save_settings(); }
        else if (id == 1) { gfx_mode_ = 0; save_settings(); }
        else if (id == 5) { set_music_enhanced(!music_enhanced_); save_settings(); }
        else if (id == 6) { music_on_ = !music_on_; if (mode_ == Mode::Title) { stop_sound(); play_title(); } save_settings(); }
        else if (id == 7) { sfx_on_ = !sfx_on_; save_settings(); }
        else if (id == 3) { menu_page_ = -1; }
    };
    if (in.click && hit >= 0) activate(hit);
    static const int order[6] = {0, 1, 5, 6, 7, 3};
    int pos = 0;
    for (int k = 0; k < 6; k++) if (order[k] == settings_focus_) pos = k;
    if (up) settings_focus_ = order[(pos + 5) % 6];
    if (down) settings_focus_ = order[(pos + 1) % 6];
    if (edge) activate(settings_focus_);
    if (cancel) menu_page_ = -1;
}

void Game::draw_settings() {
    screen_.reset_clip();
    screen_.fill_rect(0, 0, kW, 128, 8);          // flat dark panel, deliberately unlike the scrolls
    screen_.fill_rect(10, 4, 300, 122, 0);
    screen_.fill_rect(10, 4, 300, 12, 7);
    screen_.text(130, 6, "SETTINGS", 0);
    auto box = [&](int x, int y, bool checked, bool hot) {
        screen_.fill_rect(x, y, 10, 10, hot ? 15 : 7);
        screen_.fill_rect(x + 1, y + 1, 8, 8, 0);
        if (checked) screen_.fill_rect(x + 3, y + 3, 4, 4, 13);
    };
    auto row = [&](int id) -> const SRow& { for (auto& r : kSRows) if (r.id == id) return r; return kSRows[0]; };
    auto option = [&](int id, bool checked, const char* text) {
        const SRow& r = row(id); bool hot = settings_focus_ == id;
        if (hot) screen_.fill_rect(14, r.y - 2, 292, r.h + 2, 4);
        box(24, r.y, checked, hot);
        screen_.text(42, r.y + 1, text, hot ? 15 : 7);
    };
    { const SRow& r = row(0); bool hot = settings_focus_ == 0;   // controls (two lines)
      if (hot) screen_.fill_rect(14, r.y - 2, 292, r.h + 10, 4);
      box(24, r.y, orig_controls_, hot);
      screen_.text(42, r.y + 1, orig_controls_ ? "Original controls" : "Updated controls", hot ? 15 : 7);
      screen_.text(42, r.y + 12, orig_controls_ ? "keypad: 8, 2 turn; 7, 9 jump" : "instant moves, smooth jumps", 8); }
    option(1, gfx_mode_ == 0, "Original 16-colour graphics (EGA)");
    option(5, music_enhanced_, music_enhanced_ ? "Music style: enhanced (orchestral)" : "Music style: original (PC speaker)");
    option(6, music_on_, music_on_ ? "Music: on" : "Music: off");
    option(7, sfx_on_, sfx_on_ ? "Sound effects: on" : "Sound effects: off");
    { const SRow& r = row(3); bool hot = settings_focus_ == 3;
      screen_.fill_rect(24, r.y, 150, r.h, hot ? 15 : 7);
      screen_.fill_rect(25, r.y + 1, 148, r.h - 2, hot ? 4 : 0);
      screen_.text(40, r.y + 3, "Back to main menu", hot ? 15 : 7); }
    draw_panel();
}

// ---- Dead creatures. Most dither away. A troll (unless it was killed by fire, the cluebook's way) crashes into a green heap, lies there for ten seconds and then the
// heap builds itself back into a troll, the crash played backwards, and it gets up to fight on.
static const int kCrashSteps = 6, kPileSteps = 60, kFadeSteps = 8;

// How the dead go (the manual): the Baaz crumble to dust, the Kapak dissolve (into a puddle of acid), the Bozak's flesh dries and their bones explode, hurting anyone too close;
// a troll falls and rises again; the rest dither away.
void Game::reap_dead() {
    bool boomed = false;
    const int hxv = interior() ? istrip_x() : cx_;
    std::vector<Enemy> blasts;
    for (auto& e : enemies_) {
        if (e.hp > 0) continue;
        if (interior() && e.tag >= 0 && e.tag < (int)ienemies_.size()) ienemies_[e.tag].hp = 0;   // it is dead for the level too
        const std::string& nm = mtypes_[e.type].name;
        if (nm == "Bozak" && !e.nocorpse) { blasts.push_back(e); continue; }
        if (nm == "Dragon" && interior() && e.hlx >= 0) {   // the dragon is dead: its body, part of the lair's picture, goes with its head
            Level& L = levels_[level_]; int hx = e.hlx >> 7, hy = e.hly >> 7;
            if (hx >= 0 && hy >= 0 && hx < L.w && hy < L.h && L.cells[hy * L.w + hx] == 90) { L.cells[hy * L.w + hx] = 5; istrip_key_ = -1; }
        }
        if (!e.nocorpse) {
            Corpse c; c.e = e; c.troll = nm == "Troll" && !e.burned;
            c.kind = nm == "Baaz" ? 1 : nm == "Kapak" ? 2 : 0;
            if (c.kind == 1) fx(kFxCrumble); else if (c.kind == 2) fx(kFxAcid);
            corpses_.push_back(c);
        }
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.hp <= 0; }), enemies_.end());
    for (auto& b : blasts) {   // the explosion: a burst of bone, 2 to 12 points to the hero and to every foe within 48 px
        boomed = true;
        add_impact(b, 5);
        fx(kFxBoneBurst);
        if (abs(b.x - hxv) < 48 && !god()) { party_[leader_].hp = std::max(0, party_[leader_].hp - rnd(2, 12)); add_spark_hero(); fx(kFxPcHit, 0.7); }
        for (auto& o : enemies_) if (o.hp > 0 && abs(o.x - b.x) < 48) { int d = rnd(2, 12); o.hp -= d; add_floater(o, d); add_spark(o); if (o.hp <= 0) { const MonsterType& mt = mtypes_[o.type]; if (!o.revived) xp_ += mt.xp; kills_++; } }
    }
    if (boomed) reap_dead();   // the blast may have killed others
}

bool Game::troll_pending() const { for (auto& c : corpses_) if (c.troll) return true; return false; }

void Game::step_corpses() {
    for (size_t i = 0; i < corpses_.size();) {
        Corpse& c = corpses_[i];
        c.t++;
        if (c.kind == 2 && c.t >= 6 && c.t < 30 && c.t % 6 == 0) {   // the acid pool burns whoever stands in it: 1-8 a round for a few rounds, the foes too unless they fly
            int hx = interior() ? istrip_x() : cx_;
            if (!god() && abs(hx - c.e.x) < 24) { party_[leader_].hp = std::max(0, party_[leader_].hp - rnd(1, 8)); add_spark_hero(); fx(kFxPcHit, 0.7); message_ = "The acid burns!"; message_steps_ = 10; }
            for (auto& o : enemies_) {
                if (o.hp <= 0 || abs(o.x - c.e.x) >= 24) continue;
                const std::string& on = mtypes_[o.type].name;
                if (on == "Stirge" || on == "Giant Wasp" || on == "Griffon" || on == "Dragon") continue;   // fliers pass over the pool
                int d = rnd(1, 8); o.hp -= d; add_floater(o, d); add_spark(o);
                if (o.hp <= 0) { if (!o.revived) { xp_ += mtypes_[o.type].xp; party_[leader_].xp += mtypes_[o.type].xp; } kills_++; }
            }
        }
        if (!c.troll && c.t >= (c.kind == 1 ? baaz_statue_steps(c.e) + 12 : c.kind == 2 ? 40 : kFadeSteps)) { corpses_.erase(corpses_.begin() + i); continue; }
        if (c.troll && c.t >= kCrashSteps * 2 + kPileSteps) {   // it gets up
            if (interior() && c.e.tag >= 0 && c.e.tag < (int)ienemies_.size()) {   // a dungeon troll gets up where it fell
                Enemy& o = ienemies_[c.e.tag]; o.hp = std::max(1, c.e.hpmax); o.revived = true; o.frozen = 0; o.cooldown = 12;
                corpses_.erase(corpses_.begin() + i); continue;
            }
            Enemy e = c.e; e.hp = std::max(1, e.hpmax); e.striking = false; e.cooldown = 12; e.frozen = 0; e.anim = 0; e.revived = true;
            enemies_.push_back(e);
            corpses_.erase(corpses_.begin() + i); continue;
        }
        i++;
    }
}

// The troll's heap: its standing picture squashed down and spread out, turning green; progress p = 0 (standing) to 1 (a heap). The crash and the regrowth are the same frames.
static Image squash_to_pile(const Image& src, double p, uint8_t ca = 10, uint8_t cb = 2) {
    Image out = src;
    std::fill(out.px.begin(), out.px.end(), 0);
    int top = src.h, bottom = -1, left = src.w, right = -1;
    for (int y = 0; y < src.h; y++) for (int x = 0; x < src.w; x++) if (src.px[y * src.w + x]) { top = std::min(top, y); bottom = std::max(bottom, y); left = std::min(left, x); right = std::max(right, x); }
    if (bottom < 0) return out;
    double scale = 1.0 - 0.82 * p, spread = 1.0 + 0.45 * p, cx = (left + right) / 2.0, hh = (bottom - top + 1) * scale, rx = (right - left + 1) / 2.0 * spread;
    for (int y = 0; y < src.h; y++) {
        int sy = bottom - (int)std::lround((bottom - y) / scale);
        if (y > bottom || sy < top || sy > bottom) continue;
        for (int x = 0; x < src.w; x++) {
            int sx = (int)std::lround(cx + (x - cx) / spread);
            if (sx < 0 || sx >= src.w) continue;
            int v = src.px[sy * src.w + sx];
            if (!v) continue;
            if (p > 0.55) {   // the heap is a lumpy mound
                double ex = (x - cx) / rx, ey = (bottom - y) / std::max(2.0, hh * (1.0 + 0.6 * (1 - p)));
                if (ex * ex + ey * ey > 1.0 + (1 - p) * 3 + ((x * 7 + y * 3) % 5) * 0.04) continue;
            }
            unsigned hsh = (unsigned)(x * 73856093 ^ y * 19349663); hsh ^= hsh >> 11;
            if (p > 0.2 && (hsh % 100) < (unsigned)std::min(100.0, (p - 0.2) / 0.5 * 100.0)) v = ((x + y) % 3 == 0) ? ca : cb;   // turning green (or whatever colour is asked for)
            out.px[y * src.w + x] = (uint8_t)v;
        }
    }
    return out;
}

Image Game::corpse_image(const Corpse& c, int& dither) const {
    const MonsterType& t = mtypes_[c.e.type];
    const auto& seq = t.walk;
    size_t fr = seq.empty() ? 0 : (size_t)seq[0];
    if (fr >= t.actor.frames.size()) fr = 0;
    const Image& base = t.actor.frames[fr];
    dither = 16;
    if (c.kind == 1 && c.t < baaz_statue_steps(c.e)) {   // first it is a statue of dull metal, weapon-sticking and all
        Image out = base;
        for (int y = 0; y < out.h; y++) for (int x = 0; x < out.w; x++) {
            uint8_t& v = out.px[y * out.w + x]; if (!v) continue;
            v = (v == 3 || v == 15) ? 3 : ((x * 3 + y) % 7 == 0) ? 3 : (y % 2 ? 7 : 8);   // grey metal with a few highlights
        }
        dither = 16;
        return out;
    }
    if (c.kind == 1) {   // a Baaz crumbles to dust: eaten away from the head down, grey flecks falling, a little heap of dust left that blows away
        const int ct = c.t - baaz_statue_steps(c.e);   // steps since the statue began to crumble
        double p = std::clamp((ct + 1) / 9.0, 0.0, 1.0);
        Image out = base; std::fill(out.px.begin(), out.px.end(), 0);
        int top = out.h, bottom = -1, left = out.w, right = -1;
        for (int y = 0; y < base.h; y++) for (int x = 0; x < base.w; x++) if (base.px[y * base.w + x]) { top = std::min(top, y); bottom = std::max(bottom, y); left = std::min(left, x); right = std::max(right, x); }
        if (bottom < 0) return base;
        double cut = top + p * (bottom - top + 1) * 1.05;
        for (int y = 0; y < base.h; y++) for (int x = 0; x < base.w; x++) {
            uint8_t v = base.px[y * base.w + x]; if (!v) continue;
            unsigned hsh = (unsigned)(x * 73856093 ^ y * 19349663 ^ (ct / 2) * 83492791); hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
            double ragged = (int)(hsh % 5) - 2;
            if (y < cut + ragged) continue;
            if ((hsh % 100) < (unsigned)(p * 30)) continue;   // crumbling away below the edge too
            out.px[y * out.w + x] = v;
        }
        int cxm = (left + right) / 2, hw = std::max(6, (right - left) / 2 + 2);
        double hmax = 2 + p * 8;
        for (int x = -hw; x <= hw; x++) {   // the heap of dust
            int hgt = (int)std::lround(hmax * (1 - (double)(x * x) / (hw * hw)));
            for (int k = 0; k < hgt; k++) { int xx = cxm + x, yy = bottom - k; if (xx >= 0 && xx < out.w && yy >= 0 && yy < out.h) out.px[yy * out.w + xx] = ((x + k) % 3 == 0) ? 8 : 7; }
        }
        for (int k = 0; k < 16; k++) {   // flecks of dust coming down
            unsigned hsh = (unsigned)(k * 2654435761u + ct * 40503u); hsh ^= hsh >> 11;
            int xx = left + (int)(hsh % (unsigned)std::max(1, right - left + 1)), yy = (int)cut + (int)((hsh >> 8) % (unsigned)std::max(1, bottom - (int)cut + 1));
            if (yy >= 0 && yy < out.h && xx >= 0 && xx < out.w && yy > cut - 3) out.px[yy * out.w + xx] = k % 2 ? 7 : 8;
        }
        dither = ct < 8 ? 16 : std::clamp(16 - (ct - 8) * 4, 0, 16);   // the dust blows away at the end
        return out;
    }
    if (c.kind == 2) {   // a Kapak dissolves: it melts down into a puddle of acid that bubbles for a while and then dries up
        double p = std::clamp((c.t + 1) / 8.0, 0.0, 1.0);
        Image out = squash_to_pile(base, p, 13, 14);
        if (c.t >= 8) {
            int top = out.h, bottom = -1, left = out.w, right = -1;
            for (int y = 0; y < out.h; y++) for (int x = 0; x < out.w; x++) if (out.px[y * out.w + x]) { top = std::min(top, y); bottom = std::max(bottom, y); left = std::min(left, x); right = std::max(right, x); }
            for (int k = 0; k < 7 && bottom >= 0; k++) {   // bubbles popping
                unsigned hsh = (unsigned)(k * 2654435761u + (c.t / 2) * 97531u); hsh ^= hsh >> 12;
                int xx = left + (int)(hsh % (unsigned)(right - left + 1)), yy = top + (int)((hsh >> 9) % (unsigned)(bottom - top + 1));
                if (out.px[yy * out.w + xx]) out.px[yy * out.w + xx] = (k + c.t) % 2 ? 3 : 1;
            }
        }
        dither = c.t < 32 ? 16 : std::clamp(16 - (c.t - 32) * 2, 0, 16);   // the puddle dries up
        return out;
    }
    if (!c.troll) { dither = std::clamp(16 - c.t * 16 / kFadeSteps, 0, 16); return base; }
    if (!t.death.empty() && t.actor.frames.size() > (size_t)t.death.back()) {   // the sheet's own death frames, played down, held, and played back up
        int n = (int)t.death.size(), k;
        if (c.t < kCrashSteps) k = c.t * n / kCrashSteps;
        else if (c.t < kCrashSteps + kPileSteps) k = n - 1;
        else k = n - 1 - (c.t - kCrashSteps - kPileSteps) * n / kCrashSteps;
        return t.actor.frames[t.death[std::clamp(k, 0, n - 1)]];
    }
    double p;
    if (c.t < kCrashSteps) p = double(c.t + 1) / kCrashSteps;                                   // crashing down
    else if (c.t < kCrashSteps + kPileSteps) p = 1;                                              // lying there
    else p = 1.0 - double(c.t - kCrashSteps - kPileSteps + 1) / kCrashSteps;                    // and back up
    return squash_to_pile(base, std::clamp(p, 0.0, 1.0));
}

// The three army fires (sbits 1 small, 2 medium, 3 large) get eight frames each, made from the original pixels: the flames sway more towards the tips, the tips stretch and
// shrink, and a few of the hottest pixels flicker. The logs at the foot stay put.
void Game::build_fire_frames() {
    if (sbits_.size() < 4) return;
    for (int s = 0; s < 3; s++) {
        const Image& src = sbits_[s + 1];
        int base = -1, top = src.h;   // base: the row where the flames meet the logs (the lowest row that is not log brown); top: the highest lit row
        for (int y = 0; y < src.h; y++) for (int x = 0; x < src.w; x++) if (src.px[y * src.w + x]) { top = std::min(top, y); }
        for (int y = src.h - 1; y >= 0 && base < 0; y--) { int n = 0; for (int x = 0; x < src.w; x++) if (src.px[y * src.w + x] == 15 || src.px[y * src.w + x] == 3 || src.px[y * src.w + x] == 11) n++; if (n) base = y; }
        if (base < 0) base = src.h - 3;
        const double amp = 0.9 + 0.5 * s;
        for (int f = 0; f < 8; f++) {
            Image im = src;
            std::fill(im.px.begin(), im.px.end(), 0);
            double ph = 2 * 3.14159265 * f / 8, stretch = 1.0 + 0.10 * std::sin(ph + s);
            for (int y = 0; y < src.h; y++) {
                bool logs = y > base;
                double rel = logs ? 0 : double(base - y) / std::max(1, base - top);   // 0 at the foot, 1 at the tip
                int sy = logs ? y : base - (int)std::lround((base - y) / stretch);       // the source row for this row (the flames grow and shrink)
                if (sy < 0 || sy >= src.h) continue;
                int dx = logs ? 0 : (int)std::lround(amp * rel * rel * std::sin(ph + y * 0.9 + s * 1.7));
                for (int x = 0; x < src.w; x++) {
                    int v = src.px[sy * src.w + x];
                    if (!v) continue;
                    if (!logs) {
                        unsigned h = (unsigned)(f * 7919 + x * 104729 + y * 1299709 + s * 15485863); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                        if (h % 6 == 0) v = v == 15 ? 3 : v == 3 ? 15 : v == 12 ? 11 : v == 11 ? 12 : v;   // flicker
                    }
                    int nx = x + dx;
                    if (nx >= 0 && nx < im.w) im.px[y * im.w + nx] = (uint8_t)v;
                }
            }
            fire_[s][f] = im;
        }
    }
}

// The quest map: the 20x24 map of qtiles (numbers are 1-based) in a 128-px window, fire marks (sbits 1-3) where the dragon armies are,
// and the party. The window starts at (party y / 8) - 64, limited to 0..256 (fn 4B1E).
// The whole of the explored dungeon level over the full screen (Back in a dungeon): grey = seen from a passage, blue with white wall lines = been there, the arrow is the party.
// The shortest way between two cells of the current level (breadth first). Doors are treated as open and secret doors as found: a door cell's exits are those of its open twin
// (the cell values come in threes: hidden, shut, open; secret_flags low bits 1, 2, 3).
std::vector<std::pair<int, int>> Game::dungeon_route(int sx, int sy, int tx, int ty) const {
    const Level& L = levels_[level_];
    std::vector<std::pair<int, int>> path;
    if (L.w <= 0 || sx < 0 || sy < 0 || sx >= L.w || sy >= L.h) return path;
    auto eff = [&](int x, int y) {
        int v = L.cells[y * L.w + x];
        if (!v) return 0;
        int fl = v < (int)isecret_.size() ? isecret_[v] : 0, kind = fl & 7;
        int open = (kind >= 1 && kind <= 3) ? v + (3 - kind) : v;
        return iexits_[std::min(open, (int)iexits_.size() - 1)] | iexits_[std::min(v, (int)iexits_.size() - 1)];
    };
    static const int dx4[4] = {0, 1, 0, -1}, dy4[4] = {-1, 0, 1, 0}, bit4[4] = {1, 2, 4, 8}, opp4[4] = {4, 8, 1, 2};
    for (int strict = 1; strict >= 0; strict--) {
        std::vector<int> prev((size_t)L.w * L.h, -2);
        std::vector<int> q; q.push_back(sy * L.w + sx); prev[sy * L.w + sx] = -1;
        for (size_t qi = 0; qi < q.size() && prev[ty * L.w + tx] == -2; qi++) {
            int cx = q[qi] % L.w, cy = q[qi] / L.w;
            for (int d = 0; d < 4; d++) {
                int nx = cx + dx4[d], ny = cy + dy4[d];
                if (nx < 0 || ny < 0 || nx >= L.w || ny >= L.h || !L.cells[ny * L.w + nx] || prev[ny * L.w + nx] != -2) continue;
                bool a = eff(cx, cy) & bit4[d], b = eff(nx, ny) & opp4[d];
                if (strict ? (a && b) : (a || b)) { prev[ny * L.w + nx] = cy * L.w + cx; q.push_back(ny * L.w + nx); }
            }
        }
        if (prev[ty * L.w + tx] != -2) {
            for (int c = ty * L.w + tx; c != -1; c = prev[c]) path.push_back({c % L.w, c / L.w});
            std::reverse(path.begin(), path.end());
            return path;
        }
    }
    return path;
}

void Game::draw_dungeon_map() {
    screen_.reset_clip();
    screen_.clear(0);
    const Level& L = levels_[level_];
    auto& st = dmap_[level_];
    if (L.w <= 0 || L.h <= 0 || (int)st.size() != L.w * L.h) { screen_.text(80, 90, "Nothing explored yet", 7, false); return; }
    const int cell = std::max(2, std::min((kW - 8) / L.w, (kH - 26) / L.h));
    const int ox = (kW - cell * L.w) / 2, oy = 14;
    char t[32]; snprintf(t, sizeof t, "LEVEL %d", level_);
    screen_.text(4, 2, t, 15, false);
    screen_.text(kW - 4 - 8 * 31, 2, "Back / Esc / A: close the map", 8, false);
    auto exits = [&](int x, int y) { int v = L.cells[y * L.w + x]; return v ? iexits_[std::min(v, (int)iexits_.size() - 1)] : 0; };
    for (int y = 0; y < L.h; y++)
        for (int x = 0; x < L.w; x++) {
            int s = st[y * L.w + x];
            if (!s || !L.cells[y * L.w + x]) continue;
            int sx = ox + x * cell, sy = oy + y * cell, e = exits(x, y);
            screen_.fill_rect(sx, sy, cell, cell, s == 2 ? 10 : 8);
            if (s == 2) {
                if (!(e & 1)) screen_.fill_rect(sx, sy, cell, 1, 3);
                if (!(e & 4)) screen_.fill_rect(sx, sy + cell - 1, cell, 1, 3);
                if (!(e & 8)) screen_.fill_rect(sx, sy, 1, cell, 3);
                if (!(e & 2)) screen_.fill_rect(sx + cell - 1, sy, 1, cell, 3);
            }
        }
    auto cross = [&](int cx0, int cy0, uint8_t col) {   // an X over a cell of the map
        int mx = ox + cx0 * cell + cell / 2, my = oy + cy0 * cell + cell / 2, r = std::max(3, cell / 2 + 1);
        for (int i = -r; i <= r; i++) for (int k = 0; k < 2; k++) {
            screen_.fill_rect(mx + i, my + i + k, 1, 1, col); screen_.fill_rect(mx - i, my + i + k, 1, 1, col);
        }
    };
    bool any_exit = false, any_end = false;
    for (auto& l : links_)   // the ways to other levels (and out of Sla-Mori), marked whether or not they have been seen
        if (l.from == level_ && l.mode >= 2) { cross(((l.x1 + l.x2) / 2) >> 7, ((l.y1 + l.y2) / 2) >> 7, 3); any_exit = true; }
    for (auto& g : items_)   // id 58: the south door of Pax Tharkas, the end of the quest
        if (g.id == 58 && g.level == level_) { cross(g.x >> 7, g.y >> 7, 6); any_end = true; }
    if (any_exit) screen_.text(4, kH - 20, "White X: the way to another level", 3, false);
    if (any_end) screen_.text(4, kH - 10, "Red X: the end of the way (the south door)", 6, false);
    screen_.text(4, kH - 30, "Dotted line: the quickest way on.  ?: a secret door (press A beside it)", 8, false);
    {   // a light dotted arrow along the quickest way to the next level (or, on level 7, to the end), through doors that are shut or secret
        static const int forward[8] = {0, 0, 3, 4, 6, 0, -1, 4};   // level -> the level the main way leads to; level 6 holds the south door (the end), level 7 is a side level
        int tx = -1, ty = -1;
        if (forward[level_] < 0) { for (auto& g : items_) if (g.id == 58 && g.level == level_) { tx = g.x >> 7; ty = g.y >> 7; } }
        else for (auto& l : links_) if (l.from == level_ && l.to == forward[level_] && l.mode == 3) { tx = ((l.x1 + l.x2) / 2) >> 7; ty = ((l.y1 + l.y2) / 2) >> 7; }
        if (tx >= 0 && ty >= 0 && L.cells[ty * L.w + tx]) {
            auto route = dungeon_route(ix_ >> 7, iy_ >> 7, tx, ty);
            std::vector<std::pair<int, int>> pts;   // screen points along the route: from the party to the middles of the cells
            pts.push_back({ox + (ix_ * cell) / 128, oy + (iy_ * cell) / 128});
            for (auto& c : route) pts.push_back({ox + c.first * cell + cell / 2, oy + c.second * cell + cell / 2});
            for (auto& c : route) {   // a "?" where the way goes through a secret door
                int v = L.cells[c.second * L.w + c.first], fl = v < (int)isecret_.size() ? isecret_[v] : 0;
                if ((fl & 7) == 1) screen_.text(ox + c.first * cell + cell / 2 - 3, oy + c.second * cell + cell / 2 - 4, "?", 15, false);
            }
            double acc = 0; int n = 0;
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                double dx = pts[i + 1].first - pts[i].first, dy = pts[i + 1].second - pts[i].second, len = std::sqrt(dx * dx + dy * dy);
                for (double s = 0; s < len; s += 1) {
                    acc += 1;
                    if (acc >= 4) { acc = 0; int x = pts[i].first + (int)std::lround(dx * s / len), y = pts[i].second + (int)std::lround(dy * s / len); screen_.fill_rect(x, y, 1, 1, (n++ + render_frames_ / 8) % 3 ? 3 : 15); }
                }
            }
            if (pts.size() >= 2) {   // the arrowhead on the last step
                auto a = pts[pts.size() - 2], b = pts.back();
                double dx = b.first - a.first, dy = b.second - a.second, len = std::max(1.0, std::sqrt(dx * dx + dy * dy)); dx /= len; dy /= len;
                for (int k = 0; k < 4; k++) for (int w = -k; w <= k; w++) {
                    int x = b.first - (int)std::lround(dx * (5 - k) + dy * w * 0.8) - (int)std::lround(dx * 2), y = b.second - (int)std::lround(dy * (5 - k) - dx * w * 0.8) - (int)std::lround(dy * 2);
                    screen_.fill_rect(x, y, 1, 1, 15);
                }
            }
        }
    }
    int pxc = ox + (ix_ * cell) / 128, pyc = oy + (iy_ * cell) / 128;   // the party: a blinking cross-hair of yellow
    uint8_t c = (render_frames_ / 15) % 2 ? 15 : 3;
    screen_.fill_rect(pxc - 3, pyc, 7, 1, c); screen_.fill_rect(pxc, pyc - 3, 1, 7, c);
    for (auto& p : npcs_) {   // a freed or waiting prisoner is not marked: the map shows only what has been walked
        (void)p;
    }
}

// The cave as a long bar: portal at the left, the gold archway between the two rooms, the chest, foes (red) and the party (white).
static void lair_bar(Screen& s, int x, int y, int w, int h, int x0, int x1, int mid, int chest, bool taken, int px, const std::vector<Enemy>& en, bool big) {
    auto X = [&](int v) { return x + (int)((long long)(v - x0) * (w - 1) / std::max(1, x1 - x0)); };
    s.fill_rect(x - 1, y - 1, w + 2, h + 2, 15); s.fill_rect(x, y, w, h, 8);
    s.fill_rect(x, y + h / 2 - 1, w, 3, 7);                        // the floor of the passage
    int m = X(mid); s.fill_rect(m - 1, y, 3, h, 14);               // the archway
    s.fill_rect(x, y, 3, h, 10);                                    // the portal out
    if (!taken) s.fill_rect(X(chest) - 1, y + h / 2 - 2, 4, 4, 14);
    for (auto& e : en) if (e.hp > 0) s.fill_rect(X(e.x), y + h / 2 - 1, 2, 2, 12);
    s.fill_rect(X(px) - 1, y + h / 2 - 2, 3, 4, 15);
    (void)big;
}
void Game::draw_lair_minimap() {
    lair_bar(screen_, kW - 84, 4, 80, 8, lair_x0_, lair_x1_, lair_mid(), lair_chest_x_, lair_chest_taken_, cx_, enemies_, false);
}
void Game::draw_lair_map() {
    screen_.reset_clip(); screen_.fill_rect(0, 0, kW, 128, 0);
    screen_.text(kW / 2 - 36, 6, "THE LAIR", 14, true);
    lair_bar(screen_, 16, 40, kW - 32, 28, lair_x0_, lair_x1_, lair_mid(), lair_chest_x_, lair_chest_taken_, cx_, enemies_, true);
    screen_.text(16, 72, "Green: portal out (west end)", 10, false);
    screen_.text(16, 82, "Gold bar: archway to the 2nd room", 14, false);
    screen_.text(16, 92, lair_chest_taken_ ? "Treasure taken" : "Gold box: the treasure", 14, false);
    screen_.text(16, 102, "Red: foes   White: your party", 15, false);
    screen_.text(16, 116, "Back to close", 7, false);
}
void Game::draw_quest_map() {
    if (lair_active_) { draw_lair_map(); return; }
    if (interior()) { draw_dungeon_map(); return; }
    screen_.reset_clip();
    screen_.fill_rect(0, 0, kW, 128, 0);
    screen_.set_clip(0, 0, kW, 128);
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 20; c++) {
            int y = r * 16 - qm_top_;
            if (y <= -16 || y >= 128) continue;
            int t = qm_terrain_[r * 20 + c];
            if (t >= 1 && t <= (int)qtiles_.size()) screen_.blit(qtiles_[t - 1], c * 16, y);
            int a = (qm_armies_[r * 20 + c] & 0x60) >> 5;
            if (a >= 1 && a <= 3 && !fire_[a - 1][0].empty()) {   // each fire burns at its own pace: 100 ms a frame plus 0-50 ms (steps of 5), so they drift apart
                unsigned hsh = (unsigned)(r * 31 + c * 17); hsh = (hsh * 2654435761u) >> 16;
                double period = 100.0 + 5.0 * (hsh % 11);
                int frame = (int)(render_frames_ * (1000.0 / 60.0) / period) % 8;
                screen_.blit(fire_[a - 1][frame], c * 16, y, true);
            } else if (a >= 1 && a < (int)sbits_.size()) screen_.blit(sbits_[a], c * 16, y, true);
        }
    int px = interior() ? 72 : px_ / 8, py = interior() ? 360 : py_ / 8;
    if (!sbits_.empty()) screen_.blit(sbits_[0], px - 8, py - qm_top_ - 8, true);  // the party: sbits sprite 0, four arrows pointing inwards
    if (!pois_loaded_) load_pois();
    for (auto& p : pois_) {   // the places of interest not yet visited: a dot (yellow for towns, red for lairs and camps, cyan for the rest)
        if (p.level != 1 || poi_done_.count(p.name)) continue;
        int x = p.x / 8, y = p.y / 8 - qm_top_;
        if (x < 1 || x >= kW - 1 || y < 1 || y >= 127) continue;
        uint8_t col = p.icon == "town" || p.icon == "village" ? 15 : (p.icon == "cave" || p.icon == "den" || p.icon == "camp") ? 6 : 1;
        screen_.fill_rect(x - 1, y - 1, 3, 3, 0); screen_.px[y * kW + x] = col; screen_.px[(y - 1) * kW + x] = col; screen_.px[(y + 1) * kW + x] = col; screen_.px[y * kW + x - 1] = col; screen_.px[y * kW + x + 1] = col;
    }
    for (auto& l : links_) {   // a pulsing X where the way into Sla-Mori is (world coordinates / 8 = quest map pixels), once the party knows where it is
        if (l.from != 1 || l.mode != 3 || !entrance_known_) continue;
        int xc = (l.x1 / 8 + 16 + 8) / 8, yc = (l.y1 / 8 + 8) / 8 - qm_top_;
        int r = 5 + (int)(3 * (1 + std::sin(render_frames_ * 0.18)));   // 5..11 px, about twice a second (drawn over the party's marker)
        uint8_t col = (render_frames_ / 8) % 2 ? 3 : 15;
        for (int i = -r; i <= r; i++)
            for (int k = 0; k < 2; k++) {
                int xs[2] = {xc + i, xc - i}, ys = yc + i;
                for (int d = 0; d < 2; d++) {
                    int xx = xs[d] + k, yy = ys + (k ? 0 : 0);
                    if (xx >= 0 && xx < kW && yy >= 0 && yy < 128) screen_.px[yy * kW + xx] = k ? 5 : col;
                }
            }
    }
    screen_.reset_clip();
    draw_panel();
}

void Game::draw_menu() {
    screen_.reset_clip();
    char b[96];
    auto list = [&](const std::vector<std::string>& rows, int sel, int first_line) {
        int first = std::clamp(sel - 7, 0, std::max(0, (int)rows.size() - 8));
        for (int i = 0; i < 8 && first + i < (int)rows.size(); i++)
            screen_.text(60, 24 + (first_line + i) * 10, rows[first + i].c_str(), first + i == sel ? 5 : 10);
    };
    if (menu_page_ == kPageSettings) { draw_settings(); return; }
    if (menu_page_ == kPageEquip || menu_page_ == kPageEquipPick) { draw_equip(); draw_panel(); return; }
    if (menu_page_ == kPageInventory) { draw_inventory(); draw_panel(); return; }
    if (menu_page_ < 0) {
        draw_scroll("MAIN MENU", 8);
        std::vector<int> entries = menu_entries();
        int en = (int)entries.size();
        std::vector<std::string> rows;
        for (int e : entries) rows.push_back(e < kMenuN ? kMenu[e] : e == 20 ? "Equip" : e == 21 ? "Inventory" : "Controls (F1)");
        int sel = std::clamp(menu_item_, 0, en - 1);
        int first = std::clamp(sel - 7, 0, std::max(0, en - 8));
        for (int i = 0; i < 8 && first + i < en; i++) screen_.text(88, 31 + i * 9, rows[first + i].c_str(), first + i == sel ? 5 : 10);
    } else if (menu_page_ == kPageClerical || menu_page_ == kPageMagic) {
        bool cl = menu_page_ == kPageClerical;
        draw_scroll(cl ? "CLERICAL SPELLS" : "MAGIC USER SPELLS", 8);
        std::vector<std::string> names; std::vector<int> kinds;  // kind: 0 plain line, 1 spell with icon, 2 separator
        if (cl) {
            for (auto& r : cleric_rows(used_)) {
                if (r.second == 0) { names.push_back("Exit menu"); kinds.push_back(0); }
                else if (r.second == 2) { names.push_back("--- used ---"); kinds.push_back(2); }
                else {
                    const SpellInfo& sp = kClerical[kClericalEntry[r.first]];
                    std::string cost = sp.mark == 'o' ? " (" + std::to_string(sp.cost) + ")" : "";
                    names.push_back(std::string(sp.name) + cost); kinds.push_back(1);
                }
            }
        } else {
            names.push_back("Exit menu"); kinds.push_back(0);
            for (int i = 0; i < 9; i++) {
                names.push_back(kMagic[i].name); kinds.push_back(1);  // the original lists the names only
            }
        }
        int sel = menu_scroll_, n = (int)names.size();
        int first = std::clamp(sel - 7, 0, std::max(0, n - 8));
        for (int i = 0; i < 8 && first + i < n; i++) {
            int k = first + i, y = 31 + i * 9;
            uint8_t col = k == sel ? 5 : 10;
            if (kinds[k] == 1) { draw_flame(screen_, 88, y, col); screen_.text(97, y, names[k].c_str(), col); }
            else if (kinds[k] == 2) screen_.text(96, y, names[k].c_str(), 10);
            else screen_.text(88, y, names[k].c_str(), col);
        }
        snprintf(b, sizeof b, cl ? "Disks of Mishakal: %d" : "Staff of Magius: %d", cl ? disks_ : staff_);
        screen_.text(48, 112, b, 7);
    } else if (menu_page_ == kPageHero) {  // hero status, laid out like the original
        const Hero& h = party_[menu_hero_];
        draw_scroll(h.name.substr(0, h.name.find(' ')), 9);
        screen_.text(88, 28, "Min Max", 10); screen_.text(210, 28, "Min Max", 10);
        struct Row { const char* l; int v; int max; };
        auto ab = [&](int i) { return i < (int)h.abilities.size() ? h.abilities[i] : 0; };
        Row left[4] = {{"Str", ab(0), 20}, {"Wis", ab(2), 20}, {"Dex", ab(3), 20}, {"HP", h.hp, std::max(1, h.hp_max)}};
        Row right[4] = {{"Int", ab(1), 20}, {"Con", ab(4), 20}, {"Cha", ab(5), 20}, {"AC", ab(6), 10}};
        for (int i = 0; i < 4; i++) {
            for (int c = 0; c < 2; c++) {
                const Row& r = c ? right[i] : left[i];
                int x = c ? 156 : 52, y = 36 + i * 9;
                screen_.text(x, y, r.l, 10);
                screen_.fill_rect(x + 28, y + 1, 56, 5, 4);
                screen_.fill_rect(x + 28, y + 1, std::clamp(r.v * 56 / r.max, 0, 56), 5, 12);
            }
        }
        snprintf(b, sizeof b, "%d / %d", h.hp, h.hp_max); screen_.text(124, 72, b, 10);
        snprintf(b, sizeof b, "Using Range: %s", h.range_name.empty() ? "none" : h.range_name.c_str()); screen_.text(52, 82, b, 10);
        snprintf(b, sizeof b, "Hand: %s", h.hand_name.c_str()); screen_.text(100, 91, b, 10);
    } else if (menu_page_ == kPageUse || menu_page_ == kPageDrop) {
        draw_scroll(menu_page_ == kPageUse ? "USE" : "DROP", 8);
        std::vector<std::string> rows;
        for (int id : inv_) rows.push_back(item_name(id));
        list(rows, menu_scroll_, 0);
    } else if (menu_page_ == kPageTake) {  // what lies within reach
        draw_scroll("TAKE", 8);
        std::vector<std::string> rows;
        for (int i : items_near(false)) { const GroundItem& g = items_[i]; rows.push_back(item_name(g.id) + (g.qty > 1 && g.id < 19 ? " (" + std::to_string(g.qty) + ")" : "")); }
        list(rows, menu_scroll_, 0);
    } else if (menu_page_ == kPageQuest) {
        draw_quest_map(); return;
    } else if (menu_page_ == kPageXp) {  // experience points: kills of each monster kind
        draw_scroll("EXPERIENCE POINTS", 10);
        snprintf(b, sizeof b, "Party experience points: %d", xp_);
        screen_.text(44, 22, b, 10);
        std::vector<int> order;
        for (int i = 0; i < (int)mtypes_.size(); i++) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](int a, int c) { return mtypes_[a].slot < mtypes_[c].slot; });
        for (size_t k = 0; k < order.size(); k++) {
            int col = (int)k / 9, row = (int)k % 9;
            const MonsterType& t = mtypes_[order[k]];
            snprintf(b, sizeof b, "%-9.9s%3d", t.name.c_str(), order[k] < (int)kills_by_type_.size() ? kills_by_type_[order[k]] : 0);
            screen_.text(48 + col * 120, 33 + row * 9, b, 10);
        }
    }
    draw_panel();
}

// The inside of a lair: the dungeon's corridor wall (the picture of cell 5) recoloured to earth and rock, stalactites hanging from the roof, boulders along the floor, solid rock at both ends of the room.
void Game::draw_lair_background(int cam) {
    if (cave_tiles_.empty() && !utiles_.empty()) {
        for (const Image& t : utiles_) {
            Image c = t;
            for (size_t i = 0; i < c.px.size(); i++) {
                uint8_t v = c.px[i];
                int x = (int)(i % c.w), y = (int)(i / c.w);
                if (v == 7) v = ((x + y) % 5 == 0) ? 8 : 12;            // light stone becomes brown earth with darker flecks
                else if (v == 8) v = ((x * 3 + y) % 4 == 0) ? 12 : 8;   // dark stone: shadowed earth
                else if (v == 3 || v == 15) v = 12;
                c.px[i] = v;
            }
            cave_tiles_.push_back(c);
        }
    }
    const int block = utokens_.size() > 9 ? 9 : 0;
    for (int row = 0; row < 8; row++)
        for (int tx = cam >> 4; tx <= (cam + kW - 1) >> 4; tx++) {
            int t = utokens_.empty() ? 0 : utokens_[block][row * 8 + (tx & 7)];
            if (t >= 1 && t <= (int)cave_tiles_.size()) screen_.blit(cave_tiles_[t - 1], tx * 16 - cam, row * 16);
        }
    for (int y = 0; y < 112; y++) for (int x = 0; x < kW; x++)   // the dark rock behind the pillars: a faint earthen speckle instead of plain black
        if (screen_.px[y * kW + x] == 0 && ((((x + cam) * 7 + y * 13) % 11) == 0 || ((x + cam + y) % 29) == 0)) screen_.px[y * kW + x] = 8;
    unsigned seed = 12345;
    auto rn = [&]() { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 16) & 0xff); };
    for (int wx = lair_x0_ - 40; wx < lair_x1_ + 40; wx += 18) {   // stalactites from the roof
        int len = 6 + rn() % 14, sx = wx + rn() % 8 - cam;
        if (sx < -10 || sx > kW + 10) continue;
        for (int y = 0; y < len; y++) { int w = std::max(1, 6 - y * 6 / len); screen_.fill_rect(sx - w / 2, 8 + y, w, 1, y % 3 == 0 ? 12 : 8); }
    }
    for (int wx = lair_x0_; wx < lair_x1_; wx += 29) {   // boulders at the foot of the wall
        int bw = 8 + rn() % 10, bh = 4 + rn() % 5, sx = wx + rn() % 12 - cam;
        if (sx < -20 || sx > kW + 4) continue;
        for (int yy = 0; yy < bh; yy++) { int w = bw - (bh - yy) * 1; screen_.fill_rect(sx + (bw - w) / 2, 112 - bh + yy, std::max(2, w), 1, yy == 0 ? 7 : 8); }
    }
    {   // between the two rooms: a rock wall with a low archway through it
        int mid = lair_mid() - cam;
        if (mid > -50 && mid < kW + 50) {
            for (int side = 0; side < 2; side++) {
                int x0 = mid + (side ? 14 : -50), w = 36;
                screen_.fill_rect(std::max(0, x0), 0, std::max(0, std::min(kW, x0 + w) - std::max(0, x0)), 112, 0);
                for (int y = 0; y < 112; y += 5) for (int x = x0; x < x0 + w; x += 7) if (x >= 0 && x < kW) screen_.fill_rect(x + (y / 5) % 3, y, 4, 2, ((x + y) / 5) % 2 ? 8 : 12);
            }
            for (int x = mid - 14; x < mid + 14; x++) {   // the lintel over the archway, and its dark inside
                if (x < 0 || x >= kW) continue;
                int top = 52 - (int)(std::sqrt(std::max(0.0, 196.0 - (x - mid) * (x - mid))) * 0.5);
                screen_.fill_rect(x, 0, 1, std::max(0, top), 0);
                for (int y = std::max(0, top); y < std::max(0, top) + 3; y++) screen_.px[y * kW + x] = 8;
            }
        }
    }
    {   // the archway between the rooms is marked: gold trim round it and a torch on each side
        int mid = lair_mid() - cam;
        if (mid > -50 && mid < kW + 50) {
            for (int x = mid - 16; x < mid + 16; x++) {
                if (x < 0 || x >= kW) continue;
                int top = 52 - (int)(std::sqrt(std::max(0.0, 196.0 - (x - mid) * (x - mid))) * 0.5);
                if (x > mid - 15 && x < mid + 15) { int yy = std::max(0, top) + 3; if (yy < 128) screen_.px[yy * kW + x] = 15; }
            }
            for (int y = 56; y < 112; y++) { if (mid - 15 >= 0) screen_.px[y * kW + mid - 15] = 15; if (mid + 14 < kW) screen_.px[y * kW + mid + 14] = 15; }   // the jambs
            for (int side = 0; side < 2; side++) {   // torches
                int tx = mid + (side ? 22 : -24);
                if (tx < 1 || tx > kW - 5) continue;
                screen_.fill_rect(tx, 66, 2, 8, 12);
                int fl = (render_frames_ / 4 + side) % 3;
                screen_.fill_rect(tx - 1, 60 - fl, 4, 6 + fl, 5); screen_.fill_rect(tx, 59 - fl, 2, 6 + fl, 6); screen_.fill_rect(tx, 62, 2, 3, 15);
            }
        }
    }
    {   // the way out: a bright doorway in the left wall of the first room (walk into it when the foes are no longer about)
        int ex = lair_x0_ + 6 - cam;
        if (ex > -30 && ex < kW + 30) {
            for (int y = 40; y < 112; y++) for (int x = 0; x < 22; x++) {
                int X = ex + x; if (X < 0 || X >= kW) continue;
                double dx = (x - 11) / 11.0, top = 60 - std::sqrt(std::max(0.0, 1 - dx * dx)) * 20;
                if (y >= top) screen_.px[y * kW + X] = (y < 80) ? 1 : 14;   // daylight above, grass below
            }
            for (int x = 0; x < 22; x++) { int X = ex + x; if (X >= 0 && X < kW) { screen_.px[111 * kW + X] = 3; } }
            screen_.text(std::max(0, ex - 2), 28, "EXIT", 15, true);
        }
    }
    for (int side = 0; side < 2; side++) {   // the ends of the room are solid rock
        int edge = (side ? lair_x1_ : lair_x0_) - cam;
        int x0 = side ? edge - 10 : edge - 40, x1 = side ? edge + 40 : edge + 10;
        if (x1 < 0 || x0 > kW) continue;
        screen_.fill_rect(std::max(0, x0), 0, std::min(kW, x1) - std::max(0, x0), 128, 0);
        for (int y = 0; y < 128; y += 6) screen_.fill_rect(std::clamp(side ? edge - 10 : edge + 6, 0, kW - 1), y, 4, 3, 8);
    }
    screen_.fill_rect(0, 112, kW, 16, 0);   // the floor line and the ground below it
    for (int x = 0; x < kW; x++) { screen_.px[112 * kW + x] = 12; screen_.px[113 * kW + x] = (x + cam) % 3 ? 12 : 8; screen_.px[114 * kW + x] = (x + cam) % 2 ? 8 : 0; }   // a strip of packed earth
}

void Game::draw_combat() {
    screen_.reset_clip();
    screen_.clear(0);
    const int vh = 128;
    int cx = std::clamp(cx_ + 8 - kW / 2, 0, std::max(0, strip_.width_px() - kW));
    int cy = 256;  // the view starts at the top of row 2; the party stands on the ground (see ground_y)
    if (lair_active_) { cx = std::clamp(cx_ + 8 - kW / 2, lair_x0_, lair_x1_ - kW); cy = lair_ground_ - 112; }
    screen_.set_clip(0, 0, kW, vh);
    if (lair_active_) { draw_lair_background(cx); const Image& ci = lair_chest_taken_ ? chest_open_ : chest_closed_; if (!ci.empty()) screen_.blit(ci, lair_chest_x_ - cx - 8, 112 - ci.h, true); }
    else for (int ty = cy >> 4; ty <= (cy + vh - 1) >> 4; ty++)
        for (int tx = cx >> 4; tx <= (cx + kW - 1) >> 4; tx++) {
            int t = strip_.tile_at(world_, tx * 16, ty * 16);
            if (t >= 1 && t <= (int)wtiles_.size()) screen_.blit(wtiles_[t - 1], tx * 16 - cx, ty * 16 - cy);  // tile numbers are 1-based
        }
    // draw back to front so nearer figures overlap
    struct Item { int y; const Image* im; int x; bool flip; int dl = 16; };
    std::vector<Item> items;
    const Hero& h = party_[leader_];
    size_t f = hero_frame(h);
    items.push_back({cy_, &h.actor.frames[f], cx_ - cx - 8, face_left_});
    items.back().x = cx_ - cx - 8;
    std::vector<Image> corpse_imgs; corpse_imgs.reserve(corpses_.size());   // (kept alive until the items are drawn)
    for (auto& c : corpses_) {
        int dl; corpse_imgs.push_back(corpse_image(c, dl));
        if (dl >= 16) items.push_back({c.e.y, &corpse_imgs.back(), c.e.x - cx - 8 - (corpse_imgs.back().w - 32) / 2, c.e.left});
        else if (dl > 0) { const MonsterType& t = mtypes_[c.e.type]; const Image& im = corpse_imgs.back(); screen_.blit_dither(im, c.e.x - cx - 8 - (im.w - 32) / 2, c.e.y - cy + 16 - t.actor.height, c.e.left, dl); }
    }
    for (auto& e : enemies_) {
        const MonsterType& t = mtypes_[e.type];
        const auto& seq = e.striking ? t.attack : t.walk;
        size_t fr = seq.empty() ? 0 : (size_t)seq[(e.striking ? e.anim / 4 : e.anim) % seq.size()];
        if (fr >= t.actor.frames.size()) fr = 0;
        items.push_back({e.y, &t.actor.frames[fr], e.x - cx - 8 - (t.actor.frames[fr].w - 32) / 2, e.left, e.appear > 0 ? 16 - e.appear * 16 / 9 : 16});  // the sheets face right; flip to face left
    }
    const Image* talker_img = nullptr; int talker_x = 0;
    if (talk_.active && talk_.appear > 0) {  // the person waiting ahead, dithering in
        const Hero* th = talker_hero();
        if (th && !th->actor.frames.empty()) { talker_img = &th->actor.frames[0]; talker_x = talk_.x - cx - 8; }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });
    if (talker_img) screen_.blit_dither(*talker_img, talker_x, cy_ - cy + 16 - talker_img->h, true, std::min(16, talk_.appear * 2 + (talk_.talked ? 16 : 0)));
    for (auto& it : items) {
        int yy = 0;
        bool is_hero = it.im == &h.actor.frames[0] || (it.im >= h.actor.frames.data() && it.im < h.actor.frames.data() + h.actor.frames.size());
        int height = it.im->h;
        yy = (is_hero ? cy_ - jump_h_ : it.y) - cy + 16 - height;
        if (it.dl < 16) screen_.blit_dither(*it.im, it.x, yy, it.flip, it.dl); else screen_.blit(*it.im, it.x, yy, true, it.flip);
    }
    draw_ground_things(cx, false, cy);
    draw_missiles(cx, false, cy);
    draw_floaters(cx, false, cy);
    draw_spell_timers();
    char b[48];
    snprintf(b, sizeof b, "XP %d", xp_);
    screen_.text(4, 4, b, 15);
    if (message_steps_ > 0) screen_.text(4, 14, message_.c_str(), 7);
    draw_panel();
}

std::string Game::debug() const {
    char b[420];
    const char* mode = mode_ == Mode::Title ? "title" : mode_ == Mode::World ? "world" : mode_ == Mode::Combat ? "combat" : mode_ == Mode::Menu ? "menu" : mode_ == Mode::Ending ? "ending" : mode_ == Mode::Help ? "help" : mode_ == Mode::Debug ? "debug" : "gameover";
    if (mode_ == Mode::Combat)
        snprintf(b, sizeof b, "mode=combat strip=(%d,%d) world=(%d,%d) axis=%s leader=%s hp=%d enemies=%d xp=%d steps=%ld", cx_, cy_, px_, py_,
                 east_west_ ? "ew" : "ns", party_[leader_].name.c_str(), party_[leader_].hp, (int)enemies_.size(), xp_, steps_);
    else
        snprintf(b, sizeof b, "mode=%s pos=(%d,%d) leader=%s steps=%ld xp=%d level=%d in=(%d,%d) wild=%d inv=%d party=%d", mode, px_, py_, party_[leader_].name.c_str(), steps_, xp_, level_, ix_, iy_, (int)wild_.size(), (int)inv_.size(), (int)party_.size());
    if (mode_ == Mode::Menu) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " menu=%d/%d/%d mus=%d sfx=%d mh=%d hand=%s", menu_item_, menu_page_, menu_scroll_, (int)music_on_, (int)sfx_on_, menu_hero_, party_[std::clamp(menu_hero_, 0, (int)party_.size() - 1)].hand_name.c_str()); }
    if (interior() && mode_ != Mode::Title && ix_ >= 0 && iy_ >= 0 && (ix_ >> 7) < levels_[level_].w && (iy_ >> 7) < levels_[level_].h) { size_t n = strlen(b); { const Level& LL = levels_[level_]; int cx = ix_ >> 7, cy = iy_ >> 7; auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < LL.w && y < LL.h ? LL.cells[y * LL.w + x] : -1; }; snprintf(b + n, sizeof b - n, " cell=%d n=%d s=%d e=%d w=%d", at(cx, cy), at(cx, cy - 1), at(cx, cy + 1), at(cx + 1, cy), at(cx - 1, cy)); } }
    if (message_steps_ > 0 && !message_.empty()) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " msg=[%.60s]", message_.c_str()); }
    if (mode_ == Mode::Combat) { int th = 0, dd = 99999; for (auto& g : items_) if (g.thrown && !g.taken) { th++; dd = std::min(dd, abs(strip_of_world(g.x / 8, g.y / 8) - cx_)); } size_t n = strlen(b); snprintf(b + n, sizeof b - n, " ammo=%d staff=%d used=%d%d flying=%d dropped=%d dd=%d", party_[leader_].ammo, staff_, (int)used_[7], (int)used_[0], (int)missiles_.size(), th, dd); }
    if (lair_active_) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " lair=%d..%d chest=%d%s", lair_x0_, lair_x1_, lair_chest_x_, lair_chest_taken_ ? "(taken)" : ""); }
    if (getenv("DOF_HANDS")) { for (auto& h : party_) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " [%.4s:%s:%d]", h.name.c_str(), h.hand_name.c_str(), h.armor); } }   // test hook: what everyone wields
    { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " music=%s", music_track_.empty() ? "-" : music_track_.c_str()); }
    if (!box_.empty()) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " box=%d", box_scroll_); }
    if (mode_ == Mode::Title) { size_t n = strlen(b); snprintf(b + n, sizeof b - n, " page=%d sel=%d", title_page_, title_sel_); }
    return b;
}

}  // namespace dof
