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

bool Game::draw_followers(int cx, int cy) {
    const int n = (int)party_.size();
    if (n < 2 || party_[leader_].hp <= 0) return false;
    if ((int)fol_d_.size() != n) { fol_d_.assign(n, -1.f); fol_side_.assign(n, 0.f); }
    const float side_target = face_left_ ? 1.f : -1.f;   // behind the leader: the side they are not facing
    int rank = 0;
    struct F { int hero; float d; };
    std::vector<F> list;
    for (int i = 0; i < n; i++) {
        if (party_[i].hp <= 0) continue;
        float target = i == leader_ ? 0.f : (float)(++rank);
        if (fol_d_[i] < 0) { fol_d_[i] = target; fol_side_[i] = side_target; }   // first seen: already in place
        else {
            fol_d_[i] += (target - fol_d_[i]) * 0.13f;   // ~half a second to settle at 60 frames a second
            if (std::fabs(target - fol_d_[i]) < 0.01f) fol_d_[i] = target;
            fol_side_[i] += (side_target - fol_side_[i]) * (0.10f / (1.f + 0.25f * fol_d_[i]));   // the ones further back turn a little later
        }
        list.push_back({i, fol_d_[i]});
    }
    std::sort(list.begin(), list.end(), [](const F& a, const F& b) { return a.d > b.d; });   // far first
    bool in_transit = false;
    for (auto& e : list) {
        if (e.hero == leader_) { if (e.d > 0.02f) in_transit = true; else continue; }
        const Hero& h = party_[e.hero];
        if (h.actor.frames.empty()) continue;
        size_t f = rules_.walk_frames.empty() ? 0 : (size_t)rules_.walk_frames[anim_ % rules_.walk_frames.size()];
        if (anim_ == 0 || f >= h.actor.frames.size()) f = 0;
        const Image& im = h.actor.frames[f];
        double d = e.d;
        double scale = std::max(0.5, 1.0 - 0.07 * d);
        int haze = (int)std::lround(std::min(10.0, d * 1.7));
        int xc = cx_ - cx + (int)std::lround(fol_side_[e.hero] * 17.0 * d);
        int feet = cy_ - cy + 16 - (int)std::lround(std::min(9.0, d * 1.3));
        // the frames are drawn with their left edge 8 px left of the hero's centre, so the middle of a frame is at +im.w/2 - 8
        blit_far(screen_, im, xc + im.w / 2 - 8, feet, scale, face_left_, haze);
    }
    return in_transit;
}

}  // namespace dof
