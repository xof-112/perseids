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
#include "multi_params.h"
#include "quadrature_encoder.h"
#include "spatial_params.h"

#include <atomic>
#include <cstdint>

namespace perseids
{

enum class UiScreen : uint8_t
{
    Dashboard,
    CycleView,
    MultiView,
};

// Top-level UI state machine — main loop only, never in the audio callback.
class UiController
{
  public:
    // Auto-return to Home (ARCHITECTURE 4.7a). Was 4s bench interim — too
    // short once CycleViews scroll (Resonator 6 params) and slow pot turns
    // no longer reset the timer every frame.
    static constexpr uint32_t kInactivityMs         = 7000;
    static constexpr uint32_t kResetConfirmMs       = 3000;
    static constexpr uint32_t kLongPressMs          = 1500;
    // Imprint lock-all — shorter than Cycle delete so a firm hold registers.
    static constexpr uint32_t kImprintLongPressMs   = 800;
    // Value edit once a Block is open (ignore ADC chatter).
    static constexpr float    kEditThreshold        = 0.015f;
    // Idle-timer refresh: below kEditThreshold so slow turns still count as
    // activity, above mux noise so a parked pot cannot freeze the timeout.
    static constexpr float    kActivityThreshold    = 0.004f;
    // Leave Dashboard — cumulative travel from baseline + same-direction confirm.
    // REVERT "pot-menu open baseline": set kOpenThreshold=0.040f and remove the
    // kOpenConfirmTravel / open_dir_* gate (Phase-5 policy: travel-only @ 4%).
    static constexpr float    kOpenThreshold        = 0.028f;
    static constexpr float    kOpenConfirmTravel    = 0.012f;
    static constexpr float    kOpenStepNoise        = 0.002f;
    // Switch Block while another CycleView is open — stricter than open.
    static constexpr float    kSwitchThreshold      = 0.060f;
    static constexpr float    kSwitchConfirmTravel  = 0.018f;
    static constexpr float    kResetCancelThreshold = 0.04f;
    static constexpr float    kScrollStepThreshold  = 0.10f;
    static constexpr uint32_t kScrollMinIntervalMs  = 180;
    static constexpr size_t   kMaxCycleRows         = 12;
    static constexpr uint32_t kDisplayMinIntervalMs = 33;
    static constexpr size_t   kMuxStepsPerTick      = 1; // Process reads full mux cache
    static constexpr uint32_t kLoopDelayMs          = 1;
    static constexpr float    kMultiPushOnThresh    = 0.35f;
    static constexpr float    kMultiPushOffThresh   = 0.65f;
    // Cycle+Multi scroll: quad encoders emit several ticks per detent.
    static constexpr int      kMultiScrollTicksPerStep   = 4;
    static constexpr uint32_t kMultiScrollMinIntervalMs  = 220;
    static constexpr uint32_t kMultiBootIgnoreMs         = 500;
    // Discrete Multi edits (CountNum/Toggle): one detent → one step.
    static constexpr uint32_t kDiscreteEncMinIntervalMs  = 160;

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
              CycleRow&              multi_row,
              MultiParamValues&      multi_params,
              SpatialParamValues&    spatial_params,
              std::atomic<float>*    dry_wet,
              std::atomic<float>*    cpu_load = nullptr);

    void Process();

    bool Playing() const { return playing_; }

  private:
    void PollControls();
    void HandleCycleButton(ButtonGesture::Event event);
    void HandleImprintButton(ButtonGesture::Event event);
    void HandlePotTurn(size_t row_idx, float pot_norm, float delta);
    void TouchActivity();
    void EnterDashboard();
    void EnterMultiView();
    void EnterSettingsView();
    void HandleMultiShortPress();
    void StepMultiOrSettingsMenu(int direction);
    void ApplyMultiEncoderSteps(int32_t steps);
    void ApplyEncoderToParam(const ParameterDef& def, int32_t steps);
    bool InSettingsView() const;
    bool ReadMultiPushPressed();
    void CalibrateMultiPushIdle();
    void UpdateScreen();
    void SyncEngines();
    void CapturePotBaselines();
    void PollMultiEncoder();

    daisy::DaisySeed*    seed_;
    ParameterRegistry*   registry_;
    CycleRow*            rows_;
    size_t               row_count_;
    size_t               settings_row_; // pot-less Settings CycleRow index
    const PotMapping*    pot_mappings_;
    size_t               pot_count_;
    CaptureEngine*       capture_;
    CaptureParamValues*  capture_params_;
    SpectraEngine*       spectra_;
    SpectraParamValues*  spectra_params_;
    SwarmEngine*         swarm_;
    SwarmParamValues*    swarm_params_;
    CycleRow*            multi_row_;
    MultiParamValues*    multi_params_;
    SpatialParamValues*  spatial_params_;
    std::atomic<float>*  dry_wet_;
    std::atomic<float>*  cpu_load_;

    MuxAdcPoller         mux_;
    DisplayRenderer      display_;
    ButtonGesture        cycle_btn_;
    ButtonGesture        imprint_btn_;
    TrailLevelController trails_;
    QuadratureEncoder    multi_enc_;

    UiScreen screen_;
    size_t   active_row_;
    bool     playing_;
    bool     reset_confirm_;
    uint32_t reset_deadline_ms_;
    uint32_t last_activity_ms_;
    uint32_t last_display_ms_;
    bool     cycle_held_prev_;
    bool     pot_moved_during_hold_;
    // Bitmask of Trails locked by Imprint (selective release, 4.7b).
    uint8_t  imprint_mask_;
    bool     imprint_engaged_;
    bool     multi_push_prev_;
    bool     multi_push_long_fired_;
    bool     multi_push_active_low_; // idle high → press pulls to GND
    uint32_t multi_push_down_ms_;
    float    multi_push_idle_;
    int32_t  multi_scroll_accum_;
    uint32_t last_multi_scroll_ms_;
    uint32_t multi_boot_ignore_until_ms_;
    uint32_t last_discrete_enc_ms_;
    // After BCK leaves Settings, block gateway re-enter until Multi list moves.
    bool     suppress_settings_enter_;
    float    scroll_anchor_[kMaxCycleRows];
    uint32_t last_scroll_ms_[kMaxCycleRows];
    float    pot_prev_[kMaxCycleRows];
    float    pot_baseline_[kMaxCycleRows];
    bool     pot_prev_ok_[kMaxCycleRows];
    // Same-direction step accumulator for Dashboard→CycleView open confirm.
    float    open_dir_accum_[kMaxCycleRows];
    int8_t   open_dir_sign_[kMaxCycleRows];
};

} // namespace perseids
