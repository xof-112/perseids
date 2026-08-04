#pragma once

#include "capture_engine.h"
#include "swarm_params.h"

#include <atomic>
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
    static constexpr size_t kMaxGrains = 24;
    // Load-governor floor — below this the cloud thins out audibly.
    static constexpr size_t kMinGrains = 6;
    // Size CountNum range (UI) — governor may hold the live cap lower.
    static constexpr size_t kSizeMin = 4;
    static constexpr size_t kSizeMax = kMaxGrains;
    static constexpr size_t kTrailCount = CaptureEngine::kTrailCount;
    // Grain-envelope table. Linear interpolation over 1024 points keeps the
    // error near -110 dB, so the tabulated window is inaudible vs. the direct
    // cos/sin/pow evaluation it replaces.
    static constexpr size_t kWindowLut = 1024;

    void Init(float sample_rate);

    // Main loop — also retabulates the grain envelope when Atmosphere moved.
    void SyncFromUi(const SwarmParamValues& params);

    // Audio thread — grain cloud stereo out.
    void Process(float* out_l, float* out_r, size_t size);

    // Audio thread, block rate — thins the cloud before the callback overruns.
    void UpdateGovernor(float cpu_load);

    // UI thread — true while the governor holds the grain count below max.
    bool GovernorActive() const
    {
        return gov_active_.load(std::memory_order_relaxed);
    }

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
        // pan_* × Trail Pan Drift, renormalised. Both factors are constant for
        // the whole block, so this is refreshed block-rate, not per sample.
        float  eff_l;
        float  eff_r;
        float  amp;
    };

    float PitchRatio() const;
    float GrainDurationSec() const;
    size_t WantedGrains() const;
    float ReadInterp(size_t trail, float pos, float play_f) const;
    void  SpawnGrain(size_t trail,
                     float  play_f,
                     float  dur_n,
                     float  pitch,
                     size_t cap);
    float NextRand();
    void  BuildWindowTable(float blur);

    float sample_rate_;
    float sample_rate_inv_;

    SwarmParamValues params_;

    Grain  grains_[kMaxGrains];
    float  scan_pos_[kTrailCount];
    float  spawn_phase_;
    uint32_t rng_;

    // Double-buffered envelope table: the main loop fills the back buffer and
    // flips the index, so the audio thread never waits or retries.
    float                 window_tab_[2][kWindowLut + 1];
    std::atomic<uint32_t> win_index_;
    float                 win_blur_;
    uint32_t              win_build_ms_;

    // 1/sqrt(n) for the grain-sum normalisation — same values as the runtime
    // expression, just not recomputed 48000 times a second.
    float inv_sqrt_[kMaxGrains + 1];

    size_t            grain_cap_;
    uint32_t          gov_recover_;
    std::atomic<bool> gov_active_;

    // Radiation: output BBD-style slew + lo-fi hold.
    float slew_l_;
    float slew_r_;
    float hold_l_;
    float hold_r_;
    int   hold_left_;
};

} // namespace perseids
