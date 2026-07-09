#pragma once

#include "param_registry.h"

#include <cstddef>
#include <cstdint>

namespace perseids
{

// Generic cycle row reused by all 15 control rows in later phases.
class CycleRow
{
  public:
    CycleRow(const char* block_name,
             const uint16_t* param_ids,
             size_t          param_count);

    const char* BlockName() const { return block_name_; }

    size_t BoundIndex() const { return bound_index_; }
    size_t ScrollIndex() const { return scroll_index_; }
    size_t ParamCount() const { return param_count_; }

    const ParameterDef* BoundParam(const ParameterRegistry& reg) const;
    const ParameterDef* ScrollParam(const ParameterRegistry& reg) const;
    const ParameterDef* ParamAt(const ParameterRegistry& reg, size_t index) const;

    // Cycle button held + turn: scroll list, no value change (Section 4.6).
    void Scroll(int direction);

    // Normal turn: change bound parameter with pickup/catch (Section 4.6).
    void ChangeValue(const ParameterRegistry& reg, float pot_norm);

    // Cycle button released: bind to last scrolled entry.
    void CommitScrollBinding(const ParameterRegistry& reg);

    void InitPickup(const ParameterRegistry& reg);

    // Latest physical pot reading — keeps catch-up line aligned without a jump.
    void UpdatePotPosition(float pot_norm);

    bool     InCycleScroll() const { return cycle_scroll_active_; }
    void     SetCycleScrollActive(bool active) { cycle_scroll_active_ = active; }
    bool     PickupActive() const { return pickup_active_; }
    float    PickupPotNorm() const { return pickup_pot_norm_; }

  private:
    void BeginPickup(const ParameterDef& def);

    const char*       block_name_;
    const uint16_t*   param_ids_;
    size_t            param_count_;
    size_t            bound_index_;
    size_t            scroll_index_;
    bool              cycle_scroll_active_;
    bool              pickup_active_;
    float             pickup_target_norm_;
    float             pickup_pot_norm_;
    float             physical_pot_norm_;
    float             last_pot_norm_;
    bool              last_pot_valid_;
};

} // namespace perseids
