#pragma once

#include <cstdint>

namespace perseids
{

// Parameter IDs — Block 3 (Engines) + Block 5 (Swarm).
enum SwarmParamId : uint16_t
{
    // Block 3 — Phase 6: continuous Blend replaced the Phase 5 A/B toggle.
    kEnginesPitchSwarm = 31,
    kEnginesBlend      = 32, // 0 = Spectra … 1 = Swarm (equal-power crossfade)

    // Block 5 — Swarm Parameters
    kSwarmSize        = 50,
    kSwarmSpread      = 51,
    kSwarmScan        = 52,
    kSwarmAtmosphere  = 53,
};

struct SwarmParamValues
{
    float pitch_swarm = 0.f;   // bipolar ±1 octave (4 % deadzone → 0)
    float blend       = 0.f;   // 0 = Spectra … 1 = Swarm (boot default Spectra)
    float size         = 0.45f; // grain length 0..1 → ~8–180 ms
    float spread       = 0.35f; // stereo width of grains 0..1
    float scan         = 0.2f;  // scrub speed; 0 = freeze
    float atmosphere   = 0.f;   // bipolar: Blur←0→Radiation
};

} // namespace perseids
