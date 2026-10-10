#pragma once

// Platform seam: storage format of the Trail ring buffers.
//
// Daisy firmware: float, exactly as before (64 MB SDRAM, 5 × 30 s fits).
// disting NT plug-in: 16 bit (PERSEIDS_TRAIL_INT16), because the NT gives a
// plug-in far less DRAM — 5 × 15 s are ~7 MB instead of ~14 MB as float.
//
// Shared core code reads and writes Trail samples only through TrailToFloat /
// TrailFromFloat. For float storage both are the identity, so the firmware's
// arithmetic is unchanged bit for bit.

#include <cstdint>

namespace perseids
{

#if defined(PERSEIDS_TRAIL_INT16)

using TrailSample = int16_t;

// ±2.0 full scale: one octave of headroom over the codec range, ~-90 dB floor
// for a full-level signal. The capture input is filtered and may overshoot.
inline constexpr float kTrailFullScale = 2.f;

inline float TrailToFloat(TrailSample s)
{
    return static_cast<float>(s) * (kTrailFullScale / 32767.f);
}

inline TrailSample TrailFromFloat(float x)
{
    float v = x * (32767.f / kTrailFullScale);
    if(v > 32767.f)
        v = 32767.f;
    else if(v < -32767.f)
        v = -32767.f;
    // Round to nearest without lroundf (no libm call in the write path).
    return static_cast<TrailSample>(v >= 0.f ? v + 0.5f : v - 0.5f);
}

#else

using TrailSample = float;

inline float       TrailToFloat(TrailSample s) { return s; }
inline TrailSample TrailFromFloat(float x) { return x; }

#endif

} // namespace perseids
