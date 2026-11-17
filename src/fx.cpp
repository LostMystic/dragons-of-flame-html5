// The new sound effects (swords, staves, axes, missiles, a hero hit, Turn Undead): small synthesized sounds, made when first needed at the audio rate.
// They are built from noise through a sweeping band-pass (swishes), decaying sines (thumps and wood), clusters of inharmonic partials (metal) and short noise clicks.
#include <algorithm>
#include <cmath>

#include "sound.h"

namespace dof {
namespace {

struct Syn {
    int R;
    std::vector<float> b;
    uint32_t seed = 12345;
    explicit Syn(int rate, double seconds) : R(rate), b((size_t)(rate * seconds), 0.f) {}
    float noise() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xffff) / 32768.f - 1.f; }
    size_t at(double t) const { return (size_t)std::max(0.0, t * R); }

    // noise through a band-pass sweeping f0 -> f1; envelope: sine hump (attack = fraction of the length), optional amplitude modulation (Hz)
    void swish(double t0, double dur, double f0, double f1, double amp, double q = 0.4, double attack = 0.4, double am = 0) {
        size_t s = at(t0), n = (size_t)(dur * R);
        float low = 0, band = 0;
        for (size_t i = 0; i < n && s + i < b.size(); i++) {
            double p = (double)i / n, fc = f0 + (f1 - f0) * p;
            float f = 2.f * std::sin(3.14159265f * (float)fc / R), in = noise();
            low += f * band; float high = in - low - (float)q * band; band += f * high;
            double env = p < attack ? std::sin(1.5707963 * p / attack) : std::cos(1.5707963 * (p - attack) / (1 - attack));
            double mod = am > 0 ? 0.6 + 0.4 * std::sin(6.2831853 * am * i / R) : 1.0;
            b[s + i] += (float)(band * env * mod * amp * 2.2);
        }
    }
    // low-passed noise burst with exponential decay (rate per second)
    void burst(double t0, double dur, double cut, double amp, double decay, bool highpass = false) {
        size_t s = at(t0), n = (size_t)(dur * R);
        float lp = 0, a = 1.f - std::exp(-6.2831853f * (float)cut / R);
        for (size_t i = 0; i < n && s + i < b.size(); i++) {
            float in = noise(); lp += a * (in - lp);
            float v = highpass ? in - lp : lp;
            b[s + i] += v * (float)(amp * std::exp(-decay * i / R) * 1.5);
        }
    }
    // sine sweeping f0 -> f1 with exponential decay
    void sine(double t0, double dur, double f0, double f1, double amp, double decay, double tremolo = 0) {
        size_t s = at(t0), n = (size_t)(dur * R);
        double ph = 0;
        for (size_t i = 0; i < n && s + i < b.size(); i++) {
            double p = (double)i / n, f = f0 + (f1 - f0) * p;
            ph += 6.2831853 * f / R;
            double env = std::exp(-decay * i / R) * std::min(1.0, i / (0.003 * R));
            if (tremolo > 0) env *= 0.75 + 0.25 * std::sin(6.2831853 * tremolo * i / R);
            b[s + i] += (float)(std::sin(ph) * amp * env);
        }
    }
    // a swelling tone: rises over `attack` of its length, then falls away (a brass or voice-like swell); harmonics are added by the caller
    void swell(double t0, double dur, double f0, double f1, double amp, double attack = 0.45, double vib = 0) {
        size_t s = at(t0), n = (size_t)(dur * R);
        double ph = 0;
        for (size_t i = 0; i < n && s + i < b.size(); i++) {
            double p = (double)i / n, f = f0 + (f1 - f0) * p;
            if (vib > 0) f *= 1.0 + 0.006 * std::sin(6.2831853 * vib * i / R);
            ph += 6.2831853 * f / R;
            double env = p < attack ? std::sin(1.5707963 * p / attack) : std::cos(1.5707963 * (p - attack) / (1 - attack) * 0.5);
            b[s + i] += (float)(std::sin(ph) * amp * env * env);
        }
    }
    void partials(double t0, double dur, std::initializer_list<double> fs, double amp, double decay) { for (double f : fs) sine(t0, dur, f, f * 0.998, amp, decay); }
    std::vector<int16_t> done(double peak) {
        float m = 1e-6f;
        for (float v : b) m = std::max(m, std::fabs(v));
        std::vector<int16_t> o(b.size());
        for (size_t i = 0; i < b.size(); i++) o[i] = (int16_t)std::lround(std::clamp((float)(b[i] / m * peak), -1.f, 1.f) * 32000.f);
        return o;
    }
};

}  // namespace

