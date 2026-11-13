#include "strip.h"

#include <algorithm>

#include "../third_party/json/json.hpp"

namespace dof {

bool Strip::load_tables(const Assets& a) {
    using nlohmann::json;
    try {
        json j = json::parse(a.read("data/strip_tables.json"));
        auto read = [&](const char* key, std::vector<std::array<uint8_t, 3>>& out) {
            out.clear();
            for (auto& t : j[key]) out.push_back({(uint8_t)t[0], (uint8_t)t[1], (uint8_t)t[2]});
        };
        read("north_south", ns_);
        read("east_west", ew_);
    } catch (...) {
        return false;
    }
    return !ns_.empty() && !ew_.empty();
}

void Strip::build(const World& w, int wx, int wy, bool east_west) {
    std::fill(blocks_.begin(), blocks_.end(), 0);
    const auto& table = east_west ? ew_ : ns_;
    auto put = [&](int n, int tile) {
        if (tile < 0 || tile >= (int)table.size()) tile = 0;
        const auto& b = table[tile];  // stored [row2,row1,row0]
        blocks_[0 * kLen + n] = b[2];
        blocks_[1 * kLen + n] = b[1];
        blocks_[2 * kLen + n] = b[0];
        blocks_[3 * kLen + n] = 10;
    };
    if (east_west) {
        for (int n = 0; n < 160; n++) put(n, w.tile_at(n * 16, wy));
    } else {
        int n = 0;
        for (int y = 0xbf0; y > 0; y -= 16) put(n++, w.tile_at(wx, y));
    }
}

int Strip::tile_at(const World& w, int x, int y) const {
    if (x < 0 || y < 0 || x >= width_px() || y >= height_px()) return -1;
    int block = block_at(x >> 7, y >> 7);
    return w.token_tile(block, ((y & 0x7f) >> 4) * 8 + ((x & 0x7f) >> 4));
}

bool Strip::solid_at(const World& w, int x, int y) const {
    int t = tile_at(w, x, y);
    return t < 0 || w.tile_solid(t);
}

bool Strip::can_move(const World& w, int x, int y, int dx, int dy) const {
    int nx = x + dx, ny = y + dy;
    if (dx < 0) return !solid_at(w, nx, ny) && !solid_at(w, nx, ny + 15);
    if (dx > 0) return !solid_at(w, nx + 15, ny) && !solid_at(w, nx + 15, ny + 15);
    if (dy < 0) return !solid_at(w, nx, ny) && !solid_at(w, nx + 15, ny);
    if (dy > 0) return !solid_at(w, nx, ny + 15) && !solid_at(w, nx + 15, ny + 15);
    return true;
}

// Back to the wilderness view: the position along the strip becomes the world position again.
int Strip::world_from_x(int cx, int wx, int wy, bool east_west, int* nwx, int* nwy) const {
    if (east_west) { *nwx = std::clamp(cx >> 3, 0, 2559); *nwy = wy; }
    else { *nwx = wx; *nwy = std::clamp(3056 - (cx >> 3), 0, 3071); }
    return 0;
}

}  // namespace dof
