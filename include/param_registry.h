#pragma once

#include <cstddef>
#include <cstdint>

namespace perseids
{

enum class ParamDisplayType : uint8_t
{
    Unipolar,  // 0–100 % bar from baseline to ceiling
    Bipolar,   // ±100 % bar from center, 4 % deadzone at 0
    Toggle,    // two states, side by side
    CountBar,  // numeric label + unipolar-style bar (e.g. Partials)
    CountNum,  // large number only (e.g. Trail count)
    Seconds,   // time in seconds (Buffer, Fade In/Out)
    HoldTime,  // Hold seconds; values >30 display as INF
};

struct ParameterDef
{
    uint16_t         id;
    const char*      name;
    const char*      abbrev; // 3–4 characters for segmented row
    float            min_val;
    float            max_val;
    float            default_val;
    float*           value_ptr;
    ParamDisplayType display_type;
    bool             bipolar_deadzone; // Section 2.8 — only for bipolar types
    // Subtle 50% marker dots beside a unipolar bar (crossfade-style params
    // like Blend, where the middle is an equal mix — NOT a bipolar zero).
    // Omitted in aggregate init → false.
    bool             center_mark;
    // Optional dynamic side hint for named-pole params (Blend SP/SW, Umbra/
    // Aurora, Atmosphere Blur/Radiation, Character Chorus/Friction, Waveshape
    // Saw/Fold). Drawn in the CycleView *value header* (Font_4x6 beside the %),
    // not in the segmented row. Low label below 50%, high above, nothing at
    // center. On bipolar macros the pole name replaces the ± sign.
    const char*      seg_hint_low;
    const char*      seg_hint_high;
    // Optional named labels for CountNum / CountBar (e.g. Filter Destination
    // Inp/Sp/Sw/Rv). Indexed as round(value) − round(min). Length must cover
    // the inclusive integer span [min, max]. Omitted → nullptr (numeric).
    const char* const* enum_labels;
    // Optional 0..1 expander for bipolar Pitch Spectra/Swarm: multiplies the
    // header ±% (and the engine octave span) from 1× (±100% / ±1 oct) to 2×
    // (±200% / ±2 oct). Driven by Engines Pitch Both. Omitted → nullptr.
    float* bipolar_span_ptr;
};

class ParameterRegistry
{
  public:
    static constexpr size_t kMaxParams = 128;

    ParameterRegistry() : count_(0) {}

    bool Register(const ParameterDef& def);
    const ParameterDef* Find(uint16_t id) const;
    const ParameterDef* At(size_t index) const;
    size_t              Count() const { return count_; }

    static float Normalize(const ParameterDef& def, float value);
    static float Denormalize(const ParameterDef& def, float norm);
    static float Clamp(const ParameterDef& def, float value);
    static float ApplyBipolarDeadzone(float norm);

  private:
    ParameterDef defs_[kMaxParams];
    size_t       count_;
};

} // namespace perseids
