#pragma once

#include <cstdint>

namespace perseids
{

// Block 8 — Pan Drift / Block 9 — Crossfade (Phase 9).
enum SpatialParamId : uint16_t
{
    kPanPhase     = 80,
    kPanAmplitude = 81,
    kPanVelocity  = 82,

    kXfadeAmplitude = 90,
    kXfadeVelocity  = 91,
};

struct SpatialParamValues
{
    // Block 8 — Pan Drift
    float pan_phase     = 0.f;  // 0 = sync LFOs, 1 = maximally offset
    float pan_amplitude = 0.3f; // excursion 0…1
    float pan_velocity  = 0.f;  // bipolar rate (sign = direction)

    // Block 9 — Crossfade focus wave
    float xfade_amplitude = 0.f; // 0 = flat, 1 = only focused Trail
    float xfade_velocity  = 0.f; // bipolar travel (4% deadzone in UI)
};

} // namespace perseids
