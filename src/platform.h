// What the engine needs from the host: a window with a 320x200 picture, keyboard state and a clock.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gfx.h"
#include "sound.h"

namespace dof {

struct Input {
    bool left = false, right = false, up = false, down = false;
    bool confirm = false;  // Enter, Space
    bool cancel = false;   // Esc
    bool attack = false;   // Space, numpad +, X (held)
    int select = 0;        // 1-8: choose a party member as leader (edge)
    bool save = false, load = false;  // F5 / F9 (edge)
    bool mute = false;     // V: sound on/off (edge), as in the original
    bool toggle = false;   // Tab or numpad 0: switch between the wilderness and combat views (edge)
    bool rest = false;     // 0 / numpad 0: rest (the original's numpad 0 is 'You feel refreshed having rested')
    bool kp1 = false, kp3 = false, kp7 = false, kp9 = false;  // keypad diagonals: walk diagonally outdoors, jump (7, 9) in the side view
    int mx = -1, my = -1;  // mouse position in game pixels (320x200); -1 = unknown
    bool click = false;    // left button went down (edge)
    std::string text;      // characters typed this frame (for the high score name)
    bool backspace = false;
    int jump = 0;          // -1 / +1: jump left or right (keypad 7 / 9, Q / E)
    bool mouse_held = false;  // left button is down
    int wheel = 0;            // mouse wheel: -1 up, +1 down (edge)
    bool act = false;      // controller A (held): contextual - attack a foe in reach, else take what lies here, else open a door or chest
    bool qkey = false;     // the Q key (edge): the quest map with the original controls, a jump with the updated ones
    bool debug = false;    // F10 (edge): the debug menu
    bool help = false;     // F1 (edge): the list of commands
    bool map = false;      // controller Back (edge): the world (quest) map
    bool start = false;    // controller Start (edge): pause = open the main menu; it does not close it again (B or Exit menu does)
    bool ranged = false;   // LT / R (held): the ranged attack: the ranged weapon, or the casters' spells
    bool pad_up = false, pad_down = false;  // up / down come from the controller (stick or d-pad): they walk on the map but neither jump nor turn the side view
    bool turn = false;     // controller trigger (edge): turn the side view to the next perpendicular direction
    bool hop = false;      // controller jump button (edge): jump the way the hero faces
    bool back = false;     // controller B: backs out of menus
    int cycle = 0;         // -1 / +1: previous or next party leader (controller shoulder buttons, edge)
    int level = 0;         // F1-F6 enter interior level 2-7, F7 back outdoors (edge; levels are 2..7, 1 = outdoors)
    bool any = false;      // any key went down this frame
    bool quit = false;
};

class Platform {
public:
    virtual ~Platform() = default;
    virtual bool init() = 0;
    virtual Input poll() = 0;                // called once per rendered frame
    virtual void present(const Screen&) = 0;
    virtual double now_ms() = 0;
    virtual void shot(const Screen&, const std::string&) {}
    virtual void set_pad_enabled(bool) {}   // gamepad input is part of the updated controls only
    virtual void attach_audio(Sound*) {}  // start feeding the sound sequencer to the speakers
    virtual void lock_audio() {}
    virtual void unlock_audio() {}
};

std::unique_ptr<Platform> make_headless_platform(const std::string& keys, int frames, const std::string& shot_path);
#ifdef DOF_SDL
std::unique_ptr<Platform> make_sdl_platform();
#endif

}  // namespace dof
