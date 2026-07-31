#pragma once

#include <cstdint>

namespace perseids
{

// Block 10 — Filter Mix (Phase 8). IDs 120–123; Settings extras use 130+.
enum FilterParamId : uint16_t
{
    kFilterCutoff      = 120,
    kFilterResonance   = 121,
    kFilterFeedback    = 122,
    kFilterDestination = 123,
};

// Destination CountNum 1..5 — Off first, then which wet-chain stage the SVF taps.
// "Input" = engine-sum bus (pre-reverb), NOT listen-through / Multi dry.
enum FilterDestination : int
{
    kFilterDestOff     = 1,
    kFilterDestInput   = 2, // engine bus (Spectra+Swarm sum)
    kFilterDestSpectra = 3,
    kFilterDestSwarm   = 4,
    kFilterDestReverb  = 5,
};

struct FilterParamValues
{
    float cutoff      = 0.7f; // 0..1 → exponential Hz
    float resonance   = 0.2f; // 0..1 → Svf SetRes
    float feedback    = 0.f;  // 0..1 audio-rate cutoff drive
    float destination = 1.f;  // 1..5 Off / Inp / Sp / Sw / Rv — boot Off
};

} // namespace perseids
