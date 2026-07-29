#pragma once

#include <cstdint>

namespace perseids
{

// Block 7 — Spectral Resonator (Phase 7).
enum ResoParamId : uint16_t
{
    kResoMix       = 70,
    kResoDecay     = 71,
    kResoPitch     = 72,
    kResoQuantized = 73,
};

struct ResoParamValues
{
    float mix       = 0.25f; // 0..1 dry/wet on Swarm → Resonator
    float decay     = 0.5f;  // 0..1 → resonator Q / ring time
    float pitch     = 0.f;   // bipolar ±1 octave on the bank root
    float quantized = 0.f;   // toggle — force pitches onto Settings scale
};

} // namespace perseids
