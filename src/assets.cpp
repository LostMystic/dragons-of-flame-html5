#include "assets.h"

#include <algorithm>
#include <sys/stat.h>

#include <cstdio>

namespace dof {

static inline int u16(const std::vector<uint8_t>& d, size_t o) { return o + 1 < d.size() ? d[o] | (d[o + 1] << 8) : 0; }

Image decode_picture_rows(const std::vector<uint8_t>& d, size_t off, int words, int h) {
    Image im;
    im.w = words * 16;
    im.h = h;
    im.px.assign((size_t)im.w * h, 0);
    size_t i = off;
    for (int y = 0; y < h; y++)
        for (int wc = 0; wc < words; wc++)
            for (int pl = 0; pl < 4; pl++)
                for (int b = 0; b < 2; b++) {
                    uint8_t v = i < d.size() ? d[i] : 0;
                    i++;
                    if (!v) continue;
                    int x0 = wc * 16 + b * 8;
                    for (int bit = 0; bit < 8; bit++)
                        if ((v >> (7 - bit)) & 1) im.px[(size_t)y * im.w + x0 + bit] |= 1 << pl;
                }
    return im;
}

Image decode_sprite_rows(const std::vector<uint8_t>& d, size_t off, int words, int h) {
    Image im;
    im.w = words * 16;
    im.h = h;
    im.px.assign((size_t)im.w * h, 0);
    size_t i = off;
    for (int y = 0; y < h; y++)
        for (int pl = 0; pl < 4; pl++)
            for (int bx = 0; bx < words * 2; bx++) {
                uint8_t v = i < d.size() ? d[i] : 0;
                i++;
                if (!v) continue;
                for (int bit = 0; bit < 8; bit++)
                    if ((v >> (7 - bit)) & 1) im.px[(size_t)y * im.w + bx * 8 + bit] |= 1 << pl;
            }
    return im;
}

bool Assets::init(const std::string& root) {
    root_ = root;
    return !game_file("title").empty();
}

std::vector<uint8_t> Assets::read(const std::string& path) const {
    std::vector<uint8_t> out;
    FILE* f = fopen((root_ + "/" + path).c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    if (n > 0 && fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
    fclose(f);
    return out;
}

// Some files hold their pixels the way the driver's image routine wants them: each pair of bytes swapped
// (the left and right 8 pixels of a 16-pixel column exchanged). Pictures shown through the driver's
// "show picture" call (title, pannel) are stored unswapped. See docs/formats.md.
static void swap_pairs(std::vector<uint8_t>& d, size_t from) {
    for (size_t i = from; i + 1 < d.size(); i += 2) std::swap(d[i], d[i + 1]);
}

Image Assets::picture(const std::string& name, Palette* own_palette) const {
    auto d = game_file(name);
    if (d.size() < 34) return {};
    if (name == "title") {  // 2 bytes, 16 palette registers (+overscan), then a 320x200 picture
        if (own_palette) *own_palette = ega_palette(&d[2]);
        return decode_picture_rows(d, 34, 20, 200);
    }
    if (name == "pax" || name == "child") swap_pairs(d, 4);   // (child too: unswapped it shows each 16-px column with its halves exchanged)
    return decode_picture_rows(d, 4, u16(d, 0), u16(d, 2));
}

std::vector<Image> Assets::tiles(const std::string& name) const {
    auto d = game_file(name);
    std::vector<Image> out;
    for (size_t t = 0; t * 128 + 128 <= d.size(); t++) out.push_back(decode_picture_rows(d, t * 128, 1, 16));
    return out;
}

std::vector<Image> Assets::clips() const {
    auto d = game_file("clips");
    auto sw = d;  // pixel data is in driver byte order; the w,h headers are not
    swap_pairs(sw, 0x44);
    std::vector<Image> out;
    size_t o = 0x44;
    while (o + 4 <= d.size()) {
        int w = u16(d, o), h = u16(d, o + 2);
        if (w < 1 || h < 1) break;
        out.push_back(decode_picture_rows(sw, o + 4, w, h));
        o += 4 + (size_t)w * h * 8;
    }
    return out;
}

std::vector<Image> Assets::figures(const std::string& name) const {
    auto d = game_file(name);
    std::vector<Image> out;
    if (d.size() < 0x4a) return out;
    for (int f = 0; f < 8; f++) {
        size_t ft = u16(d, 0x3a + 2 * f);
        if (ft + 8 > d.size()) return {};
        for (int v : {0, 1, 3}) {
            size_t off = u16(d, ft + 2 * v);
            if (off + 128 > d.size()) return {};
            out.push_back(decode_sprite_rows(d, off, 1, 16));
        }
    }
    return out;
}

// missle.dat: a table at 0x10 of 8-byte records [offset of a list of frame offsets, width in words, height, ?]; the number of frames of a record is the length of its list
// (up to the next list). Records 6 / 7 are the spinning throwing axe, 8 the long spear, 10 and 12 arrows, 9 a sling stone, 0 a small burst (see docs/formats.md).
std::vector<Image> Assets::missile(int record) const {
    auto d = game_file("missle");
    std::vector<Image> out;
    if (d.size() < 0x200 || record < 0 || record >= 50) return out;
    size_t off = u16(d, 0x10 + 8 * record), w = u16(d, 0x10 + 8 * record + 2), h = u16(d, 0x10 + 8 * record + 4);
    if (!off || !w || !h) return out;
    size_t next = d.size();
    for (int r = 0; r < 50; r++) { size_t o = u16(d, 0x10 + 8 * r); if (o > off && o < next) next = o; }
    for (size_t o = off; o + 2 <= next && o < 0x2a0 && out.size() < 12; o += 2) {   // (the frame lists run up to 0x2a0; a lower limit lost every sprite from record 18 up)
        size_t fo = u16(d, o);
        if (fo + w * h * 8 > d.size()) break;
        out.push_back(decode_sprite_rows(d, fo, (int)w, (int)h));
    }
    return out;
}

Actor Assets::actor(const std::string& name) const {
    auto d = game_file(name);
    Actor a;
    if (d.size() < 22) return a;
    a.height = u16(d, 0);
    size_t base = u16(d, 20), fsz = (size_t)a.height * 16;
    if (!fsz) return a;
    for (size_t k = 0; base + (k + 1) * fsz <= d.size(); k++) a.frames.push_back(decode_sprite_rows(d, base + k * fsz, 2, a.height));
    return a;
}

bool Assets::write(const std::string& path, const std::string& text) const {
    mkdir((root_ + "/user").c_str(), 0755);  // already there on the web (mounted by the page)
    FILE* f = fopen((root_ + "/" + path).c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}

}  // namespace dof
