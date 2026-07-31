#include "ui_controller.h"

#include "hw_pins.h"

#include <cmath>

namespace perseids
{

void UiController::Init(daisy::DaisySeed&   seed,
                        ParameterRegistry&  registry,
                        CycleRow*           rows,
                        size_t              row_count,
                        const PotMapping*   pot_mappings,
                        size_t              pot_count,
                        CaptureEngine&      capture,
                        CaptureParamValues& capture_params,
                        SpectraEngine&      spectra,
                        SpectraParamValues& spectra_params,
                        SwarmEngine&        swarm,
                        SwarmParamValues&   swarm_params,
                        CycleRow&           multi_row,
                        MultiParamValues&   multi_params,
                        SpatialParamValues& spatial_params,
                        std::atomic<float>* dry_wet,
                        std::atomic<float>* cpu_load)
{
    seed_            = &seed;
    registry_        = &registry;
    rows_            = rows;
    row_count_       = row_count;
    settings_row_    = row_count; // invalid until resolved below
    pot_mappings_    = pot_mappings;
    pot_count_       = pot_count;
    capture_         = &capture;
    capture_params_  = &capture_params;
    spectra_         = &spectra;
    spectra_params_  = &spectra_params;
    swarm_           = &swarm;
    swarm_params_    = &swarm_params;
    multi_row_       = &multi_row;
    multi_params_    = &multi_params;
    spatial_params_  = &spatial_params;
    dry_wet_         = dry_wet;
    cpu_load_        = cpu_load;

    screen_                = UiScreen::Dashboard;
    active_row_            = 0;
    playing_               = true;
    reset_confirm_         = false;
    reset_deadline_ms_     = 0;
    last_activity_ms_      = daisy::System::GetNow();
    last_display_ms_       = 0;
    cycle_held_prev_       = false;
    pot_moved_during_hold_ = false;
    imprint_mask_          = 0;
    imprint_engaged_       = false;
    multi_push_prev_       = false;
    multi_push_long_fired_ = false;
    multi_push_active_low_ = true;
    multi_push_down_ms_    = 0;
    multi_push_idle_       = 1.f;
    multi_scroll_accum_    = 0;
    last_multi_scroll_ms_  = 0;
    multi_boot_ignore_until_ms_ = 0;
    last_discrete_enc_ms_  = 0;

    for(size_t i = 0; i < kMaxCycleRows; ++i)
    {
        scroll_anchor_[i]  = 0.f;
        last_scroll_ms_[i] = 0;
        pot_prev_[i]       = 0.f;
        pot_baseline_[i]   = 0.f;
        pot_prev_ok_[i]    = false;
        open_dir_accum_[i] = 0.f;
        open_dir_sign_[i]  = 0;
    }

    mux_.Init(seed);
    display_.Init(seed);
    cycle_btn_.Init(hw::kCycleButton, kLongPressMs);
    imprint_btn_.Init(hw::kImprintButton, kImprintLongPressMs);
    multi_enc_.Init(hw::kMultiEncClk, hw::kMultiEncDt);
    trails_.Init(seed, capture_, &capture_params_->trail_lvl);

    if(dry_wet_ != nullptr && multi_params_ != nullptr)
        dry_wet_->store(multi_params_->dry_wet, std::memory_order_relaxed);

    for(size_t n = 0; n < 48; ++n)
        mux_.Process();

    CalibrateMultiPushIdle();

    // Drain encoder chatter from GPIO init; sync push edge so boot cannot
    // fake a short-press → MultiView. Ignore Multi opens briefly after boot.
    for(size_t n = 0; n < 16; ++n)
        multi_enc_.Debounce();
    (void)multi_enc_.ConsumeSteps();
    multi_push_prev_            = ReadMultiPushPressed();
    multi_boot_ignore_until_ms_ = daisy::System::GetNow() + kMultiBootIgnoreMs;

    for(size_t i = 0; i < row_count_; ++i)
    {
        if(const ParameterDef* p = rows_[i].ParamAt(registry, 0))
        {
            if(p->id == kSettingsCpuMeter)
            {
                settings_row_ = i;
                break;
            }
        }
    }

    for(size_t i = 0; i < pot_count_ && i < row_count_; ++i)
    {
        const PotMapping& map = pot_mappings_[i];
        const float       n   = mux_.Get(map.chain, map.channel);
        rows_[i].UpdatePotPosition(n);
        rows_[i].InitPickup(registry);
    }
    CapturePotBaselines();

    SyncEngines();
}

void UiController::CapturePotBaselines()
{
    for(size_t i = 0; i < pot_count_ && i < row_count_; ++i)
    {
        const PotMapping& map = pot_mappings_[i];
        const float       n   = mux_.Get(map.chain, map.channel);
        pot_prev_[i]          = n;
        pot_baseline_[i]      = n;
        pot_prev_ok_[i]       = true;
        open_dir_accum_[i]    = 0.f;
        open_dir_sign_[i]     = 0;
    }
}

void UiController::CalibrateMultiPushIdle()
{
    float sum = 0.f;
    for(size_t n = 0; n < 16; ++n)
    {
        mux_.Process();
        sum += mux_.GetRaw(hw::kMultiPushChain, hw::kMultiPushChannel);
    }
    multi_push_idle_       = sum * (1.f / 16.f);
    // Pull-up to 3V3 → idle high, press to GND. Pull-down → idle low, press to 3V3.
    multi_push_active_low_ = multi_push_idle_ >= 0.45f;
}

bool UiController::ReadMultiPushPressed()
{
    const float v = mux_.GetRaw(hw::kMultiPushChain, hw::kMultiPushChannel);
    if(multi_push_active_low_)
    {
        // Idle high — pressed when low.
        if(multi_push_prev_)
            return v < kMultiPushOffThresh;
        return v < kMultiPushOnThresh;
    }
    // Idle low — pressed when high.
    if(multi_push_prev_)
        return v > (1.f - kMultiPushOffThresh);
    return v > (1.f - kMultiPushOnThresh);
}

void UiController::PollMultiEncoder()
{
    if(multi_row_ == nullptr || multi_params_ == nullptr || dry_wet_ == nullptr)
        return;

    multi_enc_.Debounce();
    const int32_t  steps = multi_enc_.ConsumeSteps();
    const uint32_t now   = daisy::System::GetNow();

    // Boot: stay on Dashboard — mux/GPIO settle must not open Multi.
    if(now < multi_boot_ignore_until_ms_)
    {
        multi_scroll_accum_ = 0;
        multi_push_prev_    = ReadMultiPushPressed();
        return;
    }

    if(cycle_btn_.IsHeld())
    {
        if(steps != 0)
        {
            pot_moved_during_hold_ = true;
            multi_scroll_accum_ += steps;
            TouchActivity();
            // Stay in Settings submenu when Cycle+Multi scrolls there;
            // otherwise open Multi list.
            if(!InSettingsView() && screen_ != UiScreen::MultiView)
                EnterMultiView();
        }

        // One menu step per detent-ish burst, rate-limited — stops the list
        // from leaping through all entries on a single click.
        if((now - last_multi_scroll_ms_) >= kMultiScrollMinIntervalMs)
        {
            if(multi_scroll_accum_ >= kMultiScrollTicksPerStep)
            {
                if(InSettingsView() || screen_ == UiScreen::MultiView)
                    StepMultiOrSettingsMenu(1);
                multi_scroll_accum_ -= kMultiScrollTicksPerStep;
                last_multi_scroll_ms_ = now;
                TouchActivity();
            }
            else if(multi_scroll_accum_ <= -kMultiScrollTicksPerStep)
            {
                if(InSettingsView() || screen_ == UiScreen::MultiView)
                    StepMultiOrSettingsMenu(-1);
                multi_scroll_accum_ += kMultiScrollTicksPerStep;
                last_multi_scroll_ms_ = now;
                TouchActivity();
            }
        }
    }
    else
    {
        multi_scroll_accum_ = 0;
        if(steps != 0)
        {
            if(InSettingsView())
            {
                ApplyMultiEncoderSteps(steps);
            }
            else
            {
                if(screen_ != UiScreen::MultiView)
                    EnterMultiView();
                ApplyMultiEncoderSteps(steps);
            }
            TouchActivity();
        }
    }

    const bool pressed = ReadMultiPushPressed();

    if(pressed && !multi_push_prev_)
    {
        multi_push_down_ms_    = now;
        multi_push_long_fired_ = false;
    }

    if(pressed && !multi_push_long_fired_
       && (now - multi_push_down_ms_) >= kLongPressMs)
    {
        multi_push_long_fired_ = true;
        EnterDashboard();
        TouchActivity();
    }

    if(!pressed && multi_push_prev_ && !multi_push_long_fired_)
        HandleMultiShortPress();

    multi_push_prev_ = pressed;
}

void UiController::EnterMultiView()
{
    screen_ = UiScreen::MultiView;
    // Re-baseline pots so ADC chatter cannot steal the screen into Trails
    // (active_row_ defaults to 0) on the next PollControls tick.
    CapturePotBaselines();
    TouchActivity();
}

void UiController::EnterSettingsView()
{
    if(settings_row_ >= row_count_)
        return;
    active_row_ = settings_row_;
    screen_     = UiScreen::CycleView;
    CapturePotBaselines();
    TouchActivity();
}

bool UiController::InSettingsView() const
{
    return screen_ == UiScreen::CycleView && settings_row_ < row_count_
           && active_row_ == settings_row_;
}

bool UiController::TryEnterSettingsIfBound()
{
    if(multi_row_ == nullptr || registry_ == nullptr)
        return false;
    const ParameterDef* def = multi_row_->BoundParam(*registry_);
    if(def == nullptr || def->id != kMultiSettings)
        return false;
    EnterSettingsView();
    return true;
}

void UiController::HandleMultiShortPress()
{
    TouchActivity();
    if(InSettingsView() || screen_ == UiScreen::MultiView)
    {
        StepMultiOrSettingsMenu(1);
        return;
    }
    // First short press opens Multi on the current bound entry (Dry/Wet default).
    EnterMultiView();
}

void UiController::StepMultiOrSettingsMenu(int direction)
{
    if(direction == 0)
        return;

    if(InSettingsView())
    {
        rows_[settings_row_].StepBound(direction);
        return;
    }

    if(screen_ != UiScreen::MultiView || multi_row_ == nullptr)
        return;

    // Already on Settings gateway → open submenu; else step, then enter if landed.
    if(direction > 0 && TryEnterSettingsIfBound())
        return;

    multi_row_->StepBound(direction);
    if(direction > 0)
        TryEnterSettingsIfBound();
}

void UiController::ApplyEncoderToParam(const ParameterDef& def, int32_t steps)
{
    if(def.value_ptr == nullptr || steps == 0)
        return;

    if(def.display_type == ParamDisplayType::Toggle
       || def.display_type == ParamDisplayType::CountNum
       || def.display_type == ParamDisplayType::CountBar)
    {
        // Quad encoders emit several ticks per detent — never jump discrete
        // enums by |steps| (PRS→CTR was skipping PLR). Also rate-limit so one
        // detent split across polls cannot double-step.
        const uint32_t now = daisy::System::GetNow();
        if(now - last_discrete_enc_ms_ < kDiscreteEncMinIntervalMs)
            return;
        last_discrete_enc_ms_ = now;

        const float delta = (steps > 0) ? 1.f : -1.f;
        float       v     = *def.value_ptr + delta;
        v                 = ParameterRegistry::Clamp(
            def,
            static_cast<float>(
                static_cast<int>(v + (v >= 0.f ? 0.5f : -0.5f))));
        *def.value_ptr = v;
    }
    else
    {
        float norm = ParameterRegistry::Normalize(def, *def.value_ptr);
        norm += static_cast<float>(steps) * 0.02f;
        if(norm < 0.f)
            norm = 0.f;
        else if(norm > 1.f)
            norm = 1.f;
        *def.value_ptr = ParameterRegistry::Clamp(
            def, ParameterRegistry::Denormalize(def, norm));
    }

    if(def.id == kMultiDryWet && dry_wet_ != nullptr)
        dry_wet_->store(*def.value_ptr, std::memory_order_relaxed);

    if(def.id == kSettingsTrailLevel)
        trails_.ApplyDefaultLevels();

    if(def.id == kSettingsTrailCount && capture_params_ != nullptr)
    {
        float c = *def.value_ptr;
        if(c < 1.f)
            c = 1.f;
        else if(c > static_cast<float>(kTrailCount))
            c = static_cast<float>(kTrailCount);
        c = static_cast<float>(static_cast<int>(c + 0.5f));
        *def.value_ptr           = c;
        capture_params_->count   = c;
        capture_params_->trail_cnt = c;
    }
}

void UiController::ApplyMultiEncoderSteps(int32_t steps)
{
    if(steps == 0 || registry_ == nullptr)
        return;

    if(InSettingsView())
    {
        if(const ParameterDef* def = rows_[settings_row_].BoundParam(*registry_))
            ApplyEncoderToParam(*def, steps);
        return;
    }

    if(multi_row_ == nullptr)
        return;

    const ParameterDef* def = multi_row_->BoundParam(*registry_);
    if(def == nullptr || def->value_ptr == nullptr)
        return;

    // Settings gateway is display-only — enter via short press / land.
    if(def->id == kMultiSettings)
        return;

    ApplyEncoderToParam(*def, steps);
}

void UiController::SyncEngines()
{
    TrailMixerState mixer[TrailLevelController::kCount];
    trails_.FillMixerState(mixer);
    capture_->SyncFromUi(*capture_params_, mixer, playing_, *spatial_params_);
    spectra_->SyncFromUi(*spectra_params_, swarm_params_->pitch_both);
    swarm_->SyncFromUi(*swarm_params_);
}

void UiController::TouchActivity()
{
    last_activity_ms_ = daisy::System::GetNow();
}

void UiController::EnterDashboard()
{
    if(screen_ == UiScreen::Dashboard)
        return;

    screen_ = UiScreen::Dashboard;
    CapturePotBaselines();
    TouchActivity();
}

void UiController::HandleCycleButton(ButtonGesture::Event event)
{
    if(reset_confirm_)
    {
        if(event == ButtonGesture::Event::ShortPress)
        {
            trails_.ResetAll();
            imprint_mask_    = 0;
            imprint_engaged_ = false;
            reset_confirm_   = false;
            playing_         = true;
            if(capture_params_ != nullptr)
            {
                float c = capture_params_->trail_cnt;
                if(c < 1.f)
                    c = 1.f;
                else if(c > static_cast<float>(kTrailCount))
                    c = static_cast<float>(kTrailCount);
                c = static_cast<float>(static_cast<int>(c + 0.5f));
                capture_params_->trail_cnt = c;
                capture_params_->count     = c;
            }
            EnterDashboard();
        }
        return;
    }

    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        // Interim: Multi encoder push is unreliable — step Multi/Settings like
        // Cycle-hold+turn does for block pots. Elsewhere short stays unused (4.7).
        if(InSettingsView() || screen_ == UiScreen::MultiView)
        {
            StepMultiOrSettingsMenu(1);
            TouchActivity();
        }
        break;

    case ButtonGesture::Event::LongPress:
        if(!pot_moved_during_hold_)
        {
            reset_confirm_     = true;
            reset_deadline_ms_ = daisy::System::GetNow() + kResetConfirmMs;
            EnterDashboard();
        }
        break;

    default:
        break;
    }
}

