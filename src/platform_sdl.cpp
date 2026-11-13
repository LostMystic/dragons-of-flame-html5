// SDL2 window for the web build (and native if SDL2 is available): 320x200 picture shown at 4:3 like a CRT.
#ifdef DOF_SDL
#include <SDL.h>

#include "platform.h"

namespace dof {
namespace {
class SdlPlatform : public Platform {
public:
    bool init() override {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return false;
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);  // optional: no controller support is not an error
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        win_ = SDL_CreateWindow("Dragons of Flame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE);
        if (!win_) return false;
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
        if (!ren_) return false;
        SDL_RenderSetLogicalSize(ren_, 320, 240);  // EGA pixels are 1:1.2 on a 4:3 monitor
        SDL_StartTextInput();
        tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
        return tex_ != nullptr;
    }
    Input poll() override {
        Input in;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) in.quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad_) pad_ = SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad_ && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_))) { SDL_GameControllerClose(pad_); pad_ = nullptr; }
            if (pad_on_ && e.type == SDL_CONTROLLERBUTTONDOWN) {  // A contextual (attack, take, open; confirm in menus), B jump (back in menus), X attack, Y switch view, shoulders: previous / next leader, Start: pause (main menu)
                in.any = true;
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_B: in.back = true; in.hop = true; break;
                    case SDL_CONTROLLER_BUTTON_START: in.start = true; break;
                    case SDL_CONTROLLER_BUTTON_Y: in.toggle = true; break;
                    case SDL_CONTROLLER_BUTTON_BACK: in.map = true; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: in.cycle = -1; break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: in.cycle = 1; break;
                    default: break;
                }
            }
            if (pad_on_ && e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                bool down = e.caxis.value > 16000;
                if (down && !rt_) in.turn = true;   // the right trigger turns the side view to the next perpendicular direction
                rt_ = down;
            }
            if (e.type == SDL_TEXTINPUT) in.text += e.text.text;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE) in.backspace = true;
            if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) in.wheel = e.wheel.y > 0 ? -1 : 1;
            if (e.type == SDL_MOUSEMOTION) { mx_ = e.motion.x; my_ = e.motion.y * 200 / 240; }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { mx_ = e.button.x; my_ = e.button.y * 200 / 240; in.click = true; in.any = false; }
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                in.any = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_V) in.mute = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F10) in.debug = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F1 && !(SDL_GetModState() & KMOD_SHIFT)) in.help = true;
                if (e.key.keysym.scancode >= SDL_SCANCODE_F1 && e.key.keysym.scancode <= SDL_SCANCODE_F7 && (SDL_GetModState() & KMOD_SHIFT)) in.level = e.key.keysym.scancode == SDL_SCANCODE_F7 ? 1 : e.key.keysym.scancode - SDL_SCANCODE_F1 + 2;
                if (e.key.keysym.scancode == SDL_SCANCODE_KP_7 || e.key.keysym.scancode == SDL_SCANCODE_Q) in.jump = -1;
                if (e.key.keysym.scancode == SDL_SCANCODE_Q) in.qkey = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_KP_9 || e.key.keysym.scancode == SDL_SCANCODE_E) in.jump = 1;
                if (e.key.keysym.scancode == SDL_SCANCODE_F5) in.save = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F9) in.load = true;
                if (e.key.keysym.scancode >= SDL_SCANCODE_1 && e.key.keysym.scancode <= SDL_SCANCODE_8) in.select = e.key.keysym.scancode - SDL_SCANCODE_1 + 1;
                if (e.key.keysym.scancode == SDL_SCANCODE_TAB || e.key.keysym.scancode == SDL_SCANCODE_KP_0 || e.key.keysym.scancode == SDL_SCANCODE_0) in.toggle = true;
            }
        }
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        in.left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A] || k[SDL_SCANCODE_KP_4];
        in.right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D] || k[SDL_SCANCODE_KP_6];
        in.up = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_KP_8];
        in.down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_KP_2];
        in.kp1 = k[SDL_SCANCODE_KP_1]; in.kp3 = k[SDL_SCANCODE_KP_3]; in.kp7 = k[SDL_SCANCODE_KP_7]; in.kp9 = k[SDL_SCANCODE_KP_9];
        if (pad_ && pad_on_) {  // the controller adds to the keyboard: left stick or d-pad move, A / X / Start act like Enter, Space and Esc
            auto b = [&](SDL_GameControllerButton x) { return SDL_GameControllerGetButton(pad_, x) != 0; };
            auto ax = [&](SDL_GameControllerAxis x) { return (int)SDL_GameControllerGetAxis(pad_, x); };
            const int dz = 12000;
            in.left = in.left || b(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax(SDL_CONTROLLER_AXIS_LEFTX) < -dz;
            in.right = in.right || b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax(SDL_CONTROLLER_AXIS_LEFTX) > dz;
            bool pu = b(SDL_CONTROLLER_BUTTON_DPAD_UP) || ax(SDL_CONTROLLER_AXIS_LEFTY) < -dz, pd = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ax(SDL_CONTROLLER_AXIS_LEFTY) > dz;
            in.pad_up = pu && !in.up; in.pad_down = pd && !in.down;   // (not when the keyboard is also holding it)
            in.up = in.up || pu;
            in.down = in.down || pd;
            in.act = b(SDL_CONTROLLER_BUTTON_A);   // contextual, see Game::frame
            in.attack = in.attack || b(SDL_CONTROLLER_BUTTON_X);
            in.ranged = in.ranged || ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;   // the left trigger: the ranged attack
        }
        in.mx = mx_; in.my = my_;
        in.ranged = in.ranged || k[SDL_SCANCODE_R] != 0;
        in.mouse_held = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        in.confirm = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_ENTER];
        in.cancel = k[SDL_SCANCODE_ESCAPE];
        in.attack = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_PLUS] || k[SDL_SCANCODE_X];
        return in;
    }
    void present(const Screen& s) override {
        buf_.resize(kW * kH);
        s.to_rgba(buf_.data());
        SDL_UpdateTexture(tex_, nullptr, buf_.data(), kW * 4);
        SDL_RenderClear(ren_);
        SDL_Rect dst{0, 0, 320, 240};
        SDL_RenderCopy(ren_, tex_, nullptr, &dst);
        SDL_RenderPresent(ren_);
    }
    int mx_ = -1, my_ = -1;
    double now_ms() override { return (double)SDL_GetTicks(); }
    void set_pad_enabled(bool on) override { pad_on_ = on; }
    void attach_audio(Sound* s) override {
        if (dev_) return;
        SDL_AudioSpec want{}, have{};
        want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
        want.callback = [](void* user, Uint8* stream, int len) { static_cast<Sound*>(user)->render(reinterpret_cast<int16_t*>(stream), len / 2); };
        want.userdata = s;
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (dev_) { s->set_rate(have.freq); SDL_PauseAudioDevice(dev_, 0); }
    }
    void lock_audio() override { if (dev_) SDL_LockAudioDevice(dev_); }
    void unlock_audio() override { if (dev_) SDL_UnlockAudioDevice(dev_); }

private:
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    SDL_AudioDeviceID dev_ = 0;
    SDL_GameController* pad_ = nullptr;
    bool pad_on_ = false;
    bool lt_ = false, rt_ = false;
    std::vector<uint32_t> buf_;
};
}  // namespace

std::unique_ptr<Platform> make_sdl_platform() { return std::make_unique<SdlPlatform>(); }
}  // namespace dof
#endif