std::vector<int16_t> make_fx(int id, int R) {
    switch (id) {
        // --- melee: whiff (nothing there), hit (thwack), glance (in reach but it skidded off)
        case kFxWhiffSword: { Syn s(R, 0.25); s.swish(0, 0.22, 900, 2800, 1, 0.35, 0.45); return s.done(0.40); }
        case kFxHitSword:   { Syn s(R, 0.2); s.burst(0, 0.12, 3500, 0.8, 30); s.sine(0, 0.15, 150, 60, 0.7, 22); s.swish(0, 0.07, 2600, 1800, 0.5, 0.5, 0.1); return s.done(0.8); }
        case kFxGlanceSword:{ Syn s(R, 0.6); s.partials(0, 0.55, {2100, 3170, 4400, 5900}, 0.25, 8); s.burst(0, 0.02, 6000, 0.5, 120, true); return s.done(0.55); }
        case kFxWhiffStaff: { Syn s(R, 0.34); s.swish(0, 0.30, 300, 750, 1, 0.45, 0.5); s.sine(0, 0.3, 140, 190, 0.07, 0.8, 0); return s.done(0.42); }
        case kFxHitStaff:   { Syn s(R, 0.2); s.sine(0, 0.12, 260, 130, 0.8, 26); s.burst(0, 0.03, 2200, 0.7, 90); s.sine(0, 0.07, 520, 300, 0.3, 40); return s.done(0.8); }
        case kFxGlanceStaff:{ Syn s(R, 0.2); s.sine(0, 0.15, 640, 630, 0.6, 38); s.sine(0, 0.1, 980, 975, 0.4, 50); s.burst(0, 0.012, 5000, 0.4, 150, true); return s.done(0.55); }
        case kFxWhiffAxe:   { Syn s(R, 0.4); s.swish(0, 0.34, 200, 560, 1, 0.5, 0.5); s.burst(0.02, 0.3, 300, 0.12, 5); return s.done(0.5); }
        case kFxHitAxe:     { Syn s(R, 0.3); s.burst(0, 0.18, 1800, 0.9, 15); s.sine(0, 0.22, 110, 45, 0.9, 13); s.burst(0, 0.02, 4000, 0.6, 120, true); return s.done(0.9); }
        case kFxGlanceAxe:  { Syn s(R, 0.55); s.partials(0, 0.5, {880, 1350, 2250, 3100}, 0.25, 10); s.sine(0, 0.1, 180, 120, 0.2, 30); s.burst(0, 0.02, 5000, 0.4, 130, true); return s.done(0.55); }
        // --- missiles: [arrow, spear, axe, sling] x [thrown, hits, lands (soft)]
        case kFxMissile + 0: { Syn s(R, 0.3); s.sine(0, 0.25, 200, 150, 0.6, 14); s.sine(0, 0.25, 400, 300, 0.2, 18); s.swish(0.02, 0.2, 2200, 3300, 0.3, 0.3, 0.2); return s.done(0.5); }
        case kFxMissile + 1: { Syn s(R, 0.15); s.sine(0, 0.1, 180, 90, 0.7, 34); s.burst(0, 0.05, 2500, 0.5, 60); s.burst(0, 0.01, 6000, 0.3, 200, true); return s.done(0.7); }
        case kFxMissile + 2: { Syn s(R, 0.15); s.sine(0, 0.1, 110, 70, 0.25, 30); s.burst(0, 0.08, 1200, 0.2, 35); return s.done(0.22); }
        case kFxMissile + 3: { Syn s(R, 0.35); s.swish(0, 0.3, 400, 950, 1, 0.4, 0.35); return s.done(0.45); }
        case kFxMissile + 4: { Syn s(R, 0.2); s.sine(0, 0.15, 130, 60, 0.8, 22); s.burst(0, 0.07, 2000, 0.5, 40); return s.done(0.8); }
        case kFxMissile + 5: { Syn s(R, 0.2); s.sine(0, 0.12, 90, 55, 0.3, 24); s.sine(0.02, 0.08, 400, 380, 0.08, 40); s.burst(0, 0.05, 1000, 0.15, 40); return s.done(0.24); }
        case kFxMissile + 6: { Syn s(R, 0.45); s.swish(0, 0.4, 250, 480, 1, 0.45, 0.5, 18); return s.done(0.5); }
        case kFxMissile + 7: { Syn s(R, 0.25); s.burst(0, 0.14, 1800, 0.9, 18); s.sine(0, 0.18, 120, 50, 0.8, 16); s.burst(0, 0.015, 4000, 0.5, 120, true); return s.done(0.85); }
        case kFxMissile + 8: { Syn s(R, 0.3); s.partials(0, 0.25, {700, 1100}, 0.12, 28); s.sine(0, 0.12, 80, 55, 0.25, 25); return s.done(0.22); }
        case kFxMissile + 9: { Syn s(R, 0.3); s.swish(0, 0.24, 600, 1300, 0.6, 0.35, 0.5, 30); s.burst(0.23, 0.04, 5000, 0.8, 80, true); return s.done(0.42); }
        case kFxMissile + 10:{ Syn s(R, 0.15); s.burst(0, 0.04, 4500, 0.9, 70, true); s.burst(0, 0.05, 2500, 0.4, 70); s.sine(0, 0.07, 220, 100, 0.6, 40); return s.done(0.8); }
        case kFxMissile + 11:{ Syn s(R, 0.25); s.sine(0, 0.04, 600, 580, 0.1, 90); s.sine(0.07, 0.04, 520, 500, 0.07, 90); s.burst(0, 0.1, 1500, 0.08, 40); return s.done(0.16); }
        // --- a hero is hit
        case kFxPcHit: { Syn s(R, 0.3); s.sine(0, 0.22, 210, 70, 0.8, 14); s.burst(0, 0.15, 1500, 0.5, 20); s.sine(0.01, 0.1, 340, 150, 0.3, 24); return s.done(0.75); }
        // --- Turn Undead: a rising shimmer of bells
        case kFxTurn: {
            Syn s(R, 1.1);
            s.sine(0, 0.7, 400, 1700, 0.18, 2.2);
            for (double f : {659.0, 880.0, 1319.0, 1760.0}) s.sine(0.04 * (f > 800), 0.95, f, f, 0.22, 3.2, 7);
            s.burst(0, 0.8, 7000, 0.05, 3, true);
            return s.done(0.55);
        }
        // --- Wyrmslayer: a sword of power; the same three blows, with a ringing of magic in each
        case kFxWyrmWhiff:  { Syn s(R, 0.4); s.swish(0, 0.26, 700, 3000, 1, 0.3, 0.45); s.sine(0.02, 0.35, 1760, 2090, 0.14, 5, 9); s.sine(0.02, 0.35, 2640, 3000, 0.07, 5, 9); return s.done(0.45); }
        case kFxWyrmHit:    { Syn s(R, 0.5); s.burst(0, 0.12, 3800, 0.8, 28); s.sine(0, 0.2, 130, 50, 0.85, 14); s.partials(0, 0.45, {1320, 1980, 2970}, 0.12, 9); s.swish(0, 0.07, 2800, 2000, 0.5, 0.5, 0.1); return s.done(0.85); }
        case kFxWyrmGlance: { Syn s(R, 0.9); s.partials(0, 0.85, {1500, 2250, 3400, 5100}, 0.22, 5); s.sine(0.0, 0.8, 750, 745, 0.15, 5, 6); s.burst(0, 0.02, 6000, 0.5, 120, true); return s.done(0.55); }
        // --- Raistlin's spells, each its own
        case kFxMagicMissile: {   // three quick zaps
            Syn s(R, 0.5);
            for (int k = 0; k < 3; k++) { double t = 0.08 * k; s.sine(t, 0.14, 1500 + 300 * k, 3200 + 400 * k, 0.5, 18); s.swish(t, 0.1, 3000, 5000, 0.25, 0.3, 0.2); }
            s.burst(0.28, 0.1, 6000, 0.1, 25, true);
            return s.done(0.5);
        }
        case kFxBurningHands: { Syn s(R, 0.8); s.swish(0, 0.6, 250, 900, 0.8, 0.5, 0.35); s.burst(0, 0.7, 700, 0.5, 4); for (int k = 0; k < 14; k++) s.burst(0.04 * k + 0.02 * (k % 3), 0.012, 5000, 0.5, 200, true); return s.done(0.55); }
        case kFxFireball:     { Syn s(R, 0.6); s.swish(0, 0.5, 180, 1100, 1, 0.45, 0.6); s.sine(0, 0.5, 90, 160, 0.25, 1.5, 22); s.burst(0.1, 0.4, 900, 0.3, 3); return s.done(0.55); }
        case kFxFireballBoom: { Syn s(R, 1.1); s.burst(0, 0.9, 1300, 1, 4.5); s.sine(0, 0.9, 80, 28, 1, 4); s.burst(0, 0.05, 5000, 0.7, 60, true); return s.done(0.95); }
        case kFxFinalStrike:  {   // a rising roar, then everything at once
            Syn s(R, 3.2);
            s.swish(0, 0.9, 120, 1600, 0.7, 0.5, 0.95); s.sine(0, 0.9, 60, 240, 0.3, 0, 12);
            s.burst(0.85, 2.2, 1000, 1, 1.6); s.sine(0.85, 2.3, 55, 20, 1, 1.5); s.burst(0.85, 0.2, 6000, 0.6, 12, true);
            s.partials(0.9, 2.0, {220, 330, 659}, 0.08, 1.8);
            return s.done(1.0);
        }
        case kFxCharm:  { Syn s(R, 0.8); int k = 0; for (double f : {784.0, 988.0, 1175.0, 1568.0}) { s.sine(0.07 * k, 0.6, f, f, 0.3, 6); s.sine(0.07 * k, 0.5, f * 2, f * 2, 0.08, 8); k++; } return s.done(0.5); }
        case kFxSleep:  { Syn s(R, 1.0); s.sine(0, 0.9, 880, 590, 0.35, 2.5, 4); s.sine(0.1, 0.8, 660, 440, 0.25, 2.5, 4); s.burst(0, 0.9, 600, 0.12, 2); return s.done(0.4); }
        case kFxWeb:    { Syn s(R, 0.6); s.swish(0, 0.14, 800, 2600, 1, 0.3, 0.2); s.sine(0.08, 0.4, 320, 200, 0.2, 5, 14); for (int k = 0; k < 6; k++) s.burst(0.15 + 0.05 * k, 0.015, 3500, 0.4, 150, true); return s.done(0.5); }
        case kFxDetectMagic:    { Syn s(R, 1.0); s.partials(0, 0.9, {2093, 3136, 4186}, 0.2, 4); s.sine(0, 0.9, 1568, 1568, 0.15, 3, 8); return s.done(0.4); }
        case kFxDetectInvisible:{ Syn s(R, 0.9); s.swish(0, 0.8, 1500, 700, 0.8, 0.8, 0.5); s.sine(0, 0.8, 440, 430, 0.12, 1.5, 5); return s.done(0.35); }
        case kFxBuzz: { Syn s(R, 0.7); for (int k = 1; k <= 6; k++) s.sine(0, 0.65, 118.0 * k, 118.0 * k, 0.3 / k, 0.7, 31); s.swish(0, 0.6, 700, 900, 0.12, 0.7, 0.5, 31); return s.done(0.4); }   // Wyrmslayer hums near a dragon
        case kFxLightning: { Syn s(R, 1.4); s.burst(0, 0.03, 7000, 1, 60, true); s.swish(0, 0.12, 6000, 900, 1, 0.3, 0.05); s.sine(0.02, 0.3, 700, 90, 0.7, 9); s.burst(0.05, 1.2, 500, 0.7, 2.6); for (int k = 0; k < 5; k++) s.burst(0.06 * k, 0.02, 5000, 0.4, 120, true); return s.done(0.95); }
        case kFxSlab: { Syn s(R, 1.0); s.burst(0, 0.5, 700, 1, 7); s.sine(0, 0.5, 70, 28, 1, 8); s.burst(0, 0.04, 5000, 0.8, 70, true); s.burst(0.05, 0.4, 3000, 0.15, 10, true); s.partials(0.02, 0.3, {180, 270}, 0.1, 14); return s.done(0.95); }
        case kFxFanfare: {   // triumphant horns: a rising call of brass notes (harmonics 1-6, a little attack), the last held, with a soft echo
            Syn s(R, 3.3);
            auto horn = [&](double t0, double dur, double f, double amp) {
                static const double h[6] = {1.0, 0.7, 0.5, 0.35, 0.22, 0.12};
                for (int k = 0; k < 6; k++) {
                    s.sine(t0, dur, f * (k + 1), f * (k + 1), amp * h[k], 1.4 + 0.6 * k, k == 0 ? 0 : 5.5);
                    s.sine(t0, dur, f * (k + 1) * 1.004, f * (k + 1) * 1.004, amp * h[k] * 0.6, 1.4 + 0.6 * k);   // two horns, a little apart
                }
            };
            horn(0.00, 0.40, 392.0, 0.30);   // G4
            horn(0.30, 0.40, 523.3, 0.30);   // C5
            horn(0.60, 0.40, 659.3, 0.30);   // E5
            horn(0.95, 2.20, 784.0, 0.34);   // G5, held
            horn(0.95, 2.20, 523.3, 0.22);   // with the C below it
            horn(0.95, 2.20, 392.0, 0.20);
            s.burst(0.0, 0.03, 4000, 0.15, 60, true);
            return s.done(0.7);
        }
        case kFxCrumble: { Syn s(R, 1.3); s.burst(0, 1.1, 2200, 0.5, 2.5, true); for (int k = 0; k < 22; k++) s.burst(0.04 * k + 0.01 * (k % 4), 0.015, 3500, 0.35, 160, true); s.sine(0.05, 0.5, 120, 60, 0.3, 6); return s.done(0.5); }
        case kFxAcid:    { Syn s(R, 1.6); s.burst(0, 1.4, 5500, 0.3, 1.8, true); for (int k = 0; k < 16; k++) { double t = 0.05 + 0.08 * k + 0.02 * (k % 3); double f = 280 + 55 * ((k * 7) % 11); s.sine(t, 0.07, f, f * 1.8, 0.35, 28); } s.swish(0, 0.5, 1200, 400, 0.5, 0.6, 0.3); return s.done(0.5); }
        case kFxBoneBurst: { Syn s(R, 1.2); s.burst(0, 0.8, 1500, 1, 6); s.sine(0, 0.6, 95, 35, 0.9, 6); s.burst(0, 0.03, 5000, 0.7, 80, true); for (int k = 0; k < 14; k++) { double t = 0.08 + 0.035 * k; s.sine(t, 0.04, 1800 + 150 * ((k * 5) % 7), 1200, 0.3, 60); s.burst(t, 0.01, 6000, 0.25, 200, true); } return s.done(0.9); }
        // --- Goldmoon's spells
        case kFxCure:       { Syn s(R, 1.0); int k = 0; for (double f : {523.0, 659.0, 784.0, 1047.0}) { s.sine(0.06 * k, 0.8, f, f, 0.3, 3.5); k++; } s.burst(0, 0.5, 6000, 0.03, 4, true); return s.done(0.5); }
        case kFxProtection: { Syn s(R, 1.2); for (double f : {220.0, 330.0, 440.0}) s.sine(0, 1.1, f, f, 0.25, 1.5, 3); s.partials(0.05, 1.0, {1760, 2640}, 0.1, 4); return s.done(0.5); }
        case kFxFindTraps:  { Syn s(R, 0.8); s.sine(0, 0.5, 1500, 1480, 0.5, 7); s.sine(0.35, 0.4, 1500, 1480, 0.3, 8); s.burst(0, 0.01, 4000, 0.3, 200, true); return s.done(0.5); }
        case kFxHold:       { Syn s(R, 0.7); s.sine(0, 0.5, 320, 85, 0.6, 4); s.burst(0, 0.6, 7000, 0.12, 5, true); s.partials(0, 0.4, {1900, 2400}, 0.1, 9); return s.done(0.5); }
        case kFxMenuSelect: { Syn s(R, 0.25); s.burst(0, 0.012, 5000, 0.9, 260, true); s.sine(0, 0.12, 190, 120, 0.7, 40); s.partials(0.004, 0.2, {1320, 1980, 2640}, 0.22, 24); return s.done(0.5); }   // a wooden knock with a small bell-like tick, like a latch or a die set down on a table
        case kFxMenuBack:   { Syn s(R, 0.32); s.swish(0, 0.28, 2400, 350, 0.55, 0.5, 0.12); s.sine(0.16, 0.1, 150, 90, 0.35, 30); return s.done(0.42); }   // a soft whoosh falling away, then a muffled thud
        case kFxHammer:     { Syn s(R, 1.2); s.swish(0, 0.25, 300, 1400, 0.6, 0.4, 0.8); s.partials(0.22, 1.0, {330, 495, 742}, 0.25, 3.5); s.sine(0.22, 0.4, 110, 45, 0.9, 9); s.burst(0.22, 0.06, 3000, 0.8, 50); return s.done(0.85); }
        case kFxPrayer:     { Syn s(R, 1.6); int k = 0; for (double f : {392.0, 494.0, 587.0, 784.0}) { s.sine(0.12 * k, 1.4, f, f, 0.22, 1.6, 4.5); k++; } return s.done(0.45); }
        case kFxBless:      { Syn s(R, 0.9); int k = 0; for (double f : {880.0, 1108.0, 1319.0, 1760.0, 2217.0}) { s.sine(0.05 * k, 0.5, f, f, 0.25, 7); k++; } return s.done(0.45); }
        case kFxDispel:     { Syn s(R, 0.7); s.swish(0, 0.45, 3200, 200, 1, 0.3, 0.15); s.burst(0.4, 0.15, 4000, 0.6, 25); s.sine(0.4, 0.25, 180, 60, 0.6, 12); return s.done(0.55); }
        // --- the sting when a wandering group comes at the party in a dungeon
        case kFxStingArmy: {   // the Dragon Army: a low war horn on a tritone (brass harmonics), drums beating closer, a clank of mail
            Syn s(R, 3.0);
            static const double h[6] = {1.0, 0.8, 0.55, 0.4, 0.25, 0.14};
            for (double f : {82.4, 116.5}) for (int k = 0; k < 6; k++) { s.swell(0.05, 2.4, f * (k + 1), f * (k + 1) * 0.985, 0.22 * h[k], 0.38, 5.2); s.swell(0.05, 2.4, f * (k + 1) * 1.006, f * (k + 1) * 0.99, 0.12 * h[k], 0.38, 4.6); }
            for (int k = 0; k < 4; k++) { double t = 0.1 + 0.42 * k - 0.02 * k * k; double a = 0.6 + 0.12 * k; s.sine(t, 0.5, 105, 48, a, 7); s.burst(t, 0.06, 900, a * 0.7, 40); s.sine(t + 0.01, 0.2, 160, 90, a * 0.3, 14); }
            s.partials(1.5, 0.8, {1650, 2480, 3710, 5100}, 0.10, 9); s.burst(1.5, 0.05, 6000, 0.25, 90, true);
            s.burst(0.0, 1.8, 400, 0.12, 1.2);
            return s.done(0.75);
        }
        case kFxStingUndead: {   // the dead: a hollow drone on a beating minor second, a wail that slides down, a cold breath and one deep knock
            Syn s(R, 3.2);
            for (double f : {110.0, 116.5, 164.8}) { s.swell(0.0, 2.9, f, f * 0.97, 0.26, 0.4, 3.5); s.swell(0.0, 2.9, f * 2.0, f * 1.94, 0.08, 0.4, 3.0); }
            s.swell(0.35, 2.0, 880, 520, 0.14, 0.3, 5.5); s.swell(0.35, 2.0, 1318, 780, 0.06, 0.3, 5.0);
            s.swish(0.2, 2.3, 2200, 500, 0.55, 0.8, 0.35, 0.8);
            s.sine(0.0, 0.7, 62, 30, 0.9, 5); s.burst(0.0, 0.05, 500, 0.5, 50);
            s.sine(0.75, 0.6, 58, 28, 0.6, 6);
            return s.done(0.7);
        }
        default: return {};
    }
}

}  // namespace dof
