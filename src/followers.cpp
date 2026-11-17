// The companions in the side view: with the updated controls the other heroes trail behind the leader, shrinking and turning blue with distance, as if walking
// in the land behind. When the leader changes, each hero eases to their new place in line (about half a second): the new leader steps forward out of the haze
// while the old one drops back. Everything here is drawn before the creatures and the leader, so it only ever sits in the background.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace dof {

// nearest-neighbour scaled blit of a hero frame, feet on y_feet, centred on x_center, with a Bayer-dithered blue haze (0..16 sixteenths)
static void blit_far(Screen& s, const Image& im, int x_center, int y_feet, double scale, bool flip, int haze) {
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int dw = std::max(1, (int)std::lround(im.w * scale)), dh = std::max(1, (int)std::lround(im.h * scale));
    int x0 = x_center - dw / 2, y0 = y_feet - dh;
    for (int dy = 0; dy < dh; dy++) {
        int y = y0 + dy; if (y < s.clip_y || y >= s.clip_y + s.clip_h) continue;
        int sy = std::min(im.h - 1, (int)(dy / scale));
        for (int dx = 0; dx < dw; dx++) {
            int x = x0 + dx; if (x < s.clip_x || x >= s.clip_x + s.clip_w) continue;
            int sx = std::min(im.w - 1, (int)(dx / scale)); if (flip) sx = im.w - 1 - sx;
            uint8_t c = im.px[sy * im.w + sx];
            if (c == 0) continue;
            s.px[y * kW + x] = bayer[(y & 3) * 4 + (x & 3)] < haze ? 10 : c;   // 10 = the blue of the distance
        }
    }
}

bool Game::draw_followers(int cx, int cy, bool advance) {
    const int n = (int)party_.size();
    if (n < 2 || party_[leader_].hp <= 0) return false;
    const float S = 14.f;            // how far behind each one keeps to the one in front (strip px)
    const float speed = 1.35f;       // px per frame: a little quicker than the leader's walk, so they catch up
    bool fresh = (int)fol_d_.size() != n || render_frames_ - fol_seen_ > 3;   // first time, or the view was somewhere else: line up behind the leader
    if ((int)fol_d_.size() != n) { fol_d_.assign(n, 0.f); fol_x_.assign(n, 0.f); fol_anim_.assign(n, 0.f); fol_left_.assign(n, 0); }
    if (advance) fol_seen_ = render_frames_;
    // a foe in the strip: the companions do not fight, but they spread out around it in the background while the leader does; with it gone they follow again
    const Enemy* foe = nullptr; float fd = 1e9f;
    for (auto& e : enemies_) if (e.hp > 0 && e.refugee != 1 && std::fabs((float)e.x - (float)cx_) < std::min(fd, 110.f)) { fd = std::fabs((float)e.x - (float)cx_); foe = &e; }
    // the line: the leader first, then the others in party order
    std::vector<int> line; line.push_back(leader_);
    for (int i = 0; i < n; i++) if (i != leader_ && party_[i].hp > 0) line.push_back(i);
    for (size_t r = 0; r < line.size(); r++) {
        int h = line[r];
        if (fresh) { fol_d_[h] = (float)r; fol_x_[h] = (float)cx_ + (face_left_ ? 1 : -1) * S * (float)r; fol_left_[h] = face_left_; fol_anim_[h] = 0; }
        if (!advance) continue;
        float target_d = (float)r;
        fol_d_[h] += (target_d - fol_d_[h]) * 0.13f;   // ~half a second to settle at 60 frames a second
        if (std::fabs(target_d - fol_d_[h]) < 0.01f) fol_d_[h] = target_d;
        bool moving = false;
        if (r == 0) {   // the leader: the companion that was far back comes forward to the party's place
            float d = (float)cx_ - fol_x_[h];
            if (std::fabs(d) > 0.5f) { fol_x_[h] += d * 0.13f; }
            else fol_x_[h] = (float)cx_;
        } else if (foe) {   // the companions take places on both sides of the foe, a little way off, and face it
            int k = (int)r - 1, side = (k % 2 == 0) ? (cx_ < foe->x ? -1 : 1) : (cx_ < foe->x ? 1 : -1);   // the first on the leader's side, the next on the far side, and so on
            float spot = (float)foe->x + side * (30.f + 15.f * (float)(k / 2));
            float d = spot - fol_x_[h], ad = std::fabs(d);
            if (ad > 1.f) { float step = std::min(speed * 0.8f, ad); fol_x_[h] += (d > 0 ? 1 : -1) * step; moving = true; }
            fol_left_[h] = foe->x < fol_x_[h];
        } else {
            float ahead = fol_x_[line[r - 1]], d = ahead - fol_x_[h], ad = std::fabs(d);
            float step = 0; int dir = d >= 0 ? 1 : -1;
            if (ad > S) step = std::min(speed, ad - S);                            // too far behind: catch up (nobody else moves until they have to)
            else if (ad < S * 0.5f) {                                              // standing on top of the one in front: step back, away from them
                step = std::min(speed, S * 0.5f - ad);
                dir = d == 0.f ? (face_left_ ? 1 : -1) : (d > 0 ? -1 : 1);
            }
            if (step > 0.01f) { fol_x_[h] += dir * step; fol_left_[h] = dir < 0; moving = true; }
        }
        if (moving) fol_anim_[h] += 1.f; else fol_anim_[h] = 0.f;
    }
    bool in_transit = false;
    struct F { int hero; float d; };
    std::vector<F> list;
    for (int h : line) list.push_back({h, fol_d_[h]});
    std::sort(list.begin(), list.end(), [](const F& a, const F& b) { return a.d > b.d; });   // far first
    for (auto& e : list) {
        if (e.hero == leader_) { if (e.d > 0.02f) in_transit = true; else continue; }
        const Hero& h = party_[e.hero];
        if (h.actor.frames.empty()) continue;
        size_t f = 0;
        if (fol_anim_[e.hero] > 0.f && !rules_.walk_frames.empty()) { f = (size_t)rules_.walk_frames[((size_t)fol_anim_[e.hero] / 6) % rules_.walk_frames.size()]; if (f >= h.actor.frames.size()) f = 0; }   // their own walk cycle, whatever the leader is doing
        const Image& im = h.actor.frames[f];
        double d = e.d;
        double scale = std::max(0.5, 1.0 - 0.07 * d);
        int haze = (int)std::lround(std::min(10.0, d * 1.7));
        int xc = (int)std::lround(fol_x_[e.hero]) - cx;
        int feet = cy_ - cy + 16 - (int)std::lround(std::min(9.0, d * 1.3));
        blit_far(screen_, im, xc + im.w / 2 - 8, feet, scale, e.hero == leader_ ? face_left_ : (bool)fol_left_[e.hero], haze);
    }
    return in_transit;
}

}  // namespace dof
