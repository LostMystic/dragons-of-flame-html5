// When a hero falls, the others wait for the fight to end and then react by their alignment (data/farewells.json): the good are sorry, the neutral are sober,
// the evil are irreverent; clerics and druids say their own words, good clerics pray a while and promise to carry the body home.
#include <algorithm>
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
            auto add = [&](const std::string& key, const nlohmann::json& arr) { for (auto& x : arr) farewell_[key].push_back(x.get<std::string>()); };
            for (const char* top : {"say", "pair", "goldmoon"}) if (j.contains(top)) for (auto& [k, v] : j[top].items()) add(std::string(top) + ":" + k, v);
            for (const char* top : {"prayer", "pray_long", "carry", "return"}) if (j.contains(top)) add(top, j[top]);
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
    auto pick = [&](const std::string& key) -> std::string { auto it = farewell_.find(key); if (it == farewell_.end() || it->second.empty()) return ""; std::string t = it->second[rnd(0, (int)it->second.size() - 1)]; subst(t, "{dead}", dead); subst(t, "{town}", nearest_town()); return t; };
    auto say = [&](const std::string& who, const std::string& text, bool action = false, int steps = 0) {
        if (text.empty()) return;
        Speech sp; sp.who = who; sp.text = text; sp.action = action; sp.steps = steps ? steps : 22 + (int)text.size() / 3; speech_q_.push_back(sp);
    };
    static const std::set<std::string> cast = {"Sturm", "Caramon", "Raistlin", "Tanis", "Goldmoon", "Riverwind", "Flint", "Tasslehoff", "Gilthanas", "Laurana"};
    std::vector<std::string> living;
    for (auto& h : party_) if (h.hp > 0 && first_word(h.name) != dead) living.push_back(first_word(h.name));
    auto alive = [&](const char* n) { return std::find(living.begin(), living.end(), n) != living.end(); };
    // who speaks: first anyone with something personal to say about this death, then others in their own voices (two in all, Goldmoon apart)
    std::vector<std::string> speakers;
    for (auto& w : living) if (w != "Goldmoon" && farewell_.count("pair:" + w + ">" + dead) && speakers.size() < 2) speakers.push_back(w);
    std::vector<std::string> rest;
    for (auto& w : living) if (w != "Goldmoon" && std::find(speakers.begin(), speakers.end(), w) == speakers.end()) rest.push_back(w);
    while (speakers.size() < 2 && !rest.empty()) { int i = rnd(0, (int)rest.size() - 1); speakers.push_back(rest[i]); rest.erase(rest.begin() + i); }
    for (auto& w : speakers) {
        std::string t = pick("pair:" + w + ">" + dead);
        if (t.empty()) t = pick("say:" + (cast.count(w) ? w : std::string("Nomad")));
        say(w, t);
    }
    bool gm = alive("Goldmoon");
    if (gm) {   // the party's cleric: her own words, a prayer, and the promise to carry the body home
        std::string t = pick("pair:Goldmoon>" + dead);
        bool personal = !t.empty();
        if (!personal) t = pick(dead == "Raistlin" ? "goldmoon:evil" : cast.count(dead) ? "goldmoon:good" : "goldmoon:neutral");
        say("Goldmoon", t);
        std::string pr = pick("pray_long"); subst(pr, "{who}", "Goldmoon"); say("Goldmoon", pr, true, 32);   // about five seconds
        say("Goldmoon", pick("carry"));
        std::string back; for (auto& w : living) if (w != "Goldmoon" && w != "Raistlin") { back = w; break; }
        say(back.empty() ? "Goldmoon" : back, pick("return"));
    } else if (!living.empty()) {
        std::string back; for (auto& w : living) if (w != "Raistlin") { back = w; break; }
        say(back.empty() ? living[0] : back, pick("return"));
    }
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
