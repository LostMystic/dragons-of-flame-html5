// Headless platform for tests: scripted keys, a fake clock (one rendered frame = 1/60 s), PPM screenshots.
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "platform.h"

namespace dof {

namespace {
struct Step { std::string key; int frames; };

class Headless : public Platform {
public:
    Headless(const std::string& keys, int frames, const std::string& shot) : max_frames_(frames), shot_(shot) {
        std::stringstream ss(keys);
        std::string item;
        while (std::getline(ss, item, ',')) {  // "right:30,wait:5,enter:2"
            auto c = item.find(':');
            if (c == std::string::npos) continue;
            steps_.push_back({item.substr(0, c), atoi(item.c_str() + c + 1)});
        }
    }
    bool init() override { return true; }
    Input poll() override {
        Input in;
        if (frame_ >= max_frames_) { in.quit = true; return in; }
        // find the step active at this frame
        int f = frame_, acc = 0;
        for (auto& s : steps_) {
            if (f < acc + s.frames) {
                const std::string& k = s.key;
                in.left = k == "left"; in.right = k == "right"; in.up = k == "up"; in.down = k == "down";
                in.confirm = k == "enter" || k == "space";
                in.cancel = k == "esc";
                in.toggle = k == "toggle" && f == acc;
                in.rest = k == "rest" && f == acc;
                if (k.rfind("clk", 0) == 0 && f == acc) { sscanf(k.c_str() + 3, "%d_%d", &in.mx, &in.my); in.click = true; }
                if (k.rfind("txt", 0) == 0 && f == acc) in.text = k.substr(3);
                if (k == "bksp" && f == acc) in.backspace = true;
                if (k == "jumpl" && f == acc) in.jump = -1;
                if (k == "jumpr" && f == acc) in.jump = 1;
                in.attack = k == "attack"; in.act = k == "act";
                in.mute = k == "mute" && f == acc;
                in.turn = k == "turn" && f == acc;
                if (k == "padup") { in.up = true; in.pad_up = true; }
                if (k == "paddown") { in.down = true; in.pad_down = true; }
                in.map = k == "map" && f == acc;
                if ((k == "lb" || k == "rb") && f == acc) in.cycle = k == "lb" ? -1 : 1;
                in.ranged = k == "ranged";
                in.qkey = k == "qkey" && f == acc;
                in.start = k == "start" && f == acc;
                in.help = k == "help" && f == acc;
                in.debug = k == "debug" && f == acc;
                in.back = k == "back" && f == acc;
                in.save = k == "save" && f == acc;
                in.load = k == "load" && f == acc;
                if (k.size() == 4 && k.compare(0, 3, "sel") == 0 && f == acc) in.select = k[3] - '0';
                if (k.size() == 4 && k.compare(0, 3, "lvl") == 0 && f == acc) in.level = k[3] - '0';
                in.any = (k != "wait") && (f == acc);  // key goes down on the first frame of the step
                if (k == "upright") { in.up = in.right = true; }
                if (k == "upleft") { in.up = in.left = true; }
                if (k == "downright") { in.down = in.right = true; }
                if (k == "downleft") { in.down = in.left = true; }
                break;
            }
            acc += s.frames;
        }
        frame_++;
        return in;
    }
    void present(const Screen& s) override { last_ = s; }
    double now_ms() override { return frame_ * (1000.0 / 60.0); }
    void shot(const Screen& s, const std::string& path) override {
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) return;
        fprintf(f, "P6\n%d %d\n255\n", kW, kH);
        for (int i = 0; i < kW * kH; i++) {
            uint32_t c = s.pal[s.px[i] & 15];
            fputc(c & 255, f); fputc((c >> 8) & 255, f); fputc((c >> 16) & 255, f);
        }
        fclose(f);
    }
    ~Headless() override { if (!shot_.empty()) shot(last_, shot_); }

private:
    std::vector<Step> steps_;
    int frame_ = 0, max_frames_;
    std::string shot_;
    Screen last_;
};
}  // namespace

std::unique_ptr<Platform> make_headless_platform(const std::string& keys, int frames, const std::string& shot_path) {
    return std::make_unique<Headless>(keys, frames, shot_path);
}

}  // namespace dof
