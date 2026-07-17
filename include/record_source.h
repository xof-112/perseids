#pragma once

#include <cmath>
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

    // Mono sample that feeds threshold detection and SDRAM write.
    float CaptureSample(float in_l, float in_r) const
    {
        if(mode_ == AudioRoutingMode::Sidechain)
            return in_r;

        // Stereo mix, but keep full level when only one jack is wired (typical
        // Phase-3 bench: one cable in, one out).
        const float al = in_l >= 0.f ? in_l : -in_l;
        const float ar = in_r >= 0.f ? in_r : -in_r;
        if(al < 1e-4f)
            return in_r;
        if(ar < 1e-4f)
            return in_l;
        return 0.5f * (in_l + in_r);
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
};

} // namespace perseids
