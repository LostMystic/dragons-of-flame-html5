// Loads and decodes the original game's data files (see docs/formats.md).
#pragma once
#include <string>
#include <vector>

#include "gfx.h"

namespace dof {

struct Actor {  // a hero or monster file: frames of 32 x height pixels
    int height = 0;
    std::vector<Image> frames;
};

class Assets {
public:
    // root contains the original files in game/ and the editable data in data/
    bool init(const std::string& root);
    const std::string& root() const { return root_; }

    std::vector<uint8_t> read(const std::string& path) const;  // path relative to root; empty if missing
    bool write(const std::string& path, const std::string& text) const;  // creates user/ if needed
    std::vector<uint8_t> game_file(const std::string& name) const { return read("game/" + name + ".dat"); }

    Image picture(const std::string& name, Palette* own_palette = nullptr) const;  // title, pannel, child, pax
    std::vector<Image> tiles(const std::string& name) const;                        // wtiles, qtiles, utiles
    std::vector<Image> clips() const;                                              // portraits and UI pieces
    std::vector<Image> figures(const std::string& name) const;                     // mpl0-2: 16x16 map figures, index = frame * 3 + view
    Actor actor(const std::string& name) const;                                    // tanis ... msvr
    std::vector<Image> missile(int record) const;                                  // missle.dat: the frames of one projectile type (record = index in its table)

private:
    std::string root_;
};

// Pixel layouts. See docs/formats.md.
Image decode_picture_rows(const std::vector<uint8_t>& d, size_t off, int words, int h);  // per column: plane 0..3
Image decode_sprite_rows(const std::vector<uint8_t>& d, size_t off, int words, int h);   // per row: plane 0 across, then 1..3

}  // namespace dof
