#include "cycle_row.h"

#include <cmath>

namespace perseids
{

namespace
{
// ADC rarely reaches exact 0.0 / 1.0 — catch & snap within this band at either end.
constexpr float kEndCatchNorm = 0.94f; // snap-to-end when writing
constexpr float kEndCatchPot  = 0.90f; // pot band to *meet* a stored end (mux tops out early)
constexpr float kEndNearEps   = 0.06f;
constexpr float kDefaultNear  = 0.02f;

// Params whose min/max sit at pot ends and must remain catchable (Count, Hold INF,
// Buffer/Fade times, etc.). Apply the same end-catch for every new such param.
bool UsesPotEndCatch(ParamDisplayType type)
{
    switch(type)
    {
    case ParamDisplayType::HoldTime:
    case ParamDisplayType::CountNum:
    case ParamDisplayType::CountBar:
    case ParamDisplayType::Seconds:
        return true;
    default:
        return false;
    }
}

bool EndCatchReady(float target_norm, float pot_norm)
{
    // Stored value at travel end (snap band) + pot in the slightly wider mux end band.
    // Fixes Count=5 / Hold INF pickup when the ADC never quite reaches 0.94.
    const bool top
        = target_norm >= kEndCatchNorm && pot_norm >= kEndCatchPot;
    const bool bottom
        = target_norm <= (1.f - kEndCatchNorm)
          && pot_norm <= (1.f - kEndCatchPot);
    return top || bottom;
}

float SnapEndNorm(float norm)
{
    if(norm >= kEndCatchNorm)
        return 1.f;
    if(norm <= (1.f - kEndCatchNorm))
        return 0.f;
    return norm;
}
} // namespace

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
                                      size_t                   index) const
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

    const bool end_catch = UsesPotEndCatch(def->display_type);

    if(pickup_active_)
    {
        if(last_pot_valid_)
        {
            const float near_eps = end_catch ? kEndNearEps : kDefaultNear;
            const bool  crossed_up
                = last_pot_norm_ < pickup_target_norm_
                  && pot_norm >= pickup_target_norm_;
            const bool crossed_down
                = last_pot_norm_ > pickup_target_norm_
                  && pot_norm <= pickup_target_norm_;
            const bool near
                = std::fabs(pot_norm - pickup_target_norm_) < near_eps;
            const bool ends
                = end_catch && EndCatchReady(pickup_target_norm_, pot_norm);

            if(crossed_up || crossed_down || near || ends)
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

    if(end_catch)
        norm = SnapEndNorm(norm);

    float value = ParameterRegistry::Clamp(
        *def, ParameterRegistry::Denormalize(*def, norm));

    // Discrete counts: store whole numbers after end-snap / denormalize.
    if(def->display_type == ParamDisplayType::CountNum
       || def->display_type == ParamDisplayType::CountBar)
    {
        value = static_cast<float>(static_cast<int>(value + 0.5f));
        value = ParameterRegistry::Clamp(*def, value);
    }

    *def->value_ptr = value;
}

void CycleRow::CommitScrollBinding(const ParameterRegistry& reg)
{
    // Always re-arm on cycle release — pot rarely matches the newly shown value.
    bound_index_ = scroll_index_;
    if(const ParameterDef* def = BoundParam(reg))
        BeginPickup(*def);
}

void CycleRow::InitPickup(const ParameterRegistry& reg)
{
    if(const ParameterDef* def = BoundParam(reg))
        BeginPickup(*def);
}

void CycleRow::ArmPickupIfNeeded(const ParameterRegistry& reg)
{
    if(const ParameterDef* def = BoundParam(reg))
    {
        if(def->display_type == ParamDisplayType::Toggle)
            return;

        const float target    = ParameterRegistry::Normalize(*def, *def->value_ptr);
        const bool  end_catch = UsesPotEndCatch(def->display_type);
        const float near_eps  = end_catch ? kEndNearEps : kDefaultNear;

        if(std::fabs(physical_pot_norm_ - target) < near_eps)
            return;
        if(end_catch && EndCatchReady(target, physical_pot_norm_))
            return;

        BeginPickup(*def);
    }
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

    const bool  end_catch = UsesPotEndCatch(def.display_type);
    const float near_eps  = end_catch ? kEndNearEps : kDefaultNear;

    if(std::fabs(physical_pot_norm_ - pickup_target_norm_) < near_eps
       || (end_catch && EndCatchReady(pickup_target_norm_, physical_pot_norm_)))
        pickup_active_ = false;
}

} // namespace perseids
