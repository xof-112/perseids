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
    // Optional dynamic side hint behind the abbrev in the segmented row
    // (crossfade-style params): lowercase label of the side the value is
    // currently on — low label below 50%, high label above, nothing at
    // exactly 50% ("sp"/"sw" for Blend). Omitted in aggregate init → nullptr.
    const char*      seg_hint_low;
    const char*      seg_hint_high;
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
