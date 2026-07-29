#pragma once

#include "reverb_params.h"

#include "Effects/chorus.h"
#include "Effects/reverbsc.h"

#include <cstddef>

namespace perseids
{

// Phase 8 — global pre-fader ReverbSc send (ARCHITECTURE 4.1 Block 6).
// ReverbSc (~400 KB tank) is supplied from SDRAM by the caller after hw.Init();
// this engine (incl. Chorus) stays in internal RAM — never DSY_SDRAM_BSS on the
// whole object (C++ lifetime / Chorus delay lines before SDRAM is up).
class ReverbEngine
{
  public:
    void Init(float sample_rate, daisysp::ReverbSc& verb_sc);

    void SyncFromUi(const ReverbParamValues& params);

    float Mix() const { return params_.mix; }

    // Runs the tank on the pre-fader send. Writes wet only (Character applied).
    // Mix≈0 skips (CPU) and clears Friction feedback state.
    void Process(const float* in_l,
                 const float* in_r,
                 float*       wet_l,
                 float*       wet_r,
                 size_t       size);

  private:
    float sample_rate_;

    ReverbParamValues params_;

    daisysp::ReverbSc* verb_;
    daisysp::Chorus    chorus_;

    float friction_fb_l_;
    float friction_fb_r_;
    float hold_l_; // half-rate output hold
    float hold_r_;
};

} // namespace perseids
