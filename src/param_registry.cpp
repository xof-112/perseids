#include "param_registry.h"

#include <cmath>

namespace perseids
{

bool ParameterRegistry::Register(const ParameterDef& def)
{
    if(count_ >= kMaxParams || def.value_ptr == nullptr)
        return false;
    defs_[count_++] = def;
    return true;
}

const ParameterDef* ParameterRegistry::Find(uint16_t id) const
{
    for(size_t i = 0; i < count_; ++i)
    {
        if(defs_[i].id == id)
            return &defs_[i];
    }
    return nullptr;
}

const ParameterDef* ParameterRegistry::At(size_t index) const
{
    return index < count_ ? &defs_[index] : nullptr;
}

float ParameterRegistry::Normalize(const ParameterDef& def, float value)
{
    const float range = def.max_val - def.min_val;
    if(range <= 0.f)
        return 0.f;
    return (Clamp(def, value) - def.min_val) / range;
}

float ParameterRegistry::Denormalize(const ParameterDef& def, float norm)
{
    return def.min_val + norm * (def.max_val - def.min_val);
}

float ParameterRegistry::Clamp(const ParameterDef& def, float value)
{
    if(value < def.min_val)
        return def.min_val;
    if(value > def.max_val)
        return def.max_val;
    return value;
}

float ParameterRegistry::ApplyBipolarDeadzone(float norm)
{
    if(norm >= 0.48f && norm <= 0.52f)
        return 0.5f;
    return norm;
}

} // namespace perseids
