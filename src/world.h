// The overworld: a grid of 128-px blocks (each 8x8 tiles of 16x16 px) plus per-tile attributes.
// Mirrors game.exe fn 4D12 (tile at a pixel) and the movement probes at 31CB-3340.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "assets.h"

namespace dof {

class World {
public:
    // Reads data/world_map.json and data/tile_attributes.json plus stokens.dat.
    bool load(const Assets& a);

    // The playable area: the bounding box of the blocks that differ from the corner block (the grey ring round the map). The camera stops at its edge.
    void play_area(int* x0, int* y0, int* x1, int* y1) const;
    int width_px() const { return bw_ * 128; }
    int height_px() const { return bh_ * 128; }
    int tile_at(int x, int y) const;  // tile number at a world pixel; -1 outside the map
    int attr_at(int x, int y) const;  // attribute byte of that tile; outside counts as solid
    bool solid_at(int x, int y) const;
    // A 16x16 body at (x,y) moving by (dx,dy): the leading two corners must be clear (as the original probes).
    bool can_move(int x, int y, int dx, int dy) const;

    int token_tile(int block, int i) const { return block >= 0 && block < (int)tokens_.size() ? tokens_[block][i] : -1; }
    int attr_of_tile(int t) const { return t >= 0 && t < (int)attrs_.size() ? attrs_[t] : blocked_value; }
    int tile_attr(int t) const { return t >= 0 && t < (int)attrs_.size() ? attrs_[t] : 1; }
    bool tile_solid(int t) const { return (attr_of_tile(t) & blocked_mask) == blocked_value; }

    int blocked_mask = 0x0f, blocked_value = 1;  // from data/rules.json

private:
    int bw_ = 0, bh_ = 0;
    std::vector<uint8_t> blocks_;
    std::vector<std::array<uint16_t, 64>> tokens_;
    std::vector<uint8_t> attrs_;
};

}  // namespace dof
