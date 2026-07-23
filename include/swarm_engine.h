#pragma once

#include "capture_engine.h"
#include "swarm_params.h"

#include <cstddef>
#include <cstdint>

namespace perseids
{

// Phase 5 — granular Swarm on Trail SDRAM buffers.
// Audio callback only for Process; reads CaptureEngine::SwarmViews() filled
// in the same callback after Capture::Process (no atomics needed).
class SwarmEngine
{
  public:
    static constexpr size_t kMaxGrains = 16;
    static constexpr size_t kTrailCount = CaptureEngine::kTrailCount;

    void Init(float sample_rate);

    void SyncFromUi(const SwarmParamValues& params);

    // Audio thread — grain cloud stereo out.
    void Process(float* out_l, float* out_r, size_t size);

  private:
    struct Grain
    {
        bool   active;
        size_t trail;
        float  pos;
        float  incr;
        float  age;
        float  age_inc;
        float  pan_l;
        float  pan_r;
        float  amp;
    };

    float PitchRatio() const;
    float GrainDurationSec() const;
    float WindowEnv(float age01, float blur) const;
    float ReadInterp(size_t trail, float pos, size_t length) const;
    void  SpawnGrain(size_t trail, size_t length, float gain);
    float NextRand();

    float sample_rate_;
    float sample_rate_inv_;

    SwarmParamValues params_;

    Grain  grains_[kMaxGrains];
    float  scan_pos_[kTrailCount];
    float  spawn_phase_;
    uint32_t rng_;

    // Radiation: output BBD-style slew + lo-fi hold.
    float slew_l_;
    float slew_r_;
    float hold_l_;
    float hold_r_;
    int   hold_left_;
};

} // namespace perseids
