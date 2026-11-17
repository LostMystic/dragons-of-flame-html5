// Camp: the Rest command (with the updated controls). The view fades to black, a few of the companions talk (data/camp_banter.json), the view fades back in,
// and then the rest itself happens (the hurt heal a point, the spells come back). The talk is chosen from those who are alive and from where the party is
// (tags: any, wild, dungeon, sla, pax, hurt); nothing is repeated until the choices run out.
#include <algorithm>
#include <cmath>

#include "../third_party/json/json.hpp"

#include "game.h"

namespace dof {

static const int kFadeFrames = 54, kAutoBase = 260;   // a fade lasts a bit under a second; a line moves on by itself after about four and a half seconds plus its reading time
static std::string first_word(const std::string& n) { return n.substr(0, n.find(' ')); }

void Game::finish_rest() {   // "heals all damaged characters one HP" (manual); regains the memorized spells
    for (auto& h : party_) if (h.hp > 0 && h.hp < h.hp_max) h.hp++;
    for (bool& u : used_) u = false;
    show_box(23);
}

bool Game::start_camp() {
    if (!camp_loaded_) {
        camp_loaded_ = true;
        try {
            auto raw = assets_.read("data/camp_banter.json");
            auto j = nlohmann::json::parse(std::string(raw.begin(), raw.end()));
            for (auto& t : j["talks"]) {
                CampTalk c;
                for (auto& g : t["tags"]) c.tags.push_back(g.get<std::string>());
                for (auto& l : t["lines"]) c.lines.push_back({l[0].get<std::string>(), l[1].get<std::string>()});
                if (!c.lines.empty()) camp_talks_.push_back(c);
            }
        } catch (...) {}
    }
    std::set<std::string> alive; bool hurt = false;
    for (auto& h : party_) if (h.hp > 0) { alive.insert(first_word(h.name)); if (h.hp * 2 < h.hp_max) hurt = true; }
    std::set<std::string> tags = {"any"};
    if (!interior()) tags.insert("wild"); else { tags.insert("dungeon"); tags.insert(level_ == 2 ? "sla" : "pax"); }
    if (hurt) tags.insert("hurt");
    std::vector<int> pool, fresh;
    for (size_t i = 0; i < camp_talks_.size(); i++) {
        const CampTalk& t = camp_talks_[i];
        bool ok = true; for (auto& l : t.lines) if (!alive.count(l.first)) ok = false;
        bool tagged = false; for (auto& g : t.tags) if (tags.count(g)) tagged = true;
        if (!ok || !tagged) continue;
        int w = 1; for (auto& g : t.tags) if (g != "any" && tags.count(g)) w = 3;   // talk that suits the place or the party's state comes up more often
        for (int k = 0; k < w; k++) pool.push_back((int)i);
    }
    for (int i : pool) if (!camp_used_.count(i)) fresh.push_back(i);
    if (fresh.empty()) { for (int i : pool) camp_used_.erase(i); fresh = pool; }
    if (fresh.empty()) return false;
    int pick = fresh[rnd(0, (int)fresh.size() - 1)];
    camp_used_.insert(pick);
    camp_ = Camp(); camp_.active = true; camp_.phase = 0; camp_.since = render_frames_;
    camp_.lines = camp_talks_[pick].lines;
    box_.clear(); attacking_ = false; ranged_pose_ = 0;
    return true;
}

void Game::step_camp(const Input& in) {
    long now = render_frames_;
    bool press = in.confirm || in.any || edges_[5];
    if (camp_.phase == 0) {   // fading out
        if (now - camp_.since >= kFadeFrames) { camp_.phase = 1; camp_.shown = 0; camp_.line_since = now; camp_.since = now; }
        return;
    }
    if (camp_.phase == 1) {   // the talk: one more line each press (or by itself after a while)
        if (camp_.shown == 0) { camp_.shown = 1; camp_.line_since = now; return; }
        bool cancel = in.cancel || edges_[4];
        const std::string& t = camp_.lines[camp_.shown - 1].second;
        long wait = kAutoBase + (long)t.size() * 3;
        bool ready = now - camp_.line_since >= 24;   // a held key or a double tap cannot skip a line
        if (cancel) { camp_.phase = 2; camp_.since = now; return; }
        if ((press && ready) || now - camp_.line_since >= wait) {
            if (camp_.shown < camp_.lines.size()) { camp_.shown++; camp_.line_since = now; }
            else { camp_.phase = 2; camp_.since = now; }
        }
        return;
    }
    if (now - camp_.since >= kFadeFrames) { camp_.active = false; finish_rest(); }   // faded back in
}

// Bayer-dithered fade: coverage 0..1 of black over the whole screen
static void dim_screen(Screen& s, double cover) {
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int lim = (int)std::lround(cover * 16);
    if (lim <= 0) return;
    for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++) if (bayer[(y & 3) * 4 + (x & 3)] < lim) s.px[y * kW + x] = 0;
}