void UiController::HandleImprintButton(ButtonGesture::Event event)
{
    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        // Global Play/Pause (moved off Cycle — ARCHITECTURE 4.7).
        playing_ = !playing_;
        TouchActivity();
        break;

    case ButtonGesture::Event::LongPress:
    {
        // Imprint lock toggle (4.7b): engage all active / selective release.
        size_t active = static_cast<size_t>(capture_params_->count + 0.5f);
        if(active < 1)
            active = 1;
        if(active > kTrailCount)
            active = kTrailCount;

        if(imprint_engaged_)
        {
            imprint_mask_    = trails_.ImprintRelease(imprint_mask_);
            imprint_engaged_ = false;
        }
        else
        {
            imprint_mask_    = trails_.ImprintEngage(active, 0);
            imprint_engaged_ = true;
        }
        TouchActivity();
        break;
    }

    default:
        break;
    }
}

void UiController::HandlePotTurn(size_t row_idx, float pot_norm, float delta)
{
    if(row_idx >= row_count_)
        return;

    // New Block (from Dashboard/Multi) or switch away from another Block.
    const bool entering_block
        = (screen_ != UiScreen::CycleView) || (active_row_ != row_idx);

    // HandlePotTurn runs every frame for the open Block — do not touch on
    // every call. Refresh the idle timer on enter, on real pot motion
    // (including slow turns below kEditThreshold), and on scroll steps below.
    if(entering_block || std::fabs(delta) >= kActivityThreshold)
        TouchActivity();

    active_row_ = row_idx;
    screen_     = UiScreen::CycleView;

    CycleRow& row = rows_[row_idx];

    if(cycle_btn_.IsHeld())
    {
        row.SetCycleScrollActive(true);

        const uint32_t now   = daisy::System::GetNow();
        const float    moved = pot_norm - scroll_anchor_[row_idx];

        if(now - last_scroll_ms_[row_idx] >= kScrollMinIntervalMs)
        {
            if(moved >= kScrollStepThreshold)
            {
                row.Scroll(1);
                scroll_anchor_[row_idx] += kScrollStepThreshold;
                last_scroll_ms_[row_idx] = now;
                TouchActivity();
            }
            else if(moved <= -kScrollStepThreshold)
            {
                row.Scroll(-1);
                scroll_anchor_[row_idx] -= kScrollStepThreshold;
                last_scroll_ms_[row_idx] = now;
                TouchActivity();
            }
        }
    }
    else
    {
        if(entering_block)
            row.ArmPickupIfNeeded(*registry_);
        row.SetCycleScrollActive(false);
        row.ChangeValue(*registry_, pot_norm);
    }
}

