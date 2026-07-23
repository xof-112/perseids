#pragma once

#include "button_gesture.h"
#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "display_renderer.h"
#include "hw_pins.h"
#include "mux_adc.h"
#include "param_registry.h"
#include "trail_level.h"

#include <atomic>
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
    static constexpr uint32_t kInactivityMs         = 7000;
    static constexpr uint32_t kResetConfirmMs       = 3000;
    static constexpr uint32_t kLongPressMs          = 1500;
    static constexpr float    kTurnThreshold        = 0.012f; // ~1.2 % — ignore mux jitter
    static constexpr float    kOpenCycleThreshold   = 0.022f; // leave Dashboard only on clear turn
    static constexpr float    kResetCancelThreshold = 0.03f;
    static constexpr float    kScrollStepThreshold  = 0.10f;
    static constexpr uint32_t kScrollMinIntervalMs  = 180;
    static constexpr size_t   kMaxCycleRows         = 8;
    static constexpr uint32_t kLoopDelayMs          = 2;

    void Init(daisy::DaisySeed&      seed,
              ParameterRegistry&     registry,
              CycleRow*              rows,
              size_t                 row_count,
              const PotMapping*      pot_mappings,
              size_t                 pot_count,
              CaptureEngine&         capture,
              CaptureParamValues&    capture_params,
              std::atomic<float>*    cpu_load = nullptr);

    void Process();

    bool Playing() const { return playing_; }

  private:
    void PollControls();
    void HandleCycleButton(ButtonGesture::Event event);
    void HandlePotTurn(size_t row_idx, float pot_norm, float delta);
    void TouchActivity();
    void UpdateScreen();
    void SyncCapture();

    daisy::DaisySeed*    seed_;
    ParameterRegistry*   registry_;
    CycleRow*            rows_;
    size_t               row_count_;
    const PotMapping*    pot_mappings_;
    size_t               pot_count_;
    CaptureEngine*       capture_;
    CaptureParamValues*  capture_params_;
    std::atomic<float>*  cpu_load_;

    MuxAdcPoller         mux_;
    DisplayRenderer      display_;
    ButtonGesture        cycle_btn_;
    TrailLevelController trails_;

    UiScreen screen_;
    size_t   active_row_;
    bool     playing_;
    bool     reset_confirm_;
    uint32_t reset_deadline_ms_;
    uint32_t last_activity_ms_;
    bool     cycle_held_prev_;
    bool     pot_moved_during_hold_;
    float    scroll_anchor_[kMaxCycleRows];
    uint32_t last_scroll_ms_[kMaxCycleRows];
};

} // namespace perseids
