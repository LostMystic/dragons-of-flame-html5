#include "gfx.h"

#include <algorithm>

#include "../third_party/font8x8/font8x8_basic.h"

namespace dof {

Palette ega_palette(const uint8_t regs[16]) {
    Palette p{};
    for (int i = 0; i < 16; i++) {
        int v = regs[i];
        int r = ((v >> 2) & 1) * 2 + ((v >> 5) & 1);
        int g = ((v >> 1) & 1) * 2 + ((v >> 4) & 1);
        int b = (v & 1) * 2 + ((v >> 3) & 1);
        if (r == 2 && g == 2 && b == 0) g = 1;  // dark yellow shows as brown on an EGA monitor
        p[i] = (uint32_t)(r * 85) | ((uint32_t)(g * 85) << 8) | ((uint32_t)(b * 85) << 16) | 0xff000000u;
    }
    return p;
}

static Palette make_game_palette() {
    static const uint8_t regs[16] = {0, 59, 61, 63, 0, 4, 60, 7, 56, 3, 57, 60, 6, 58, 2, 62};
    return ega_palette(regs);
}
const Palette kGamePalette = make_game_palette();

void Screen::clear(uint8_t c) { std::fill(px.begin(), px.end(), c); }

void Screen::set_clip(int x, int y, int w, int h) {
    clip_x = std::max(0, x);
    clip_y = std::max(0, y);
    clip_w = std::min(kW, x + w) - clip_x;
    clip_h = std::min(kH, y + h) - clip_y;
}

void Screen::blit(const Image& im, int x, int y, bool transparent, bool flip) {
    for (int sy = 0; sy < im.h; sy++) {
        int dy = y + sy;
        if (dy < clip_y || dy >= clip_y + clip_h) continue;
        for (int sx = 0; sx < im.w; sx++) {
            int dx = x + sx;
            if (dx < clip_x || dx >= clip_x + clip_w) continue;
            uint8_t v = im.px[sy * im.w + (flip ? im.w - 1 - sx : sx)];
            if (transparent && v == 0) continue;
            px[dy * kW + dx] = v;
        }
    }
}

void Screen::blit_dither(const Image& im, int x, int y, bool flip, int level) {
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int sy = 0; sy < im.h; sy++) {
        int dy = y + sy;
        if (dy < clip_y || dy >= clip_y + clip_h) continue;
        for (int sx = 0; sx < im.w; sx++) {
            int dx = x + sx;
            if (dx < clip_x || dx >= clip_x + clip_w) continue;
            uint8_t v = im.px[sy * im.w + (flip ? im.w - 1 - sx : sx)];
            if (v == 0 || bayer[dy & 3][dx & 3] >= level) continue;
            px[dy * kW + dx] = v;
        }
    }
}

void Screen::fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int yy = std::max(y, clip_y); yy < std::min(y + h, clip_y + clip_h); yy++)
        for (int xx = std::max(x, clip_x); xx < std::min(x + w, clip_x + clip_w); xx++) px[yy * kW + xx] = c;
}

void Screen::text(int x, int y, const char* str, uint8_t color, bool shadow) {
    if (y + 9 > text_max_y) return;
    for (int pass = shadow ? 0 : 1; pass < 2; pass++)
        for (int i = 0; str[i]; i++) {
            unsigned char ch = (unsigned char)str[i];
            if (ch > 127) continue;
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                    if ((font8x8_basic[ch][r] >> c) & 1) {
                        int xx = x + i * 8 + c + (pass == 0), yy = y + r + (pass == 0);
                        if (xx >= clip_x && xx < clip_x + clip_w && yy >= clip_y && yy < clip_y + clip_h) px[yy * kW + xx] = pass == 0 ? 4 : color;
                    }
        }
}

void Screen::to_rgba(uint32_t* out) const {
    for (int i = 0; i < kW * kH; i++) out[i] = pal[px[i] & 15];
}

}  // namespace dof
