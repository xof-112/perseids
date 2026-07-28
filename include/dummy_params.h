#pragma once

#include <cstdint>

namespace perseids
{

// Bench scaffolding — dummy parameters for Blocks 6–10 until their engines
// land (Reverb/Filter Phase 8, Resonator Phase 7, Pan Drift/Crossfade
// Phase 9). Development Principle 5.1: UI mechanics first, on dummy values.
// Turning these pots opens a full CycleView so value up/down movement is
// visible on the display; nothing reads these values in the audio path yet.
// When an engine phase lands, move its IDs/values into the real param header
// and delete them here.
enum DummyBlockParamId : uint16_t
{
    // Block 6 — Reverb
    kReverbMix       = 60,
    kReverbDecay     = 61,
    kReverbDamping   = 62,
    kReverbCharacter = 63,

    // Block 7 — Spectral Resonator
    kResoMix       = 70,
    kResoDecay     = 71,
    kResoPitch     = 72,
    kResoQuantized = 73,

    // Block 8 — Pan Drift
    kPanPhase     = 80,
    kPanAmplitude = 81,
    kPanVelocity  = 82,

    // Block 9 — Crossfade
    kXfadeAmplitude = 90,
    kXfadeVelocity  = 91,

    // Block 10 — Filter Mix (100 is taken by kAudioRouting, 110+ by Settings)
    kFilterCutoff      = 120,
    kFilterResonance   = 121,
    kFilterFeedback    = 122,
    kFilterDestination = 123,
};

struct DummyBlockParamValues
{
    // Block 6 — Reverb
    float rev_mix       = 0.25f;
    float rev_decay     = 0.5f;
    float rev_damping   = 0.5f;
    float rev_character = 0.f; // bipolar Chorus←0→Friction

    // Block 7 — Spectral Resonator
    float reso_mix       = 0.25f;
    float reso_decay     = 0.5f;
    float reso_pitch     = 0.f; // bipolar
    float reso_quantized = 0.f; // toggle

    // Block 8 — Pan Drift
    float pan_phase     = 0.f;
    float pan_amplitude = 0.3f;
    float pan_velocity  = 0.f; // bipolar

    // Block 9 — Crossfade
    float xfade_amplitude = 0.f;
    float xfade_velocity  = 0.f; // bipolar

    // Block 10 — Filter Mix
    float flt_cutoff      = 0.7f;
    float flt_resonance   = 0.2f;
    float flt_feedback    = 0.f;
    float flt_destination = 1.f; // 1..4 = Input/Spectra/Swarm/Reverb (enum UI later)
};

} // namespace perseids
