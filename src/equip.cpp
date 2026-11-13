// The Equip and Inventory commands of the main menu (updated controls).
//   Equip: the hero's character screen (abilities, hit points and what he carries: hand weapon, ranged weapon, ammunition, ring). Up / Down choose a slot, A chooses, LB / RB change the hero.
//          Choosing a slot lists what in the shared pack can go there ("There is no equipment here." if nothing can); choosing an item equips it and the old one goes back to the pack. B goes back a step.
//   Inventory: everything in the shared pack and everything the heroes carry, sorted by kind (melee weapons, ranged weapons, ammunition, protection, potions, scrolls, other),
//          an E and the hero's small face on what is equipped.
// While these pages are open the party's portraits are greyed except the chosen hero's, which has a pulsing border.
#include <algorithm>
#include <cstring>

#include "game.h"

namespace dof {
namespace {
const char* const kSlotNames[4] = {"Hand", "Ranged", "Ammunition", "Protection"};
const char* const kCategoryNames[7] = {"Melee weapons", "Ranged weapons", "Ammunition", "Protection", "Potions", "Scrolls and spells", "Other"};

void panel(Screen& s, const char* title) {
    s.reset_clip();
    s.fill_rect(0, 0, kW, 128, 8);
    s.fill_rect(10, 4, 300, 122, 0);
    s.fill_rect(10, 4, 300, 12, 7);
    s.text(160 - (int)strlen(title) * 4, 6, title, 0, false);
}
}  // namespace

int Game::item_id_by_name(const std::string& name) const {
    for (size_t i = 0; i < item_names_.size(); i++) if (item_names_[i] == name) return (int)i + 1;
    return 0;
}

static std::string ammo_label(const Hero& h) {
    const std::string& r = h.range_name;
    if (r.find("Bow") != std::string::npos) return h.ammo_bonus >= 2 ? "Arrows +2" : "Arrows";
    if (r.find("Sling") != std::string::npos) return "Bullets +1";
    if (r.find("Spear") != std::string::npos) return "Spears";
    if (r.find("Axe") != std::string::npos) return "Throwing axes";
    return "Ammunition";
}

std::string Game::equip_slot_text(const Hero& h, int slot) const {
    switch (slot) {
        case 0: return h.hand_name.empty() ? "empty" : h.hand_name;
        case 1: return h.range_name.empty() ? "none" : h.range_name;
        case 2: return h.range_name.empty() ? "-" : ammo_label(h) + " x" + std::to_string(h.ammo);
        default: {
            std::string s;
            auto add = [&](const char* t) { if (!s.empty()) s += ", "; s += t; };
            if (h.plate) add("Plate armor");
            if (h.shield) add("Shield +1");
            if (h.ring) add("Ring");
            return s.empty() ? std::string("none") : s;
        }
    }
}

// Indices into the pack of the items that can go into this slot of this hero.
std::vector<int> Game::equip_candidates(const Hero& h, int slot) const {
    static const char* const want[4] = {"hand", "range", "ammo", "ring"};   // (slot 3 also takes plate armor: slot "armor")
    std::vector<int> out;
    for (size_t i = 0; i < inv_.size(); i++) {
        auto it = istats_.find(inv_[i]);
        if (it == istats_.end() || !(it->second.slot == want[slot] || (slot == 3 && (it->second.slot == "armor" || it->second.slot == "shield")))) continue;
        if (slot < 3 && !item_usable_by(it->second, h)) continue;
        if (slot == 2 && h.range_name.empty()) continue;
        if (slot == 3 && ((it->second.slot == "ring" && h.ring) || (it->second.slot == "armor" && (h.plate || !item_usable_by(it->second, h))) || (it->second.slot == "shield" && (h.shield || !item_usable_by(it->second, h))))) continue;
        out.push_back((int)i);
    }
    return out;
}

bool Game::equip_from_pack(Hero& h, int slot, int inv_index, std::string& msg) {
    int id = inv_[inv_index];
    auto it = istats_.find(id);
    if (it == istats_.end()) return false;
    const ItemStat& st = it->second;
    std::string first = h.name.substr(0, h.name.find(' ')), nm = item_name(id);
    int displaced = 0;
    if (slot == 0) { displaced = item_id_by_name(h.hand_name); h.hand_name = nm; h.hand_dice = st.dice; h.hand_sides = st.sides; h.hand_bonus = st.bonus; }
    else if (slot == 1) { displaced = item_id_by_name(h.range_name); h.range_name = nm; h.range_dice = st.dice; h.range_sides = st.sides; h.range_bonus = st.bonus; }
    else if (slot == 2) { h.ammo += st.shots; h.ammo_bonus = std::max(h.ammo_bonus, st.bonus); }
    else if (st.slot == "shield") { h.shield = true; }
    else if (st.slot == "armor") { h.plate = true; h.armor = std::max(0, std::min(h.armor + h.ring_bonus, st.bonus) - h.ring_bonus); }
    else { h.ring = true; h.ring_bonus = st.bonus; h.armor = std::max(0, h.armor - st.bonus); }
    inv_.erase(inv_.begin() + inv_index);
    if (displaced && displaced != 2 && istats_.count(displaced) && inv_.size() < kPackMax) inv_.push_back(displaced);   // what he puts down goes back in the pack (his own staff stays his)
    msg = first + (slot == 2 ? " took the " : slot == 3 ? " put on the " : " equipped the ") + nm + ".";
    return true;
}

void Game::step_equip(const Input& in, bool edge, bool up, bool down, bool cancel) {
    int n = (int)party_.size();
    if (in.cycle && n > 0) menu_hero_ = ((menu_hero_ + in.cycle) % n + n) % n;   // LB / RB: another hero
    menu_hero_ = std::clamp(menu_hero_, 0, std::max(0, n - 1));
    Hero& h = party_[menu_hero_];
    if (menu_page_ == kPageEquip) {
        if (up) equip_slot_ = (equip_slot_ + 3) % 4;
        if (down) equip_slot_ = (equip_slot_ + 1) % 4;
        if (cancel) { menu_page_ = -1; return; }
        if (edge) {
            if (equip_candidates(h, equip_slot_).empty()) box_ = "There is no equipment here.";
            else { menu_page_ = kPageEquipPick; menu_scroll_ = 0; }
        }
    } else if (menu_page_ == kPageEquipPick) {
        auto c = equip_candidates(h, equip_slot_);
        if (c.empty() || cancel) { menu_page_ = kPageEquip; return; }
        int m = (int)c.size();
        menu_scroll_ = std::clamp(menu_scroll_, 0, m - 1);
        if (up) menu_scroll_ = (menu_scroll_ + m - 1) % m;
        if (down) menu_scroll_ = (menu_scroll_ + 1) % m;
        if (edge) {
            std::string msg;
            if (equip_from_pack(h, equip_slot_, c[menu_scroll_], msg)) box_ = msg;
            menu_page_ = kPageEquip;   // back to the character screen to see the result
        }
    } else {   // inventory
        if (cancel) { menu_page_ = -1; return; }
        if (up) menu_scroll_ = std::max(0, menu_scroll_ - 1);
        if (down) menu_scroll_++;
    }
}

void Game::draw_equip() {
    const Hero& h = party_[std::clamp(menu_hero_, 0, (int)party_.size() - 1)];
    auto ab = [&](int i) { return i < (int)h.abilities.size() ? h.abilities[i] : 0; };
    char b[96];
    std::string title = (menu_page_ == kPageEquipPick ? "EQUIP WITH - " : "EQUIP - ") + h.name.substr(0, h.name.find(' '));
    panel(screen_, title.c_str());
    snprintf(b, sizeof b, "Str%d Int%d Wis%d Dex%d Con%d Cha%d", ab(0), ab(1), ab(2), ab(3), ab(4), ab(5));
    screen_.text(16, 20, b, 3, false);
    snprintf(b, sizeof b, "AC %d   HP %d / %d   Experience %d", eff_armor(h), h.hp, h.hp_max, h.xp);
    screen_.text(16, 30, b, 3, false);
    if (menu_page_ == kPageEquip) {
        for (int s = 0; s < 4; s++) {
            int y = 46 + s * 14;
            bool hot = s == equip_slot_;
            if (hot) screen_.fill_rect(14, y - 3, 292, 13, 5);
            snprintf(b, sizeof b, "%-11s %s", kSlotNames[s], equip_slot_text(h, s).c_str());
            screen_.text(20, y, b, hot ? 15 : 7, false);
            if (hot && !equip_candidates(h, s).empty()) screen_.text(284, y, ">", 14, false);
        }
        screen_.text(16, 108, "A: choose   LB/RB: hero   B: back", 8, false);
    } else if (menu_page_ == kPageEquipPick) {
        screen_.text(16, 42, kSlotNames[equip_slot_], 14, false);
        auto c = equip_candidates(h, equip_slot_);
        int first = std::clamp(menu_scroll_ - 4, 0, std::max(0, (int)c.size() - 5));
        for (int i = 0; i < 5 && first + i < (int)c.size(); i++) {
            int y = 54 + i * 10, k = first + i;
            bool hot = k == menu_scroll_;
            if (hot) screen_.fill_rect(14, y - 2, 292, 11, 5);
            screen_.text(24, y, item_name(inv_[c[k]]).c_str(), hot ? 15 : 7, false);
        }
        screen_.text(16, 108, "A: equip   B: back", 8, false);
    }
}

// Inventory: the pack and everything carried, sorted by kind.
void Game::draw_inventory() {
    panel(screen_, "INVENTORY");
    struct Row { int cat; std::string text; int hero; };   // hero = -1: in the pack
    std::vector<Row> rows;
    auto category = [&](int id) {
        auto it = istats_.find(id);
        if (it != istats_.end()) { const std::string& s = it->second.slot; return s == "hand" ? 0 : s == "range" ? 1 : s == "ammo" ? 2 : 3; }
        if (id == 39 || id == 51) return 4;
        const std::string n = item_name(id);
        if (!n.empty() && n[0] == '%') return 5;
        return 6;
    };
    for (int id : inv_) rows.push_back({category(id), item_name(id), -1});
    for (size_t i = 0; i < party_.size(); i++) {
        const Hero& h = party_[i];
        if (!h.hand_name.empty()) rows.push_back({0, h.hand_name, (int)i});
        if (!h.range_name.empty()) rows.push_back({1, h.range_name, (int)i});
        if (!h.range_name.empty() && h.ammo > 0) rows.push_back({2, ammo_label(h) + " x" + std::to_string(h.ammo), (int)i});
        if (h.ring) rows.push_back({3, "Ring of Protection", (int)i});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.cat != b.cat ? a.cat < b.cat : a.text != b.text ? a.text < b.text : a.hero < b.hero; });
    // lines to show: a heading per kind, then its rows
    struct Line { bool heading; std::string text; int hero; };
    std::vector<Line> lines;
    int last = -1;
    for (auto& r : rows) {
        if (r.cat != last) { lines.push_back({true, kCategoryNames[r.cat], -1}); last = r.cat; }
        lines.push_back({false, r.text, r.hero});
    }
    const int visible = 8;
    int maxs = std::max(0, (int)lines.size() - visible);
    menu_scroll_ = std::clamp(menu_scroll_, 0, maxs);
    for (int i = 0; i < visible && menu_scroll_ + i < (int)lines.size(); i++) {
        const Line& l = lines[menu_scroll_ + i];
        int y = 20 + i * 12;
        if (l.heading) { screen_.text(16, y, l.text.c_str(), 15, false); continue; }
        if (l.hero >= 0) {   // equipped: an E and the hero's face (the portrait at half size)
            screen_.text(16, y + 2, "E", 13, false);
            int cl = party_[l.hero].clip;
            if (cl >= 0 && cl < (int)clips_.size()) {   // the hero's face at a third of its size
                const Image& im = clips_[cl];
                for (int yy = 0; yy < 11; yy++) for (int xx = 0; xx < 11; xx++)
                    screen_.px[(y - 1 + yy) * kW + 28 + xx] = im.px[(size_t)(yy * im.h / 11) * im.w + xx * im.w / 11];
            }
        }
        screen_.text(44, y + 2, l.text.c_str(), l.hero >= 0 ? 7 : 3, false);
    }
    if (lines.empty()) screen_.text(16, 40, "Nothing carried.", 7, false);
    bool blink = (render_frames_ / 20) % 2 == 0;
    auto arrow = [&](int x, int y, bool dn) { for (int r = 0; r < 4; r++) { int w = dn ? 7 - 2 * r : 1 + 2 * r; screen_.fill_rect(x + (7 - w) / 2, y + r, w, 1, 15); } };
    if (menu_scroll_ < maxs && blink) arrow(296, 116, true);
    if (menu_scroll_ > 0) arrow(296, 18, false);
    screen_.text(16, 118, "E=equipped  Up/Down: scroll  B: back", 8, false);
}

}  // namespace dof
