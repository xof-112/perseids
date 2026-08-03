#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace perseids
{

// Block 11 Audio Routing (ARCHITECTURE 4.1) — Stereo default; Sidechain in Phase 11.
enum class AudioRoutingMode : uint8_t
{
    Stereo    = 0, // In L / In R into capture (mono-cable friendly)
    Sidechain = 1, // capture In R only; In L stays dry (bypass buffers)
};

// Swappable signal source for buffer recording — never hard-wire "In L + In R".
class RecordSource
{
  public:
    void SetMode(AudioRoutingMode mode) { mode_ = mode; }
    AudioRoutingMode Mode() const { return mode_; }

    // Which jacks are wired is a *routing* decision, so it is made once per
    // block from a slow envelope with hysteresis, and the resulting weights
    // are slewed. Deciding it per sample against a fixed -80 dBFS threshold
    // flipped the mono gain between 1.0 and 0.5 at audio rate whenever the
    // unpatched channel's noise floor straddled that threshold — broadband
    // grit, proportional to the signal, absent while the input was silent.
    void UpdateBlock(const float* in_l, const float* in_r, size_t size)
    {
        float pl = 0.f;
        float pr = 0.f;
        for(size_t i = 0; i < size; ++i)
        {
            const float al = in_l[i] >= 0.f ? in_l[i] : -in_l[i];
            const float ar = in_r[i] >= 0.f ? in_r[i] : -in_r[i];
            if(al > pl)
                pl = al;
            if(ar > pr)
                pr = ar;
        }

        // Instant attack, ~0.5 s release at 256-sample blocks.
        constexpr float kRelease = 0.98f;
        env_l_ = pl > env_l_ ? pl : env_l_ * kRelease;
        env_r_ = pr > env_r_ ? pr : env_r_ * kRelease;

        // Wide hysteresis band so a noise floor can never toggle presence.
        constexpr float kOn  = 1e-3f;   // ≈ -60 dBFS
        constexpr float kOff = 2.5e-4f; // ≈ -72 dBFS
        present_l_ = present_l_ ? (env_l_ > kOff) : (env_l_ > kOn);
        present_r_ = present_r_ ? (env_r_ > kOff) : (env_r_ > kOn);

        constexpr float kSlew = 0.02f; // ≈ 270 ms across blocks
        gain_l_ += kSlew * ((present_l_ ? 1.f : 0.f) - gain_l_);
        gain_r_ += kSlew * ((present_r_ ? 1.f : 0.f) - gain_r_);

        const float d = gain_l_ + gain_r_;
        if(d > 1e-3f)
        {
            w_l_ = gain_l_ / d;
            w_r_ = gain_r_ / d;
        }
        else
        {
            w_l_ = 0.5f;
            w_r_ = 0.5f;
        }
    }

    // Mono sample that feeds threshold detection and SDRAM write.
    // Normalized weights: one jack wired keeps full level, both wired sum at
    // -6 dB, and the weights only move at block rate.
    float CaptureSample(float in_l, float in_r) const
    {
        if(mode_ == AudioRoutingMode::Sidechain)
            return in_r;
        return in_l * w_l_ + in_r * w_r_;
    }

    // Listen-through / dry path mixed to the output.
    // Stereo: monitor the capture source. Sidechain: In L stays dry.
    float DryMonitorSample(float in_l, float in_r) const
    {
        if(mode_ == AudioRoutingMode::Sidechain)
            return in_l;
        return CaptureSample(in_l, in_r);
    }

  private:
    AudioRoutingMode mode_ = AudioRoutingMode::Stereo;

    float env_l_     = 0.f;
    float env_r_     = 0.f;
    bool  present_l_ = false;
    bool  present_r_ = false;
    float gain_l_    = 0.f;
    float gain_r_    = 0.f;
    float w_l_       = 0.5f;
    float w_r_       = 0.5f;
};

} // namespace perseids