void UiController::PollControls()
{
    trails_.BeginFrame();

    for(size_t s = 0; s < kMuxStepsPerTick; ++s)
        mux_.Process();

    const size_t n_pots
        = (pot_count_ < row_count_) ? pot_count_ : row_count_;

    for(size_t i = 0; i < n_pots; ++i)
    {
        const PotMapping& map = pot_mappings_[i];
        rows_[i].UpdatePotPosition(mux_.Get(map.chain, map.channel));
    }

    cycle_btn_.Debounce();
    imprint_btn_.Debounce();

    // After Cycle debounce so hold+Multi-turn scroll sees a fresh IsHeld().
    PollMultiEncoder();

    const bool cycle_held = cycle_btn_.IsHeld();
    if(cycle_held && !cycle_held_prev_)
    {
        pot_moved_during_hold_ = false;
        TouchActivity(); // holding Cycle is interaction (scroll / reset path)
        for(size_t i = 0; i < n_pots; ++i)
        {
            const PotMapping& map = pot_mappings_[i];
            scroll_anchor_[i]     = mux_.Get(map.chain, map.channel);
            last_scroll_ms_[i]    = daisy::System::GetNow();
        }
        // Start Multi list from the currently bound entry when Cycle goes down.
        if(multi_row_ != nullptr && screen_ == UiScreen::MultiView)
            multi_row_->SyncScrollToBound();
    }

    if(cycle_held_prev_ && !cycle_held)
    {
        TouchActivity(); // commit after scroll — reading the new param is not idle
        for(size_t i = 0; i < n_pots; ++i)
        {
            rows_[i].SetCycleScrollActive(false);
            rows_[i].CommitScrollBinding(*registry_);
        }
        if(multi_row_ != nullptr)
            multi_row_->SetCycleScrollActive(false);
    }
    cycle_held_prev_ = cycle_held;

    const bool was_reset_confirm = reset_confirm_;
    bool       pot_cancel_reset  = false;

    float norms[kMaxCycleRows];
    float steps[kMaxCycleRows];
    float travels[kMaxCycleRows];

    for(size_t i = 0; i < n_pots; ++i)
    {
        const PotMapping& map  = pot_mappings_[i];
        const float       norm = mux_.Get(map.chain, map.channel);
        norms[i]               = norm;

        if(!pot_prev_ok_[i])
        {
            pot_prev_[i]     = norm;
            pot_baseline_[i] = norm;
            pot_prev_ok_[i]  = true;
            steps[i]         = 0.f;
            travels[i]       = 0.f;
            continue;
        }

        steps[i]     = norm - pot_prev_[i];
        pot_prev_[i] = norm;
        travels[i]   = std::fabs(norm - pot_baseline_[i]);

        // Same-direction confirm: ignore micro-steps; reset accum on reverse.
        if(std::fabs(steps[i]) >= kOpenStepNoise)
        {
            const int8_t sign = (steps[i] > 0.f) ? 1 : -1;
            if(sign != open_dir_sign_[i])
            {
                open_dir_sign_[i]  = sign;
                open_dir_accum_[i] = 0.f;
            }
            open_dir_accum_[i] += std::fabs(steps[i]);
        }

        if(std::fabs(steps[i]) > kResetCancelThreshold)
            pot_cancel_reset = true;

        if(cycle_held
           && std::fabs(norm - scroll_anchor_[i]) >= 0.02f)
            pot_moved_during_hold_ = true;
    }

    if(screen_ == UiScreen::Dashboard || screen_ == UiScreen::MultiView)
    {
        // Exactly one winner — largest travel from baseline, but only if that
        // pot also shows a short burst of same-direction motion (filters slow
        // mux drift). From MultiView a deliberate pot turn leaves Multi.
        size_t best   = n_pots;
        float  best_t = 0.f;
        for(size_t i = 0; i < n_pots; ++i)
        {
            if(travels[i] < kOpenThreshold)
                continue;
            if(open_dir_accum_[i] < kOpenConfirmTravel)
                continue;
            if(travels[i] > best_t)
            {
                best_t = travels[i];
                best   = i;
            }
        }
        if(best < n_pots)
        {
            HandlePotTurn(best, norms[best], steps[best]);
            CapturePotBaselines();
        }
    }
    else if(screen_ == UiScreen::CycleView && active_row_ < n_pots)
    {
        // Cross-Block switch: another pot with stricter travel + direction
        // confirm steals focus (performance jump without idle timeout).
        size_t best   = n_pots;
        float  best_t = 0.f;
        for(size_t i = 0; i < n_pots; ++i)
        {
            if(i == active_row_)
                continue;
            if(travels[i] < kSwitchThreshold)
                continue;
            if(open_dir_accum_[i] < kSwitchConfirmTravel)
                continue;
            if(travels[i] > best_t)
            {
                best_t = travels[i];
                best   = i;
            }
        }
        if(best < n_pots)
        {
            HandlePotTurn(best, norms[best], steps[best]);
            CapturePotBaselines();
        }
        else
        {
            // Active Block only — ignores other pots below switch threshold.
            HandlePotTurn(active_row_, norms[active_row_], steps[active_row_]);
        }
    }

    HandleCycleButton(cycle_btn_.Poll());
    HandleImprintButton(imprint_btn_.Poll());

    // Trail short = Home while a Block/Multi menu is open; Lock only on Dashboard.
    // Long = Solo from any screen.
    trails_.SetShortPushHomes(screen_ == UiScreen::CycleView
                              || screen_ == UiScreen::MultiView);
    trails_.PollEncoders();
    trails_.Process();
    trails_.ApplyEncoderSteps();
    if(trails_.ConsumeHomeRequest())
        EnterDashboard();

    SyncEngines();

    if(was_reset_confirm
       && (pot_cancel_reset || trails_.LevelEditActivityThisFrame()))
        reset_confirm_ = false;

    if(reset_confirm_ && daisy::System::GetNow() >= reset_deadline_ms_)
        reset_confirm_ = false;

    const uint32_t idle = daisy::System::GetNow() - last_activity_ms_;
    if(!reset_confirm_ && screen_ != UiScreen::Dashboard && idle >= kInactivityMs)
        EnterDashboard();
}

