#pragma once

#include "reso_params.h"

#include "Filters/svf.h"

#include <cstddef>
#include <cstdint>

namespace perseids
{

// Phase 7 — Spectral Resonator on the Swarm output.
// Parallel bandpass bank (DaisySP Svf), tuned by Pitch / Scale / Intonation.
// Audio callback only; SyncFromUi from the main loop.
class ResonatorEngine
{
  public:
    static constexpr size_t kNumModes = 8;

    void Init(float sample_rate);

    void SyncFromUi(const ResoParamValues& params,
                    float                  scale,      // 0 Major / 1 Minor / 2 Pent
                    float                  intonation); // 0 Equal / 1 Just

    // Processes Swarm L/R in place when mix > ~0; otherwise a no-op.
    void Process(float* io_l, float* io_r, size_t size);

  private:
    // Retunes frequency and per-mode damping — calls sinf/powf per mode, so it
    // only runs when one of its inputs actually moved.
    void  UpdateTuning();
    // Mode weight × equal-power pan. Cheap, rerun whenever Spread moves.
    void  UpdateGains();
    float RootHz() const;

    float sample_rate_;
    float sample_rate_inv_;

    ResoParamValues params_;
    float           scale_;
    float           intonation_;

    daisysp::Svf modes_[kNumModes];
    float        freq_hz_[kNumModes];
    float        gain_l_[kNumModes];
    float        gain_r_[kNumModes];

    // Last inputs UpdateTuning()/UpdateGains() were run with.
    float tuned_pitch_;
    float tuned_decay_;
    float tuned_damping_;
    float tuned_quantized_;
    float tuned_scale_;
    float tuned_intonation_;
    float gained_spread_;
};

} // namespace perseids
