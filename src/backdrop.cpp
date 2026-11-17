// The far land behind the side view (an option; updated controls only). The strip the party walks is one row of map tiles; the rows of the map beside it
// (north of an east-west strip, west of a north-south one) are the land in the distance. Each row is drawn as a thin band of that tile's own picture,
// squeezed flat, each one further back smaller and mistier (a dither toward the sky colour, so only the game's own 16 colours are used).
// Rock rises in peaks, forest in round tree-tops, water turns blue (it is the sky's own colour in the tile art); creatures of the wilderness stand
// on the bands, small and misty. The turn between the two strips (compose_turn) swings the old view away and the new one in, like the faces of a cube.
#include <cmath>

#include "game.h"

#include <algorithm>

namespace dof {

namespace {
const int kBand[] = {6, 5, 4, 3, 2, 2};            // heights in pixels, nearest first
const int kMist[] = {0, 1, 3, 6, 9, 12};          // out of 16: how much of the sky the dither lets through
const int kPeak[] = {10, 8, 6, 5, 4, 3};           // how high a mountain stands above its band
const int kTree[] = {5, 4, 3, 3, 2, 2};            // and a tree-top
const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
const uint8_t kSky = 9;                            // the sky colour of the game palette
const int kN = 6;
enum Kind { kLand = 0, kWater, kRock, kForest };
}

int Game::backdrop_kind(int t) {
    if (t < 1 || t > (int)wtiles_.size()) return -1;
    if (backdrop_kind_.size() != wtiles_.size() + 1) { backdrop_kind_.assign(wtiles_.size() + 1, -1); backdrop_dom_.assign(wtiles_.size() + 1, 0); }
    if (backdrop_kind_[t] >= 0) return backdrop_kind_[t];
    int cnt[16] = {0};
    for (uint8_t v : wtiles_[t - 1].px) cnt[v & 15]++;
    int best = 0;
    for (int v = 1; v < 16; v++) if (cnt[v] > cnt[best]) best = v;
    int n = (int)wtiles_[t - 1].px.size(), green = cnt[14] + cnt[13];
    int kind = kLand;
    if (best == 9) kind = kWater;
    else if (best == 7 || best == 8) kind = kRock;
    else if (best != 14 && green * 100 >= n * 18 && green * 100 < n * 80) kind = kForest;   // trees standing on bare ground
    backdrop_dom_[t] = (uint8_t)(kind == kForest ? 14 : kind == kWater ? 10 : best);
    return backdrop_kind_[t] = kind;
}

// The top of the near ground at a strip column, averaged a little way either side so the far land does not copy every ledge.
int Game::backdrop_horizon(int X, int cam_y) const {
    int base = 0, n = 0;
    for (int d = -24; d <= 24; d += 8) { base += ground_y(std::clamp(X + d, 0, strip_.width_px() - 1)); n++; }
    return base / n - cam_y;
}

void Game::draw_backdrop(int cam_x, int cam_y) {
    for (int sx = 0; sx < kW; sx++) {
        const int X = cam_x + sx;
        if (X < 0 || X >= strip_.width_px()) continue;
        int y = backdrop_horizon(X, cam_y);
        for (int k = 0; k < kN; k++) {
            int t = strip_.far_tile(world_, X >> 7, k + 1), kind = backdrop_kind(t), bh = kBand[k];
            if (kind >= 0) {
                const Image& im = wtiles_[t - 1];
                uint8_t dom = backdrop_dom_[t];
                int ax = ((X & 127) >> 3) & 15;
                auto put = [&](int py, uint8_t c) {
                    if (py < 0 || py >= 128 || screen_.px[py * kW + sx] != kSky) return;   // only the open sky is replaced: nearer land stays in front
                    if (kMist[k] > kBayer[py & 3][sx & 3]) c = kSky;
                    screen_.px[py * kW + sx] = c;
                };
                for (int i = 0; i < bh; i++) {
                    int ay = 15 - (i * 16) / bh;                          // the far side of the tile is its top edge
                    uint8_t c = (k >= 2 || kind == kForest) ? dom : im.px[ay * im.w + ax];   // far away the speckle blurs into the tile's main colour
                    if (kind == kWater && c == 9) c = 10;
                    put(y - 1 - i, c);
                }
                int up = 0; uint8_t top = dom;                           // what rises above the band
                if (kind == kRock) {                                      // peaks: a triangle wave, lit on one side
                    int per = 44 - k * 4, ph = (X + k * 23) % per, half = per / 2;
                    up = kPeak[k] * (ph < half ? ph : per - ph) / half;
                    top = ph < half ? 7 : 8;
                } else if (kind == kForest) {                             // round tree-tops, a different size in each tree
                    int per = 10 - k / 2, ph = (X + k * 7) % per, tree = (X + k * 7) / per;
                    double u = (ph + 0.5) / per * 2 - 1;
                    up = (int)std::lround(kTree[k] * (0.6 + 0.4 * ((tree * 7) % 3) / 2.0) * std::sqrt(std::max(0.0, 1 - u * u)));
                    top = (ph + tree) % 3 == 0 ? 13 : 14;
                }
                for (int i = 0; i < up; i++) put(y - bh - 1 - i, top);
            }
            y -= bh;
        }
    }
    // the creatures of the wilderness in the distance: the front-facing figures, shrunk with the distance and misted like the land
    for (auto& w : wild_) {
        int depth = east_west_ ? (strip_.origin_y() - w.y) / 16 : (strip_.origin_x() - w.x) / 16;
        if (depth < 1 || depth > kN) continue;
        int X = strip_of_world(w.x + 8, w.y);
        int sx = X - cam_x;
        if (sx < -16 || sx > kW + 16) continue;
        const auto& fs = w.npc ? figs_ : figs2_;
        size_t f = (size_t)(east_west_ ? 4 : 2) * 3;                      // standing, turned towards the onlooker
        if (f >= fs.size()) continue;
        const Image& im = fs[f];
        int k = depth - 1, size = std::clamp(12 - 2 * k, 4, 12);
        int feet = backdrop_horizon(std::clamp(X, 0, strip_.width_px() - 1), cam_y);
        for (int j = 0; j < k; j++) feet -= kBand[j];
        feet -= kBand[k] / 2;                                             // standing in the middle of its band
        std::vector<uint8_t> sp((size + 2) * (size + 2), 0);          // the shrunk figure with a dark rim, so it stands out from the land
        for (int dy = 0; dy < size; dy++)
            for (int dx = 0; dx < size; dx++) sp[(dy + 1) * (size + 2) + dx + 1] = im.px[(dy * 16 / size) * im.w + dx * 16 / size];
        for (int dy = 0; dy < size + 2; dy++)
            for (int dx = 0; dx < size + 2; dx++) {
                uint8_t c = sp[dy * (size + 2) + dx];
                if (c == 0) {
                    bool near = (dx > 0 && sp[dy * (size + 2) + dx - 1]) || (dx < size + 1 && sp[dy * (size + 2) + dx + 1]) || (dy > 0 && sp[(dy - 1) * (size + 2) + dx]) || (dy < size + 1 && sp[(dy + 1) * (size + 2) + dx]);
                    if (!near) continue;
                    c = 8;
                }
                int px = sx - size / 2 - 1 + dx, py = feet - size - 1 + dy;
                if (px < 0 || px >= kW || py < 0 || py >= 128) continue;
                if (kMist[k] > kBayer[py & 3][px & 3]) continue;           // mist: the dither lets the land behind show through
                screen_.px[py * kW + px] = c;
            }
    }
}

// The turn between the two strips: the old view and the new one are the two faces of a cube swinging round (e: 0 to 1). The face nearest the seam grows,
// the outer edges shrink away, so what was in the distance seems to swing forward. Only the picture of the view (128 rows) is changed.
void Game::compose_turn(double e) {
    const int W = kW, H = 128;
    std::vector<uint8_t> nw(screen_.px.begin(), screen_.px.begin() + W * H);
    const double s = std::sin(M_PI * e);
    const double outer = 1.0 - 0.30 * s, seam = 1.0 + 0.16 * s;
    const int a = (int)std::lround(W * (1.0 - e));              // where the old face ends and the new one starts
    const bool flip = turn_dir_ > 0;                            // the swing goes the other way when turning back
    for (int x = 0; x < W; x++) {
        int dx = flip ? W - 1 - x : x;                          // work in "old on the left" coordinates
        bool old_face = dx < a;
        double sc, u;                                           // vertical scale; where in the face's own width the column lies
        if (old_face) { u = (dx + 0.5) / std::max(1, a); sc = outer + (seam - outer) * u; }
        else { u = (dx - a + 0.5) / std::max(1, W - a); sc = seam + (outer - seam) * u; }
        int src_x = (int)(u * W);
        src_x = std::clamp(src_x, 0, W - 1);
        if (flip) src_x = W - 1 - src_x;
        const std::vector<uint8_t>& src = old_face ? turn_old_ : nw;
        // the edge of each face next to the seam is darkened a little: the faces turn away from the light
        bool shade = old_face ? dx > a - 3 : dx < a + 3;
        for (int y = 0; y < H; y++) {
            double sy = 64 + (y - 64 + 0.5) / sc;
            uint8_t c = 0;
            if (sy >= 0 && sy < H) {
                c = src[(int)sy * W + src_x];
                if (shade && ((x + y) & 3) == 0) c = 0;
            }
            screen_.px[y * W + x] = c;
        }
    }
}

}  // namespace dof
