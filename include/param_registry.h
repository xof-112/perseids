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
