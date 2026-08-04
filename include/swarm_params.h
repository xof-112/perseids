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
    kEnginesPitchBoth  = 33, // 0..1 → PSP/PSW span ±1 oct … ±2 oct (not a pitch offset)

    // Block 5 — Swarm Parameters
    kSwarmSize        = 50,
    kSwarmSpread      = 51,
    kSwarmScan        = 52,
    kSwarmAtmosphere  = 53,
    kSwarmDirection   = 54, // 0 Fwd · 1 Rev · 2 Rnd (per-grain at spawn)
    kSwarmScatter     = 55, // 0 = tight on scan head … 1 = whole-loop spray
};

struct SwarmParamValues
{
    float pitch_swarm = 0.f;   // bipolar ±1 octave (4 % deadzone → 0)
    float blend       = 0.f;   // 0 = Spectra … 1 = Swarm (boot default Spectra)
    float pitch_both  = 0.f;   // 0..1 → PSP/PSW octave span 1×…2× (±100%…±200%); not a pitch offset
    float size         = 16.f;  // CountNum: concurrent grains (4…24)
    float spread       = 0.35f; // stereo width of grains 0..1
    float scan         = 0.2f;  // scrub speed; 0 = freeze
    float scatter      = 0.75f; // spawn spray around scan head 0..1
    float atmosphere   = 0.f;   // bipolar: Blur←0→Radiation
    float direction    = 0.f;   // 0 Fwd · 1 Rev · 2 Rnd
};

} // namespace perseids
