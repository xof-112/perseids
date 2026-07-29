#pragma once

#include <cstdint>

namespace perseids
{

// Block 6 — Reverb (Phase 8).
enum ReverbParamId : uint16_t
{
    kReverbMix       = 60,
    kReverbDecay     = 61,
    kReverbDamping   = 62,
    kReverbCharacter = 63,
};

struct ReverbParamValues
{
    float mix       = 0.25f; // 0..1 equal-power dry/wet of reverb return
    float decay     = 0.5f;  // 0..1 → ReverbSc feedback
    float damping   = 0.5f;  // 0..1 → damp LP (0 bright … 1 dark)
    float character = 0.f;   // bipolar Chorus←0→Friction (4% deadzone in UI)
};

} // namespace perseids