static uint8_t name_color(const std::string& n) {
    static const std::map<std::string, uint8_t> c = {{"Sturm", 1}, {"Caramon", 12}, {"Goldmoon", 15}, {"Raistlin", 11}, {"Tanis", 13}, {"Tasslehoff", 14},
                                                     {"Riverwind", 10}, {"Flint", 6}, {"Gilthanas", 2}, {"Laurana", 9}};
    auto it = c.find(n); return it == c.end() ? 3 : it->second;
}

void Game::draw_camp() {
    long now = render_frames_;
    screen_.reset_clip();
    if (camp_.phase == 0) { dim_screen(screen_, std::min(1.0, (now - camp_.since) / (double)kFadeFrames)); return; }
    if (camp_.phase == 2) { dim_screen(screen_, std::max(0.0, 1.0 - (now - camp_.since) / (double)kFadeFrames)); return; }
    screen_.clear(0);
    // every line shown so far is wrapped into rows of up to 37 characters (a hanging indent of two); the newest is bright, the older ones grey and scroll away at the top
    struct Row { std::string who, t; bool newest; bool blank; };
    std::vector<Row> rows;
    for (size_t i = 0; i < camp_.shown && i < camp_.lines.size(); i++) {
        const std::string& who = camp_.lines[i].first;
        const std::string& said = camp_.lines[i].second;
        bool newest = i + 1 == camp_.shown, first = true;
        std::string cur;
        size_t p = 0;
        int lead = (int)who.size() + 2;   // the name takes the front of the first row
        while (p <= said.size()) {
            size_t e = said.find(' ', p); if (e == std::string::npos) e = said.size();
            std::string w = said.substr(p, e - p);
            int room = 37 - (first ? lead : 2);
            std::string tryl = cur.empty() ? w : cur + " " + w;
            if ((int)tryl.size() > room && !cur.empty()) { rows.push_back({first ? who : "", cur, newest, false}); first = false; cur = w; } else cur = tryl;
            p = e + 1;
        }
        if (!cur.empty()) rows.push_back({first ? who : "", cur, newest, false});
        rows.push_back({"", "", false, true});
    }
    const int maxrows = 21;
    size_t from = rows.size() > (size_t)maxrows ? rows.size() - maxrows : 0;
    int y = 14;
    for (size_t r = from; r < rows.size(); r++, y += 8) {
        const Row& w = rows[r];
        if (w.blank) continue;
        uint8_t body = w.newest ? 3 : 7;
        if (!w.who.empty()) {
            screen_.text(14, y, (w.who + ":").c_str(), w.newest ? name_color(w.who) : 8);
            screen_.text(14 + ((int)w.who.size() + 2) * 8, y, w.t.c_str(), body);
        } else screen_.text(14 + 16, y, w.t.c_str(), body);
    }
    if (camp_.shown > 0 && ((now / 20) % 2 == 0)) {   // a small arrow: press a key for more
        screen_.fill_rect(300, 188, 6, 1, 8); screen_.fill_rect(301, 189, 4, 1, 8); screen_.fill_rect(302, 190, 2, 1, 8);
    }
}

}  // namespace dof
