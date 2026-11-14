// When a hero falls, the others wait for the fight to end and then react by their alignment (data/farewells.json): the good are sorry, the neutral are sober,
// the evil are irreverent; clerics and druids say their own words, good clerics pray a while and promise to carry the body home.
#include <cmath>

#include "../third_party/json/json.hpp"

#include "game.h"

namespace dof {

static std::string first_word(const std::string& n) { return n.substr(0, n.find(' ')); }
static void subst(std::string& s, const std::string& key, const std::string& val) { for (size_t p; (p = s.find(key)) != std::string::npos;) s.replace(p, key.size(), val); }

bool Game::any_foes_left() const {
    for (auto& e : enemies_) if (e.hp > 0 && e.refugee != 1) return true;
    for (auto& e : ienemies_) if (e.hp > 0) return true;
    return false;
}

std::string Game::nearest_town() const {
    std::string best = "Solace"; double bd = 1e18;
    for (auto& p : pois_) {
        if (!(p.town || p.icon == "town" || p.icon == "village")) continue;
        double d = std::hypot(p.x - px_, p.y - py_);
        if (d < bd) { bd = d; best = p.name; }
    }
    return best;
}

void Game::farewell_step() {
    if (!farewell_loaded_) {
        farewell_loaded_ = true;
        try {
            auto j = nlohmann::json::parse(assets_.read("data/farewells.json"));
            for (auto& [k, v] : j.items()) {
                if (k == "profiles") { for (auto& [n, a] : v.items()) profile_[n] = {a[0].get<std::string>(), a[1].get<std::string>()}; }
                else if (v.is_array()) for (auto& s : v) farewell_[k].push_back(s.get<std::string>());
            }
        } catch (...) {}
    }
    if (speech_steps_ > 0) speech_steps_--;
    if (speech_steps_ <= 0 && !speech_q_.empty() && (mode_ == Mode::World || mode_ == Mode::Combat) && box_.empty()) {
        speech_cur_ = speech_q_.front(); speech_q_.pop_front(); speech_steps_ = speech_cur_.steps;
    }
    // new deaths (a hero who is up again may be mourned again)
    for (auto& h : party_) {
        if (h.hp > 0) mourned_.erase(h.name);
        else if (!mourned_.count(h.name) && farewell_ready_.count(h.name) == 0) farewell_ready_.insert(h.name);
    }
    if (farewell_ready_.empty() || any_foes_left() || !box_.empty() || (mode_ != Mode::World && mode_ != Mode::Combat)) return;
    int alive = 0; for (auto& h : party_) if (h.hp > 0) alive++;
    for (auto& n : farewell_ready_) mourned_.insert(n);
    std::set<std::string> now; now.swap(farewell_ready_);
    if (alive == 0 || farewell_.empty()) return;
    for (auto& n : now) queue_farewell(first_word(n));
}

void Game::queue_farewell(const std::string& dead) {
    auto prof = [&](const Hero& h) { auto it = profile_.find(first_word(h.name)); return it == profile_.end() ? std::make_pair(std::string("NG"), std::string()) : it->second; };
    auto kind = [](const std::string& a) { return a[0] == 'L' && a[1] == 'E' ? 2 : a.back() == 'E' ? 2 : a == "N" || a[1] == 'N' || a.back() == 'N' ? 1 : 0; };   // 0 good, 1 neutral, 2 evil
    int dk = 0;
    for (auto& h : party_) if (first_word(h.name) == dead) dk = kind(prof(h).first);
    auto pick = [&](const char* key) -> std::string { auto& v = farewell_[key]; if (v.empty()) return ""; std::string s = v[rnd(0, (int)v.size() - 1)]; subst(s, "{dead}", dead); subst(s, "{town}", nearest_town()); return s; };
    auto say = [&](const std::string& who, const std::string& text, bool action = false, int steps = 0) {
        if (text.empty()) return;
        Speech s; s.who = who; s.text = text; s.action = action; s.steps = steps ? steps : 22 + (int)text.size() / 3; speech_q_.push_back(s);
    };
    auto act = [&](const char* key, const std::string& who, int steps) { std::string t = pick(key); subst(t, "{who}", who); say(who, t, true, steps); };
    std::vector<const Hero*> plain, clerics; const Hero* good_cleric = nullptr; const Hero* good_one = nullptr;
    for (auto& h : party_) {
        if (h.hp <= 0 || first_word(h.name) == dead) continue;
        auto p = prof(h);
        if (!p.second.empty()) { clerics.push_back(&h); if (p.second == "cleric" && kind(p.first) == 0 && !good_cleric) good_cleric = &h; }
        else plain.push_back(&h);
        if (kind(p.first) == 0 && !good_one) good_one = &h;
    }
    // up to two ordinary members say something, by their own nature
    for (int n = 0; n < 2 && !plain.empty(); n++) {
        int i = rnd(0, (int)plain.size() - 1); const Hero* h = plain[i]; plain.erase(plain.begin() + i);
        int k = kind(prof(*h).first);
        std::string t = k == 0 ? pick(dk == 2 ? "good_vs_evil" : "good") : k == 1 ? pick(dk == 2 ? "good_vs_evil" : "neutral") : pick(dk == 0 ? "evil_vs_good" : dk == 1 ? "evil_vs_neutral" : "evil_vs_evil");
        say(first_word(h->name), t);
    }
    // the clerics and druids
    for (const Hero* h : clerics) {
        auto p = prof(*h); int k = kind(p.first); std::string w = first_word(h->name);
        if (p.second == "druid") { say(w, pick("druid")); act("prayer", w, 18); }
        else if (k == 0) { say(w, pick(dk == 0 ? "cleric_good_for_good" : dk == 1 ? "cleric_good_for_neutral" : "cleric_good_for_evil")); }
        else if (k == 1) { say(w, pick("cleric_neutral")); act("prayer", w, 18); }
        else if (p.first == "LE") say(w, pick("cleric_evil_lawful"));   // no prayer: a few words and away
        else say(w, pick("cleric_evil_other"));
    }
    if (good_cleric) {   // a good cleric prays for about five seconds, then they promise to carry the body home
        std::string w = first_word(good_cleric->name);
        act("pray_long", w, 32);
        say(w, pick("carry"));
        if (good_one && good_one != good_cleric) say(first_word(good_one->name), pick("return"));
        else say(w, pick("return"));
    } else if (good_one) say(first_word(good_one->name), pick("return"));
}

void Game::draw_speech() {
    if (speech_steps_ <= 0) return;
    std::vector<std::string> lines; std::string cur;
    std::string full = speech_cur_.action ? speech_cur_.text : speech_cur_.who + ": " + speech_cur_.text;
    for (size_t i = 0; i < full.size();) {
        size_t j = full.find(' ', i); std::string w = full.substr(i, j == std::string::npos ? j : j - i);
        if (!cur.empty() && cur.size() + 1 + w.size() > 38) { lines.push_back(cur); cur.clear(); }
        cur += (cur.empty() ? "" : " ") + w;
        if (j == std::string::npos) break; i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    int h = (int)lines.size() * 9 + 5;
    screen_.fill_rect(2, 2, 316, h, 0); screen_.fill_rect(2, 2, 316, 1, 7); screen_.fill_rect(2, 1 + h, 316, 1, 7);
    for (size_t i = 0; i < lines.size(); i++) screen_.text(6, 5 + (int)i * 9, lines[i].c_str(), speech_cur_.action ? 11 : 15);
}

}  // namespace dof
