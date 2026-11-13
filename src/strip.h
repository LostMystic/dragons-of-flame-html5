// The combat view's terrain: a strip of 128-px blocks, 192 long and 4 high, cut from the world map along
// the line the party travels (game.exe fn 54CC, outdoor branch). Each world tile on that line becomes a
// column of blocks through a lookup table; row 3 is always block 10.
#pragma once
#include <vector>

#include "assets.h"
#include "world.h"

namespace dof {

class Strip {
public:
    static constexpr int kLen = 192, kRows = 4;
    bool load_tables(const Assets& a);  // data/strip_tables.json
    // wx, wy: party position in world pixels (the wilderness view). east_west: the strip runs along x, else along y.
    void build(const World& w, int wx, int wy, bool east_west);

    int width_px() const { return kLen * 128; }
    int height_px() const { return kRows * 128; }
    int block_at(int bx, int by) const { return bx < 0 || by < 0 || bx >= kLen || by >= kRows ? 0 : blocks_[by * kLen + bx]; }
    int tile_at(const World& w, int x, int y) const;  // tile number at a strip pixel; -1 outside
    bool solid_at(const World& w, int x, int y) const;
    bool can_move(const World& w, int x, int y, int dx, int dy) const;  // 16x16 body, leading corners, like the original

    // Where the party stands in the strip for a world position (combat px).
    int start_x(int wx, int wy, bool east_west) const { return east_west ? wx * 8 + 0x40 : (3056 - wy) * 8 + 0x40; }
    int world_from_x(int cx, int wx, int wy, bool east_west, int* nwx, int* nwy) const;

private:
    std::vector<uint8_t> blocks_ = std::vector<uint8_t>(kLen * kRows, 0);
    std::vector<std::array<uint8_t, 3>> ns_, ew_;
};

}  // namespace dof
