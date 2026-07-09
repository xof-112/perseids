#include "cycle_row.h"

#include <cmath>

namespace perseids
{

CycleRow::CycleRow(const char*     block_name,
                   const uint16_t* param_ids,
                   size_t          param_count)
: block_name_(block_name)
, param_ids_(param_ids)
, param_count_(param_count)
, bound_index_(0)
, scroll_index_(0)
, cycle_scroll_active_(false)
, pickup_active_(false)
, pickup_target_norm_(0.f)
, pickup_pot_norm_(0.f)
, physical_pot_norm_(0.5f)
, last_pot_norm_(0.f)
, last_pot_valid_(false)
{
}

const ParameterDef* CycleRow::BoundParam(const ParameterRegistry& reg) const
{
    return param_count_ > 0 ? reg.Find(param_ids_[bound_index_]) : nullptr;
}

const ParameterDef* CycleRow::ScrollParam(const ParameterRegistry& reg) const
{
    return param_count_ > 0 ? reg.Find(param_ids_[scroll_index_]) : nullptr;
}

const ParameterDef* CycleRow::ParamAt(const ParameterRegistry& reg,
                                      size_t                  index) const
{
    return index < param_count_ ? reg.Find(param_ids_[index]) : nullptr;
}

void CycleRow::Scroll(int direction)
{
    if(param_count_ == 0)
        return;

    if(direction > 0)
        scroll_index_ = (scroll_index_ + 1) % param_count_;
    else if(direction < 0)
        scroll_index_ = (scroll_index_ + param_count_ - 1) % param_count_;
}

void CycleRow::UpdatePotPosition(float pot_norm)
{
    physical_pot_norm_ = pot_norm;
    if(pickup_active_)
        pickup_pot_norm_ = pot_norm;
}

void CycleRow::ChangeValue(const ParameterRegistry& reg, float pot_norm)
{
    const ParameterDef* def = BoundParam(reg);
    if(def == nullptr)
        return;

    physical_pot_norm_ = pot_norm;
    pickup_pot_norm_   = pot_norm;

    if(pickup_active_)
    {
        if(last_pot_valid_)
        {
            const bool crossed_up
                = last_pot_norm_ < pickup_target_norm_
                  && pot_norm >= pickup_target_norm_;
            const bool crossed_down
                = last_pot_norm_ > pickup_target_norm_
                  && pot_norm <= pickup_target_norm_;
            const bool near = std::fabs(pot_norm - pickup_target_norm_) < 0.02f;

            if(crossed_up || crossed_down || near)
                pickup_active_ = false;
        }

        last_pot_norm_  = pot_norm;
        last_pot_valid_ = true;

        if(pickup_active_)
            return;
    }

    float norm = pot_norm;
    if(def->display_type == ParamDisplayType::Bipolar && def->bipolar_deadzone)
        norm = ParameterRegistry::ApplyBipolarDeadzone(norm);

    if(def->display_type == ParamDisplayType::Toggle)
    {
        *def->value_ptr = (norm >= 0.5f) ? def->max_val : def->min_val;
        return;
    }

    *def->value_ptr
        = ParameterRegistry::Clamp(*def,
                                   ParameterRegistry::Denormalize(*def, norm));
}

void CycleRow::CommitScrollBinding(const ParameterRegistry& reg)
{
    if(scroll_index_ != bound_index_)
    {
        bound_index_ = scroll_index_;
        if(const ParameterDef* def = BoundParam(reg))
            BeginPickup(*def);
    }
}

void CycleRow::InitPickup(const ParameterRegistry& reg)
{
    if(const ParameterDef* def = BoundParam(reg))
        BeginPickup(*def);
}

void CycleRow::BeginPickup(const ParameterDef& def)
{
    if(def.display_type == ParamDisplayType::Toggle)
    {
        pickup_active_ = false;
        return;
    }

    pickup_active_      = true;
    pickup_target_norm_ = ParameterRegistry::Normalize(def, *def.value_ptr);
    pickup_pot_norm_    = physical_pot_norm_;
    last_pot_norm_      = physical_pot_norm_;
    last_pot_valid_     = true;

    if(std::fabs(physical_pot_norm_ - pickup_target_norm_) < 0.02f)
        pickup_active_ = false;
}

} // namespace perseids
