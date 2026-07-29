#pragma once

#include "filter_params.h"

#include "Filters/svf.h"

#include <cstddef>

namespace perseids
{

// Phase 8 — pre-fader SVF Filter Mix (ARCHITECTURE 4.1 Block 10).
// Destination selects which stage is filtered in place. Default mode = LP
// (CycleRow has no Mode param; BP/HP remain available on the Svf if needed later).
class FilterEngine
{
  public:
    void Init(float sample_rate);

    void SyncFromUi(const FilterParamValues& params);

    int Destination() const;

    // True when Cutoff/Res/Feedback leave the LP effectively open (skip DSP).
    bool IsBypassed() const;

    // Filters stereo (or dual-mono) buffers in place.
    void Process(float* io_l, float* io_r, size_t size);

    // Convenience: filter a mono buffer (Spectra) into both channels' state.
    void ProcessMono(float* io, size_t size);

  private:
    float CutoffHz() const;

    float sample_rate_;
    float sample_rate_inv_;

    FilterParamValues params_;

    daisysp::Svf svf_l_;
    daisysp::Svf svf_r_;

    float prev_l_;
    float prev_r_;
};

} // namespace perseids
