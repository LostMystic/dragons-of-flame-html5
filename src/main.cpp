#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "game.h"
#include "platform.h"

using namespace dof;

namespace {
struct Ctx { Game game; std::unique_ptr<Platform> plat; bool print = false; };

bool loop_once(Ctx* c) { return c->game.frame(*c->plat); }

#ifdef __EMSCRIPTEN__
void em_tick(void* arg) {
    auto* c = static_cast<Ctx*>(arg);
    if (!loop_once(c)) emscripten_cancel_main_loop();
}
#endif
}  // namespace

int main(int argc, char** argv) {
    std::string root = ".", keys, shot;
    int frames = 600, sx = -1, sy = -1;
    bool headless = false;
    int render_id = -1;
    double render_secs = 3;
    std::string render_path;
#ifdef __EMSCRIPTEN__
    root = "";  // preloaded files live at /game and /data
#endif
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--root") root = next();
        else if (a == "--headless") headless = true;
        else if (a == "--frames") frames = atoi(next().c_str());
        else if (a == "--keys") keys = next();
        else if (a == "--shot") shot = next();
        else if (a == "--start") sscanf(next().c_str(), "%d,%d", &sx, &sy);
        else if (a == "--render-sound") { render_id = atoi(next().c_str()); render_secs = atof(next().c_str()); render_path = next(); }
    }
    if (render_id >= 0) {  // test helper: write a sound to a 16-bit mono WAV
        Assets a; Sound snd;
        if (!a.init(root) || !snd.load(a)) { fprintf(stderr, "no sound data\n"); return 1; }
        snd.set_rate(22050); snd.play(render_id);
        int n = (int)(render_secs * 22050);
        std::vector<int16_t> pcm(n);
        snd.render(pcm.data(), n);
        FILE* f = fopen(render_path.c_str(), "wb");
        if (!f) return 1;
        uint32_t data = n * 2, riff = 36 + data, rate = 22050, byte_rate = rate * 2; uint16_t one = 1, bits = 16, align = 2; uint32_t sixteen = 16;
        fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
        fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(pcm.data(), 2, n, f); fclose(f);
        return 0;
    }
    Ctx ctx;
    if (headless) ctx.plat = make_headless_platform(keys, frames, shot);
#ifdef DOF_SDL
    else ctx.plat = make_sdl_platform();
#endif
    if (!ctx.plat || !ctx.plat->init()) { fprintf(stderr, "no usable platform (use --headless)\n"); return 1; }
    if (!ctx.game.init(root)) { fprintf(stderr, "could not load the game data from '%s'\n", root.c_str()); return 1; }
    if (sx >= 0) ctx.game.set_start(sx, sy);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(em_tick, &ctx, 0, 1);
#else
    while (loop_once(&ctx)) {}
    if (headless) printf("%s\n", ctx.game.debug().c_str());
#endif
    return 0;
}
