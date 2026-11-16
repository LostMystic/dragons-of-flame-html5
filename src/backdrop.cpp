// The far land behind the side view (an option; updated controls only). The strip the party walks is one row of map tiles; the rows of the map beside it
// (north of an east-west strip, west of a north-south one) are the land in the distance. Each row is drawn as a thin band of that tile's own picture,
// squeezed flat, each one further back smaller and mistier (a dither toward the sky colour, so only the game's own 16 colours are used).
#include "game.h"

#include <algorithm>

namespace dof {

void Game::draw_backdrop(int cam_x, int cam_y) {
    static const int kBand[] = {6, 5, 4, 3, 2, 2};          // heights in pixels, nearest first
    static const int kMist[] = {0, 2, 5, 8, 11, 13};        // out of 16: how much of the sky the dither lets through
    static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    std::vector<uint8_t>& dom = backdrop_dom_;
    if (dom.size() != wtiles_.size() + 1) dom.assign(wtiles_.size() + 1, 255);
    auto dominant = [&](int t, const Image& im) -> uint8_t {   // the commonest colour of a tile picture, worked out once
        if (dom[t] != 255) return dom[t];
        int cnt[16] = {0};
        for (uint8_t v : im.px) cnt[v & 15]++;
        int best = 0;
        for (int v = 1; v < 16; v++) if (cnt[v] > cnt[best]) best = v;
        return dom[t] = (uint8_t)best;
    };
    const uint8_t sky = 9;   // the sky colour of the game palette
    const int nb = (int)(sizeof kBand / sizeof kBand[0]);
    for (int sx = 0; sx < kW; sx++) {
        const int X = cam_x + sx;
        if (X < 0 || X >= strip_.width_px()) continue;
        // the horizon of this column: the top of the ground a little way off, averaged so the far land does not copy every ledge
        int base = 0, n = 0;
        for (int d = -24; d <= 24; d += 8) { int gx = std::clamp(X + d, 0, strip_.width_px() - 1); base += ground_y(gx); n++; }
        base = base / n - cam_y;
        int y = base;
        for (int k = 0; k < nb; k++) {
            int t = strip_.far_tile(world_, X >> 7, k + 1);
            int bh = kBand[k];
            if (t >= 1 && t <= (int)wtiles_.size()) {
                const Image& im = wtiles_[t - 1];
                int ax = ((X & 127) >> 3) & 15;
                for (int i = 0; i < bh; i++) {
                    int py = y - 1 - i;                                   // upward from the near edge of the band
                    if (py < 0 || py >= 128) continue;
                    if (screen_.px[py * kW + sx] != sky) continue;       // only the open sky is replaced
                    int ay = 15 - (i * 16) / bh;                          // the far side of the tile is its top edge
                    uint8_t c = im.px[ay * im.w + ax];
                    if (k >= 2) c = dominant(t, im);                      // far away the speckle of the tile blurs into its main colour
                    if (kMist[k] > kBayer[py & 3][sx & 3]) c = sky;
                    screen_.px[py * kW + sx] = c;
                }
            }
            y -= bh;
        }
    }
}

}  // namespace dof
