#pragma once

#include "button_gesture.h"
#include "cycle_row.h"
#include "display_renderer.h"
#include "hw_pins.h"
#include "mux_adc.h"
#include "param_registry.h"

#include <cstdint>

namespace perseids
{

enum class UiScreen : uint8_t
{
    Dashboard,
    CycleView,
};

// Top-level UI state machine — main loop only, never in the audio callback.
class UiController
{
  public:
    static constexpr uint32_t kInactivityMs      = 7000;
    static constexpr uint32_t kResetConfirmMs    = 3000;
    static constexpr uint32_t kLongPressMs       = 1500; // reset only when held alone (4.7)
    static constexpr float    kTurnThreshold          = 0.003f;
    static constexpr float    kPotActivityDuringHold  = 0.012f;
    static constexpr float    kScrollStepThreshold  = 0.10f;  // 10 % pot travel per step
    static constexpr uint32_t kScrollMinIntervalMs  = 180;    // hard cap on step rate
    static constexpr size_t   kMaxCycleRows       = 8;
    static constexpr uint32_t kLoopDelayMs        = 2;

    void Init(daisy::DaisySeed&      seed,
              ParameterRegistry&     registry,
              CycleRow*              rows,
              size_t                 row_count,
              const PotMapping*      pot_mappings,
              size_t                 pot_count);

    void Process();

    bool Playing() const { return playing_; }

  private:
    void PollControls();
    void HandleCycleButton(ButtonGesture::Event event);
    void HandlePotTurn(size_t row_idx, float pot_norm, float delta);
    void TouchActivity();
    void UpdateScreen();

    daisy::DaisySeed*  seed_;
    ParameterRegistry* registry_;
    CycleRow*          rows_;
    size_t             row_count_;
    const PotMapping*  pot_mappings_;
    size_t             pot_count_;

    MuxAdcPoller     mux_;
    DisplayRenderer  display_;
    ButtonGesture    cycle_btn_;

    UiScreen  screen_;
    size_t    active_row_;
    bool      playing_;
    bool      reset_confirm_;
    uint32_t  reset_deadline_ms_;
    uint32_t  last_activity_ms_;
    bool      cycle_held_prev_;
    bool      pot_moved_during_hold_;
    float     scroll_anchor_[kMaxCycleRows];
    uint32_t  last_scroll_ms_[kMaxCycleRows];
};

} // namespace perseids
