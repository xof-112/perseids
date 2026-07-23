#pragma once

#include <cstdint>

namespace perseids
{

// Parameter IDs — Block 3 (Engines, partial) + Block 4 (Spectra).
enum SpectraParamId : uint16_t
{
    // Block 3 — Pitch Spectra only for Phase 4; Blend/Pitch Swarm arrive in Phase 5/6.
    kEnginesPitchSpectra = 30,

    // Block 4 — Spectra Parameters
    kSpectraPartials  = 40,
    kSpectraWaveshape = 41,
    kSpectraUmbraAurora = 42,
    kSpectraEnsemble  = 43,
};

struct SpectraParamValues
{
    float pitch_spectra = 0.f;  // bipolar ±1 octave (4 % deadzone → 0)
    float partials      = 16.f; // 4..32 (audio CPU cap; UI max matches)
    float waveshape     = 0.f;  // bipolar: −1 Sine←center, +1 →Fold via Saw
    float umbra_aurora  = 0.f;  // bipolar: Umbra←0→Aurora
    float ensemble      = 0.f;  // unipolar 0..1 — slew + odd/even detune
};

} // namespace perseids
