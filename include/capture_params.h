#pragma once

#include <cstddef>
#include <cstdint>

namespace perseids
{

// Shared trail pool size — UI, mixer, and CaptureEngine must agree.
inline constexpr size_t kTrailCount = 5;

// Parameter IDs for Block 1 (Trails) and Block 2 (Time) — Phase 3.
enum ParamId : uint16_t
{
    kTrailsCount     = 1,
    kTrailsThreshold = 2,
    kTrailsContRec   = 3,
    kTrailsOnOff     = 4,

    kTimeBuffer  = 10,
    kTimeHold    = 11,
    kTimeFadeIn  = 12,
    kTimeFadeOut = 13,

    // Block 11 Settings (partial — more entries arrive with Phase 11).
    kSettingsCpuMeter = 110,
    kSettingsRamMeter = 111,

    // Stub — owned by CaptureEngine, not yet on a CycleRow.
    kAudioRouting = 100,
};

// Registry-backed floats (UI thread writes; audio reads via CaptureEngine snapshot).
struct CaptureParamValues
{
    float count      = 3.f;   // 1..5 — boot default 3
    float threshold  = 0.12f; // 0..1 — lower default for line-level benches
    float cont_rec   = 0.f;   // toggle
    float on_off     = 1.f;   // toggle, default ON
    float buffer_s   = 2.f;   // seconds, max recording length (ceiling 30 s)
    float hold_s     = 15.f;  // seconds; >30 = infinite
    float fade_in_s  = 3.f;
    float fade_out_s = 3.f;
    float cpu_meter  = 1.f;   // TODO(release): default Off (0.f) — On for bench/debug
    float ram_meter  = 0.f;   // Settings: RAM/SDRAM meter On/Off (default Off)
    float routing    = 0.f;   // 0 Stereo, 1 Sidechain
};

struct TrailMixerState
{
    float level  = 0.5f;
    bool  locked = false;
    bool  solo   = false;
};

// Dashboard / Trail Level UI snapshot (no Daisy/DaisySP dependency).
struct TrailSnapshot
{
    float level; // 0..1 loudness
    bool  locked;
    bool  solo;
};

// Dashboard life-cycle bar — published by CaptureEngine for the UI thread.
enum class TrailLifePhase : uint8_t
{
    Empty = 0,
    Recording,
    FadeIn,
    Hold,
    FadeOut,
};

struct TrailLifeUi
{
    TrailLifePhase phase;
    float          fill;     // 0..1 — FadeIn fill / FadeOut remaining
    int16_t        hold_sec; // Hold: seconds left; -1 = INF
};

} // namespace perseids
