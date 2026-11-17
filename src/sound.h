// The original's PC speaker sound: note streams [ticks, timer divisor] played as square waves.
// (game.exe fn 822A: a tick is 1/18.2065 s; frequency = 1193182 / divisor; divisor 0 is a rest.)
#pragma once
#include <cstdint>
#include <map>
#include <vector>

#include "assets.h"

namespace dof {

// The synthesized effects (src/fx.cpp). Missile effects are kFxMissile + 3 * (0 arrow, 1 spear, 2 throwing axe, 3 sling) + (0 thrown, 1 hits, 2 lands).
enum Fx { kFxWhiffSword, kFxHitSword, kFxGlanceSword, kFxWhiffStaff, kFxHitStaff, kFxGlanceStaff, kFxWhiffAxe, kFxHitAxe, kFxGlanceAxe,
          kFxMissile, kFxPcHit = kFxMissile + 12, kFxTurn, kFxWyrmWhiff, kFxWyrmHit, kFxWyrmGlance,
          // Raistlin's spells
          kFxMagicMissile, kFxBurningHands, kFxFireball, kFxFireballBoom, kFxFinalStrike, kFxCharm, kFxSleep, kFxWeb, kFxDetectMagic, kFxDetectInvisible,
          // Goldmoon's spells (every cure is the same)
          kFxBuzz, kFxLightning, kFxSlab, kFxFanfare, kFxCrumble, kFxAcid, kFxBoneBurst, kFxCure, kFxProtection, kFxFindTraps, kFxHold, kFxHammer, kFxPrayer, kFxBless, kFxDispel,
          // menus: a selecting click and a backward whoosh
          kFxMenuSelect, kFxMenuBack,
          // a wandering group arrives in a dungeon: the Dragon Army's war horn and drums, or the dead
          kFxStingArmy, kFxStingUndead,
          kFxCount };
std::vector<int16_t> make_fx(int id, int rate);

class Sound {
public:
    bool load(const Assets& a);  // data/sounds.json
    void play(int id);           // start a note-stream sound (others are ignored for now)
    void stop();
    void play_fx(int fx, double gain = 1.0);   // a synthesized effect on top of whatever else plays (several can overlap)
    void set_muted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }
    bool playing() const { return cur_ != nullptr; }
    void set_rate(int r) { rate_ = r; }
    // Fill mono 16-bit samples. Called from the audio thread; the host locks around play()/stop().
    void render(int16_t* out, int frames);

private:
    struct Def { std::vector<std::pair<int, int>> notes; bool loop = false; std::vector<std::pair<double, bool>> toggles; };  // toggles: seconds until the next timer interrupt, whether it flips the speaker
    std::map<int, Def> defs_;
    const Def* cur_ = nullptr;
    size_t idx_ = 0;
    double left_ = 0, phase_ = 0, freq_ = 0;
    bool level_ = false;
    int rate_ = 44100;
    bool muted_ = false;
    std::vector<std::vector<int16_t>> fx_;       // made on first use
    struct Voice { const std::vector<int16_t>* s; size_t pos; float gain; };
    std::vector<Voice> voices_;
};

}  // namespace dof
