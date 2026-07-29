#pragma once

#include <cstdint>

namespace perseids
{

// Bench scaffolding — dummy parameters for Blocks 8–9 until Phase 9
// (Pan Drift / Crossfade). Blocks 6+10 moved to reverb_params.h /
// filter_params.h (Phase 8). Block 7 Resonator: reso_params.h.
enum DummyBlockParamId : uint16_t
{
    // Block 8 — Pan Drift
    kPanPhase     = 80,
    kPanAmplitude = 81,
    kPanVelocity  = 82,

    // Block 9 — Crossfade
    kXfadeAmplitude = 90,
    kXfadeVelocity  = 91,
};

struct DummyBlockParamValues
{
    // Block 8 — Pan Drift
    float pan_phase     = 0.f;
    float pan_amplitude = 0.3f;
    float pan_velocity  = 0.f; // bipolar

    // Block 9 — Crossfade
    float xfade_amplitude = 0.f;
    float xfade_velocity  = 0.f; // bipolar
};

} // namespace perseids
