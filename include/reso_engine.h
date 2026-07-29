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
    void  UpdateTuning();
    float RootHz() const;

    float sample_rate_;
    float sample_rate_inv_;

    ResoParamValues params_;
    float           scale_;
    float           intonation_;

    daisysp::Svf  modes_[kNumModes];
    float         freq_hz_[kNumModes];
    float         q_;
};

} // namespace perseids
