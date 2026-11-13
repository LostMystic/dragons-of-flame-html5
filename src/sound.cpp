#include <algorithm>

#include "sound.h"

#include "../third_party/json/json.hpp"

namespace dof {

bool Sound::load(const Assets& a) {
    using nlohmann::json;
    try {
        json j = json::parse(a.read("data/sounds.json"));
        for (auto& [id, s] : j["sounds"].items()) {
            int t = s["type"];
            Def d;
            if (t == 0 && s.contains("segments")) {  // fn 8289: pseudo-random timer reloads, one speaker flip per interrupt
                unsigned ax = 0x1357;
                for (auto& seg : s["segments"]) {
                    unsigned mask = seg[0], base = seg[1], n = seg[2];
                    for (unsigned k = 0; k < n && d.toggles.size() < 60000; k++) {
                        ax = (ax + 0x9248) & 0xffff;
                        ax = ((ax >> 3) | (ax << 13)) & 0xffff;
                        unsigned div = (ax & mask) | base;
                        if (div == 0) div = 1;
                        d.toggles.push_back({div / 1193182.0, mask != 0xffff});
                    }
                }
                defs_[std::stoi(id)] = std::move(d);
                continue;
            }
            if (t == 1 && s.contains("intervals")) {  // fn 8305: each word is the timer count to the next flip
                for (auto& w : s["intervals"]) d.toggles.push_back({((unsigned)w.get<int>() | 0x800) / 1193182.0, true});
                defs_[std::stoi(id)] = std::move(d);
                continue;
            }
            if (t != 2 && t != 3) continue;
            d.loop = s.value("loop", false);
            for (auto& n : s["notes"]) d.notes.push_back({n[0], n[1]});
            defs_[std::stoi(id)] = std::move(d);
        }
    } catch (...) {
        return false;
    }
    return !defs_.empty();
}

void Sound::play(int id) {
    auto it = defs_.find(id);
    if (it == defs_.end()) return;
    cur_ = &it->second;
    level_ = false;
    idx_ = 0;
    left_ = 0;
    phase_ = 0;
}

void Sound::stop() { cur_ = nullptr; }

void Sound::play_fx(int fx, double gain) {
    if (muted_ || fx < 0 || fx >= kFxCount) return;
    if (fx_.empty()) fx_.resize(kFxCount);
    if (fx_[fx].empty()) fx_[fx] = make_fx(fx, rate_);
    if (fx_[fx].empty()) return;
    if (voices_.size() >= 8) voices_.erase(voices_.begin());
    voices_.push_back({&fx_[fx], 0, (float)gain});
}

void Sound::render(int16_t* out, int frames) {
    for (int i = 0; i < frames; i++) {
        int16_t v = 0;
        if (cur_ && !cur_->toggles.empty()) {
            while (left_ <= 0 && cur_) {
                if (idx_ >= cur_->toggles.size()) { cur_ = nullptr; break; }
                auto tg = cur_->toggles[idx_++];
                left_ += tg.first * rate_;
                if (tg.second) level_ = !level_;
            }
            if (cur_) { if (!muted_) v = level_ ? 3000 : -3000; left_ -= 1; }
        } else if (cur_) {
            while (left_ <= 0 && cur_) {
                if (idx_ >= cur_->notes.size()) {
                    if (cur_->loop && !cur_->notes.empty()) idx_ = 0;
                    else { cur_ = nullptr; break; }
                }
                auto n = cur_->notes[idx_++];
                left_ += n.first / 18.2065 * rate_;
                freq_ = n.second ? 1193182.0 / n.second : 0.0;
            }
            if (cur_ && freq_ > 0 && !muted_) {
                phase_ += freq_ / rate_;
                if (phase_ >= 1.0) phase_ -= 1.0;
                v = phase_ < 0.5 ? 3500 : -3500;
            }
            left_ -= 1;
        }
        int mixed = v;
        for (auto& vo : voices_) if (vo.pos < vo.s->size()) mixed += (int)((*vo.s)[vo.pos++] * vo.gain);
        out[i] = (int16_t)std::clamp(mixed, -32000, 32000);
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= v.s->size(); }), voices_.end());
}

}  // namespace dof
