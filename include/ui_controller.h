#pragma once

#include "button_gesture.h"
#include "capture_engine.h"
#include "capture_params.h"
#include "cycle_row.h"
#include "display_renderer.h"
#include "hw_pins.h"
#include "mux_adc.h"
#include "param_registry.h"
#include "spectra_engine.h"
#include "spectra_params.h"
#include "swarm_engine.h"
#include "swarm_params.h"
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
    static constexpr uint32_t kInactivityMs         = 4000;
    static constexpr uint32_t kResetConfirmMs       = 3000;
    static constexpr uint32_t kLongPressMs          = 1500;
    // Value edit once a Block is open (ignore ADC chatter).
    static constexpr float    kEditThreshold        = 0.015f;
    // Leave Dashboard — intentional pot travel from baseline.
    static constexpr float    kOpenThreshold        = 0.040f;
    static constexpr float    kResetCancelThreshold = 0.04f;
    static constexpr float    kScrollStepThreshold  = 0.10f;
    static constexpr uint32_t kScrollMinIntervalMs  = 180;
    static constexpr size_t   kMaxCycleRows         = 8;
    static constexpr uint32_t kDisplayMinIntervalMs = 33;
    static constexpr size_t   kMuxStepsPerTick      = 1; // Process reads full mux cache
    static constexpr uint32_t kLoopDelayMs          = 1;

    void Init(daisy::DaisySeed&      seed,
              ParameterRegistry&     registry,
              CycleRow*              rows,
              size_t                 row_count,
              const PotMapping*      pot_mappings,
              size_t                 pot_count,
              CaptureEngine&         capture,
              CaptureParamValues&    capture_params,
              SpectraEngine&         spectra,
              SpectraParamValues&    spectra_params,
              SwarmEngine&           swarm,
              SwarmParamValues&      swarm_params,
              std::atomic<float>*    cpu_load = nullptr);

    void Process();

    bool Playing() const { return playing_; }

  private:
    void PollControls();
    void HandleCycleButton(ButtonGesture::Event event);
    void HandlePotTurn(size_t row_idx, float pot_norm, float delta);
    void TouchActivity();
    void EnterDashboard();
    void UpdateScreen();
    void SyncEngines();
    void CapturePotBaselines();

    daisy::DaisySeed*    seed_;
    ParameterRegistry*   registry_;
    CycleRow*            rows_;
    size_t               row_count_;
    const PotMapping*    pot_mappings_;
    size_t               pot_count_;
    CaptureEngine*       capture_;
    CaptureParamValues*  capture_params_;
    SpectraEngine*       spectra_;
    SpectraParamValues*  spectra_params_;
    SwarmEngine*         swarm_;
    SwarmParamValues*    swarm_params_;
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
    uint32_t last_display_ms_;
    bool     cycle_held_prev_;
    bool     pot_moved_during_hold_;
    float    scroll_anchor_[kMaxCycleRows];
    uint32_t last_scroll_ms_[kMaxCycleRows];
    float    pot_prev_[kMaxCycleRows];
    float    pot_baseline_[kMaxCycleRows];
    bool     pot_prev_ok_[kMaxCycleRows];
};

} // namespace perseids