void UiController::UpdateScreen()
{
    const bool  show_cpu = capture_params_->cpu_meter >= 0.5f;
    const bool  show_ram = capture_params_->ram_meter >= 0.5f;
    const float cpu_load
        = cpu_load_ != nullptr
              ? cpu_load_->load(std::memory_order_relaxed)
              : 0.f;
    // Clears itself as soon as the Swarm grain cap is back at maximum.
    const bool governor = swarm_ != nullptr && swarm_->GovernorActive();

    if(reset_confirm_ || screen_ == UiScreen::Dashboard)
    {
        uint32_t secs_left = 0;
        if(reset_confirm_ && daisy::System::GetNow() < reset_deadline_ms_)
        {
            secs_left
                = (reset_deadline_ms_ - daisy::System::GetNow() + 999) / 1000;
        }
        TrailLifeUi life[kTrailCount];
        capture_->GetTrailLifeUi(life);
        TrailSnapshot snaps[kTrailCount];
        for(size_t i = 0; i < kTrailCount; ++i)
            snaps[i] = trails_.Trail(i);
        size_t active = static_cast<size_t>(capture_params_->count + 0.5f);
        if(active < 1)
            active = 1;
        if(active > kTrailCount)
            active = kTrailCount;

        display_.DrawDashboard(playing_,
                               reset_confirm_,
                               secs_left,
                               trails_.RecTrailSlot(),
                               trails_.RecTrigActive(),
                               snaps,
                               capture_->InputLevel(),
                               capture_params_->threshold,
                               life,
                               active,
                               show_cpu,
                               show_ram,
                               cpu_load,
                               capture_->CrossfadeFocus(),
                               capture_->CrossfadeAmplitudeUi(),
                               governor,
                               static_cast<uint8_t>(
                                   capture_params_->rec_style + 0.5f));
    }
    else if(screen_ == UiScreen::MultiView && multi_row_ != nullptr)
    {
        const size_t col = multi_row_->BoundIndex();
        display_.DrawCycleView(
            *registry_, *multi_row_, col, -1.f, show_cpu, cpu_load, governor);
    }
    else
    {
        const CycleRow& row = rows_[active_row_];
        const size_t    col
            = row.InCycleScroll() ? row.ScrollIndex() : row.BoundIndex();
        display_.DrawCycleView(
            *registry_, row, col, -1.f, show_cpu, cpu_load, governor);
    }

    display_.Present();
}

void UiController::Process()
{
    PollControls();

    const uint32_t now = daisy::System::GetNow();
    if(last_display_ms_ == 0 || (now - last_display_ms_) >= kDisplayMinIntervalMs)
    {
        UpdateScreen();
        last_display_ms_ = now;
    }
    seed_->DelayMs(kLoopDelayMs);
}

} // namespace perseids
