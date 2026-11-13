#include <algorithm>
#include "world.h"

#include "../third_party/json/json.hpp"

namespace dof {

bool World::load(const Assets& a) {
    using nlohmann::json;
    try {
        auto wm = json::parse(a.read("data/world_map.json"));
        bw_ = wm["width"];
        bh_ = wm["height"];
        blocks_.clear();
        for (auto& row : wm["blocks"])
            for (int v : row) blocks_.push_back((uint8_t)v);
        auto ta = json::parse(a.read("data/tile_attributes.json"));
        attrs_.clear();
        for (int v : ta["tiles"]) attrs_.push_back((uint8_t)v);
    } catch (...) {
        return false;
    }
    auto d = a.game_file("stokens");  // 132-byte blocks: 8, 8, then 64 tile numbers
    tokens_.clear();
    for (size_t b = 0; b * 132 + 132 <= d.size(); b++) {
        std::array<uint16_t, 64> t{};
        for (int i = 0; i < 64; i++) t[i] = d[b * 132 + 4 + i * 2] | (d[b * 132 + 5 + i * 2] << 8);
        tokens_.push_back(t);
    }
    return (int)blocks_.size() == bw_ * bh_ && !tokens_.empty();
}

void World::play_area(int* x0, int* y0, int* x1, int* y1) const {
    int bx0 = bw_, by0 = bh_, bx1 = -1, by1 = -1;
    if (!blocks_.empty()) {
        auto edge = blocks_[0];
        for (int y = 0; y < bh_; y++) for (int x = 0; x < bw_; x++)
            if (blocks_[(size_t)y * bw_ + x] != edge) { bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
    }
    if (bx1 < 0) { bx0 = 0; by0 = 0; bx1 = bw_ - 1; by1 = bh_ - 1; }
    *x0 = bx0 * 128; *y0 = by0 * 128; *x1 = (bx1 + 1) * 128; *y1 = (by1 + 1) * 128;
}

int World::tile_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_px() || y >= height_px()) return -1;
    size_t block = blocks_[(size_t)(y >> 7) * bw_ + (x >> 7)];
    if (block >= tokens_.size()) return -1;
    return tokens_[block][((y & 0x7f) >> 4) * 8 + ((x & 0x7f) >> 4)];
}

int World::attr_at(int x, int y) const {
    int t = tile_at(x, y);
    if (t < 0 || t >= (int)attrs_.size()) return blocked_value;  // off the map: solid
    return attrs_[t];
}

bool World::solid_at(int x, int y) const { return (attr_at(x, y) & blocked_mask) == blocked_value; }

bool World::can_move(int x, int y, int dx, int dy) const {
    int nx = x + dx, ny = y + dy;
    if (dx < 0) return !solid_at(nx, ny) && !solid_at(nx, ny + 15);
    if (dx > 0) return !solid_at(nx + 15, ny) && !solid_at(nx + 15, ny + 15);
    if (dy < 0) return !solid_at(nx, ny) && !solid_at(nx + 15, ny);
    if (dy > 0) return !solid_at(nx, ny + 15) && !solid_at(nx + 15, ny + 15);
    return true;
}

}  // namespace dof
