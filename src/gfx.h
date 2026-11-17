// 320x200 indexed-colour screen (EGA, 16 colours) and the image type the decoders produce.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace dof {

constexpr int kW = 320, kH = 200;
using Palette = std::array<uint32_t, 16>;  // 0xAABBGGRR, i.e. R,G,B,A bytes in memory

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // palette indices, w*h
    bool empty() const { return px.empty(); }
};

Palette ega_palette(const uint8_t regs[16]);  // 6-bit EGA register values -> RGBA
extern const Palette kGamePalette;            // the in-game palette (game.exe data 0x1c76)

struct Screen {
    std::vector<uint8_t> px = std::vector<uint8_t>(kW * kH, 0);
    Palette pal{};
    int text_max_y = 1 << 30;  // text() draws no line whose bottom is below this (a scroll still unrolling shows its lines one by one as the paper opens)
    int clip_x = 0, clip_y = 0, clip_w = kW, clip_h = kH;  // drawing is limited to this rectangle

    void clear(uint8_t c = 0);
    void set_clip(int x, int y, int w, int h);
    void reset_clip() { set_clip(0, 0, kW, kH); }
    // transparent: colour 0 is skipped. flip: mirror horizontally.
    void blit(const Image& im, int x, int y, bool transparent = false, bool flip = false);
    // blit with an ordered-dither mask: level 0 shows nothing, 16 everything (a person materialising)
    void blit_dither(const Image& im, int x, int y, bool flip, int level);
    void fill_rect(int x, int y, int w, int h, uint8_t c);
    void text(int x, int y, const char* str, uint8_t color, bool shadow = true);  // 8x8 font, ASCII
    void to_rgba(uint32_t* out) const;
};

}  // namespace dof
