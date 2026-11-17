// The title screen: a splash with the publishers' lines, then the picture with the logo and the menu
// (Start New Game, Load Game, Intro, Settings, Credits), the intro slideshow (the manual's back story) and the credits roll.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "../third_party/font8x8/font8x8_basic.h"
#include "game.h"

namespace dof {
namespace {

constexpr int kSettingsPage = 17;  // the page number the main menu uses for Settings
const char* const kItems[] = {"Start New Game", "Load Game", "Intro", "High Scores", "Settings", "Credits", "Exit"};
constexpr int kItemN = 7, kItemY = 108, kItemH = 11, kItemX0 = 92, kItemX1 = 228;

// The intro slideshow: the story from the manual's introduction ("The action starts"), then the game's own opening message (filled in at run time).
const char* const kSlides[] = {
    "It is more than three hundred years since the Cataclysm, when the old gods abandoned the people of Krynn.",
    "Without the interference of the old gods, Takhisis, the Queen of Darkness, spreads her power from the Abyss by awakening evil dragons and creating armies of Draconians.",
    "Once her armies are victorious, she will be free to move from the Abyss into Krynn. The only threat to the Queen's awesome power is the revival of belief in the old gods and the uniting of the resistance against the Draconian armies.",
    "The first steps against Takhisis have been taken. The Disks of Mishakal have been recovered and Goldmoon has become the first True Cleric since the Cataclysm.",
    "But, while returning from the successful assault on Xak Tsarolh, Goldmoon and the other Companions of the Lance are captured by an army of Draconians.",
    "When all seems lost, the Companions are freed during an elven attack on the Draconians. Now the Companions must ally with friendly NPCs, battle Draconian hordes, and sneak through the caves of Sla-Mori into the fortress of Pax Tharkas.",
    "Inside, the Companions must recover the sword Wyrmslayer and free the slaves held there to bind the forces of the resistance together.",
    nullptr,  // the opening message of the game (message 36)
};
constexpr int kSlideN = 8;

// Credits as printed in the manual; a leading '#' marks a heading.
const char* const kCredits[] = {
    "#DRAGONS OF FLAME", "", "#Program Creation", "U.S. Gold Ltd.", "", "#Product Manager", "Mike Wilding", "",
    "#Developers", "George MacDonald", "Graeme Bayless", "", "#16 Bit Programming", "Graham Lilley", "", "#16 Bit Graphics", "Kevin Bulmer", "",
    "#8 Bit Programming and Graphics", "Mr. Micro", "", "#Design Coordination", "Laurence H. Miller", "",
    "#Playtesting", "U.S. Gold Ltd. &", "Strategic Simulations, Inc.", "", "#Based on the Module", "DL2 Dragons of Flame", "by Douglas Niles", "",
    "#Original Cover Graphic", "Jeff Easley", "", "#Documentation", "Laurence H. Miller", "George MacDonald", "",
    "#Art and Graphic Design", "Louis Saekow Design:", "Peter Gascoyne & David Boudreau", "", "#Desktop Publishing", "Louis Saekow Design:", "Peter Gascoyne", "",
    "#Pre-press Production", "Louis Saekow Design:", "Kirk Nichols", "", "", "#HTML5 Version", "Created by Mark Barazzuol", "", "",
    "Copyright 1989", "Strategic Simulations, Inc.", "Copyright 1989 TSR, Inc.", "All Rights Reserved.", "", "", "",
};

std::vector<std::string> wrap(const std::string& t, size_t cols) {
    std::vector<std::string> out; std::string line; size_t i = 0;
    while (i < t.size()) {
        size_t e = t.find(' ', i); if (e == std::string::npos) e = t.size();
        std::string w = t.substr(i, e - i);
        if (line.size() + w.size() + 1 > cols && !line.empty()) { out.push_back(line); line.clear(); }
        if (!line.empty()) line += ' ';
        line += w; i = e + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

void flame(Screen& s, int x, int y, uint8_t col) {
    static const char* art[] = {"...X....", "..XX....", "..XXX...", ".XXXXX..", ".XXXXX..", ".XXXXX..", "..XXX..."};
    for (int r = 0; r < 7; r++) for (int c = 0; c < 8; c++) if (art[r][c] == 'X') s.px[(y + r) * kW + x + c] = col;
}

}  // namespace

uint8_t Game::pal_idx(uint8_t r, uint8_t g, uint8_t b) const {
    uint32_t want = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    for (int i = 0; i < 16; i++) if ((screen_.pal[i] & 0xFFFFFF) == want) return (uint8_t)i;
    return 15;
}

// Darken a rectangle by speckling it with black: level 0 = half, 1 = three quarters.
void Game::dim_rect(int x, int y, int w, int h, int level) {
    for (int yy = std::max(0, y); yy < std::min(kH, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(kW, x + w); xx++) {
            bool keep = level == 0 ? ((xx + yy) & 1) == 1 : ((xx & 1) == 1 && (yy & 1) == 1);
            if (!keep) screen_.px[yy * kW + xx] = 0;
        }
}

// Big lettering: the 8x8 font scaled up, filled with a colour per source row (a gradient), an outline round it and a drop shadow.
void Game::fancy_text(int x, int y, const std::string& s, int scale, int spacing, const std::vector<uint8_t>& grad, uint8_t outline, uint8_t shadow) {
    int adv = 8 * scale + spacing;
    auto put = [&](int px, int py, uint8_t c) { if (px >= 0 && px < kW && py >= 0 && py < kH) screen_.px[py * kW + px] = c; };
    for (int pass = 0; pass < 3; pass++)  // 0: shadow, 1: outline, 2: fill
        for (size_t i = 0; i < s.size(); i++) {
            unsigned char ch = (unsigned char)s[i];
            if (ch > 127 || ch == ' ') continue;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    if (!((font8x8_basic[ch][r] >> c) & 1)) continue;
                    int bx = x + (int)i * adv + c * scale, by = y + r * scale;
                    if (pass == 0) { for (int dy = 0; dy < scale; dy++) for (int dx = 0; dx < scale; dx++) put(bx + dx + 2, by + dy + 2, shadow); }
                    else if (pass == 1) { for (int dy = -1; dy < scale + 1; dy++) for (int dx = -1; dx < scale + 1; dx++) put(bx + dx, by + dy, outline); }
                    else { uint8_t col = grad[std::min<size_t>(r, grad.size() - 1)]; for (int dy = 0; dy < scale; dy++) for (int dx = 0; dx < scale; dx++) put(bx + dx, by + dy, col); }
                }
        }
}

void Game::set_title_page(int p) {
    title_page_ = p; title_page_frames_ = 0; slide_ = 0;
    screen_.pal = (p == 2 || p == 4 || p == 5) ? kGamePalette : title_pal_;   // credits, settings and the scores use the game's own colours
    if (p == 5) menu_open_step_ = steps_;   // the scroll unrolls
    if (p == 4) { menu_page_ = kSettingsPage; settings_focus_ = 0; }
}

void Game::go_title() {
    mode_ = Mode::Title; title_sel_ = 0; title_frames_ = 0;
    set_title_page(1);
    play_title();
}

void Game::start_new_game() {
    reset_party(); level_ = 1; quest_flags_ = 0x0f; spawn_wild(); load_items();
    inv_.clear(); dmap_.clear(); freed_.clear(); poi_done_.clear(); elistan_state_ = 0; elistan_talk_ = 0; elistan_near_ = false; choice_ = 0; for (bool& u : used_) u = false;
    disks_ = staff_ = 100; ward_ = bless_ = 0;
    intro_shown_ = false;
    confirm_prev_ = true; pending_confirm_ = false;   // the key that chose this entry must not also open the menu
    enter_world();   // shows the opening message
}

void Game::title_load() {
    if (assets_.read("user/save.json").empty()) { message_ = "No saved game"; message_steps_ = 18; return; }
    load_game();
    if (mode_ == Mode::World) { stop_sound(); screen_.pal = kGamePalette; intro_shown_ = true; }
}

void Game::title_activate(int item) {
    switch (item) {
        case 0: start_new_game(); break;
        case 1: title_load(); break;
        case 2: set_title_page(3); break;
        case 3: set_title_page(5); break;
        case 4: set_title_page(4); break;
        case 5: set_title_page(2); break;
        case 6: request_exit(); break;
    }
}

void Game::step_title(const Input& in) {
    bool confirm = edges_[5], up = edges_[0], down = edges_[1], cancel = edges_[4];
    bool mouse = !orig_controls_ && in.mx >= 0 && in.my >= 0;
    switch (title_page_) {
        case 0:  // the publishers' lines: any key (or a click) goes on to the menu
            if (in.any || in.confirm || in.click || title_page_frames_ > 420) set_title_page(5);   // the high scores come up before the menu
            break;
        case 1: {
            if (up) title_sel_ = (title_sel_ + kItemN - 1) % kItemN;
            if (down) title_sel_ = (title_sel_ + 1) % kItemN;
            int hit = -1;
            if (mouse && in.mx >= kItemX0 && in.mx < kItemX1)
                for (int i = 0; i < kItemN; i++) if (in.my >= kItemY + i * kItemH - 1 && in.my < kItemY + (i + 1) * kItemH - 1) hit = i;
            if (hit >= 0 && (in.mx != last_mx_ || in.my != last_my_)) title_sel_ = hit;
            last_mx_ = in.mx; last_my_ = in.my;
            if (in.click) { if (hit >= 0) { title_sel_ = hit; title_activate(hit); } }
            else if (confirm) title_activate(title_sel_);
            break;
        }
        case 2:  // credits: any key, a click or the end of the roll goes back
            if (cancel || confirm || in.click || in.any) set_title_page(1);
            break;
        case 3: {  // intro slides: confirm completes the typing, then goes to the next slide; Esc leaves
            if (cancel) { set_title_page(1); break; }
            if (confirm || in.click || down) {
                std::string t = slide_ == kSlideN - 1 && texts_.size() > 36 ? texts_[36] : kSlides[slide_] ? kSlides[slide_] : "";
                size_t shown = (size_t)(title_page_frames_ / 2);
                if (shown < t.size()) title_page_frames_ = (int)t.size() * 2;
                else if (slide_ + 1 < kSlideN) { slide_++; title_page_frames_ = 0; }
                else set_title_page(1);
            }
            if (up && slide_ > 0) { slide_--; title_page_frames_ = 0; }
            break;
        }
        case 5:  // high scores: any key goes on to the menu
            if (in.any || confirm || cancel || in.click || title_page_frames_ > 600) set_title_page(1);
            break;
        case 4:  // settings
            step_settings(in);
            if (menu_page_ != kSettingsPage) { menu_page_ = -1; set_title_page(1); }
            break;
    }
}

void Game::draw_title() {
    screen_.reset_clip();
    const uint8_t white = pal_idx(255, 255, 255), yellow = pal_idx(255, 255, 85), lred = pal_idx(255, 85, 85), red = pal_idx(170, 0, 0),
                  brown = pal_idx(170, 85, 0), grey = pal_idx(170, 170, 170), dgrey = pal_idx(85, 85, 85), black = 0;
    auto centre = [&](int y, const std::string& s, uint8_t col) { screen_.text(160 - (int)s.size() * 4, y, s.c_str(), col); };
    switch (title_page_) {
        case 0: {  // publishers' lines, fading in one after the other
            screen_.clear(0);
            struct L { int at, y; const char* s; bool big; };
            static const L lines[] = {{20, 52, "STRATEGIC SIMULATIONS, INC.", true}, {80, 74, "and", false}, {110, 92, "U.S. GOLD LTD.", true}, {170, 120, "present", false},
                                      {230, 150, "DRAGONLANCE is a trademark of TSR, Inc.", false}};
            for (auto& l : lines) {
                int t = title_page_frames_ - l.at;
                if (t < 0) continue;
                uint8_t col = t < 14 ? dgrey : t < 28 ? grey : (l.big ? yellow : white);
                std::string s = l.s;
                if (l.big) { int w = (int)s.size() * 9; fancy_text(160 - w / 2, l.y, s, 1, 1, {col, col, col, col, col, col, col, col}, black, dgrey); }
                else centre(l.y, s, col);
            }
            break;
        }
        case 1: {  // the picture with the logo and the menu
            screen_.blit(title_, 0, 0);
            dim_rect(40, 2, 240, 68, 0);
            fancy_text(160 - 10 * 18 / 2, 6, "DRAGONS OF", 2, 2, {white, yellow, yellow, yellow, lred, lred, red, red}, black, dgrey);
            fancy_text(160 - 5 * 42 / 2, 26, "FLAME", 5, 2, {white, yellow, yellow, yellow, lred, lred, red, brown}, black, dgrey);
            centre(62, "A DRAGONLANCE ADVENTURE", white);
            {   // the new name, small, in a dimmed strip just under the logo box
                const std::string rk = "R E K I N D L E D";
                int w = (int)rk.size() * 8;
                screen_.fill_rect(160 - w / 2 - 6, 72, w + 12, 12, 0);
                fancy_text(160 - w / 2, 74, rk, 1, 0, {white, yellow, yellow, yellow, lred, lred, red, red}, black, dgrey);
            }
            screen_.fill_rect(kItemX0 - 6, kItemY - 6, kItemX1 - kItemX0 + 12, kItemN * kItemH + 10, 0);
            screen_.fill_rect(kItemX0 - 6, kItemY - 6, kItemX1 - kItemX0 + 12, 1, yellow);
            screen_.fill_rect(kItemX0 - 6, kItemY + kItemN * kItemH + 3, kItemX1 - kItemX0 + 12, 1, yellow);
            for (int i = 0; i < kItemN; i++) {
                std::string s = kItems[i];
                int w = (int)s.size() * 8, x = 160 - w / 2, y = kItemY + i * kItemH;
                if (i == title_sel_) {
                    bool b = (title_frames_ / 20) % 2 == 0;
                    fancy_text(x, y, s, 1, 0, {white, yellow, yellow, yellow, yellow, yellow, lred, lred}, black, dgrey);
                    flame(screen_, x - 14 - (b ? 1 : 0), y, yellow); flame(screen_, x + w + 6 + (b ? 1 : 0), y, yellow);
                } else screen_.text(x, y, s.c_str(), white);
            }
            if (message_steps_ > 0) centre(94, message_, lred);
            break;
        }
        case 2: {  // credits roll
            screen_.clear(0);
            int off = title_page_frames_ / 2;   // 30 pixels a second
            int y = 200 - off;
            for (const char* c : kCredits) {
                std::string s = c;
                if (!s.empty() && y > -12 && y < 200) {
                    bool head = s[0] == '#';
                    if (head) { s = s.substr(1); fancy_text(160 - (int)s.size() * 4, y, s, 1, 0, {white, yellow, yellow, yellow, yellow, yellow, lred, lred}, black, dgrey); }
                    else centre(y, s, white);
                }
                y += s.empty() ? 8 : 13;
            }
            if (y < -4) set_title_page(1);
            break;
        }
        case 3: {  // intro slides over the title picture
            screen_.blit(title_, 0, 0);
            std::string t = slide_ == kSlideN - 1 && texts_.size() > 36 ? texts_[36] : kSlides[slide_] ? kSlides[slide_] : "";
            for (auto& ch : t) if (ch == '\r' || ch == '\n') ch = ' ';
            size_t shown = std::min(t.size(), (size_t)(title_page_frames_ / 2));
            screen_.fill_rect(8, 124, 304, 72, 0);
            screen_.fill_rect(8, 124, 304, 1, yellow); screen_.fill_rect(8, 195, 304, 1, yellow);
            auto lines = wrap(t.substr(0, shown), 37);
            auto full = wrap(t, 37);
            // wrap the complete text, then show only the typed part, so words do not jump between lines as they appear
            size_t used = 0;
            for (size_t i = 0; i < full.size() && i < 7; i++) {
                size_t n = std::min(full[i].size(), shown > used ? shown - used : 0);
                if (n) screen_.text(16, 129 + (int)i * 9, full[i].substr(0, n).c_str(), white);
                used += full[i].size() + 1;
            }
            (void)lines;
            char b[24]; snprintf(b, sizeof b, "%d/%d", slide_ + 1, kSlideN);
            screen_.text(280, 186, b, grey);
            screen_.text(16, 186, "Enter: next   Up: back   Esc: menu", dgrey);
            if (shown >= t.size() && (title_frames_ / 20) % 2 == 0)
                for (int r = 0; r < 4; r++) screen_.fill_rect(298 + r, 184 + r, 7 - 2 * r, 1, yellow);
            break;
        }
        case 5: {  // high scores on a scroll
            screen_.clear(0);
            draw_scroll("HIGH SCORES", 7);
            int y = 38;
            char b[64];
            for (size_t i = 0; i < highscores_.size() && i < 5; i++) { snprintf(b, sizeof b, "%d.%-16.16s%6d", (int)i + 1, highscores_[i].first.c_str(), highscores_[i].second); screen_.text(64, y, b, 10); y += 11; }
            screen_.text(96, 120, "Press any key", 8);
            break;
        }
        case 4:  // settings
            screen_.clear(0);
            draw_settings();
            screen_.fill_rect(0, 128, kW, kH - 128, 0);   // (the main menu's party panel is not wanted here)
            break;
    }
}

}  // namespace dof
