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
                        std::atomic<float>* cpu_load)
{
    seed_            = &seed;
    registry_        = &registry;
    rows_            = rows;
    row_count_       = row_count;
    pot_mappings_    = pot_mappings;
    pot_count_       = pot_count;
    capture_         = &capture;
    capture_params_  = &capture_params;
    spectra_         = &spectra;
    spectra_params_  = &spectra_params;
    cpu_load_        = cpu_load;

    screen_                  = UiScreen::Dashboard;
    active_row_              = 0;
    playing_                 = true;
    reset_confirm_           = false;
    reset_deadline_ms_       = 0;
    last_activity_ms_        = daisy::System::GetNow();
    cycle_held_prev_         = false;
    pot_moved_during_hold_   = false;

    for(size_t i = 0; i < kMaxCycleRows; ++i)
    {
        scroll_anchor_[i]  = 0.f;
        last_scroll_ms_[i] = 0;
    }

    mux_.Init(seed);
    display_.Init(seed);
    cycle_btn_.Init(hw::kCycleButton, kLongPressMs);
    trails_.Init(seed, capture_);

    mux_.Process();
    for(size_t i = 0; i < row_count_; ++i)
    {
        const PotMapping& map = pot_mappings_[i];
        rows_[i].UpdatePotPosition(mux_.Get(map.chain, map.channel));
        rows_[i].InitPickup(registry);
    }

    SyncEngines();
}

void UiController::SyncEngines()
{
    TrailMixerState mixer[TrailLevelController::kCount];
    trails_.FillMixerState(mixer);
    capture_->SyncFromUi(*capture_params_, mixer, playing_);
    spectra_->SyncFromUi(*spectra_params_);
}

void UiController::TouchActivity()
{
    last_activity_ms_ = daisy::System::GetNow();
}

void UiController::HandleCycleButton(ButtonGesture::Event event)
{
    if(reset_confirm_)
    {
        if(event == ButtonGesture::Event::ShortPress)
        {
            trails_.ResetAll();
            reset_confirm_ = false;
            playing_       = true;
            screen_        = UiScreen::Dashboard;
            TouchActivity();
        }
        return;
    }

    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        playing_ = !playing_;
        screen_  = UiScreen::Dashboard;
        TouchActivity();
        break;

    case ButtonGesture::Event::LongPress:
        if(!pot_moved_during_hold_)
        {
            reset_confirm_     = true;
            screen_            = UiScreen::Dashboard;
            reset_deadline_ms_ = daisy::System::GetNow() + kResetConfirmMs;
            TouchActivity();
        }
        break;

    default:
        break;
    }
}

void UiController::HandlePotTurn(size_t row_idx, float pot_norm, float delta)
{
    if(row_idx >= row_count_)
        return;

    const bool opening_cycle = (screen_ == UiScreen::Dashboard);

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
            }
            else if(moved <= -kScrollStepThreshold)
            {
                row.Scroll(-1);
                scroll_anchor_[row_idx] -= kScrollStepThreshold;
                last_scroll_ms_[row_idx] = now;
            }
        }
    }
    else
    {
        // One-shot catch-up when entering CycleView from Dashboard (4.6).
        if(opening_cycle)
            row.ArmPickupIfNeeded(*registry_);
        row.SetCycleScrollActive(false);
        row.ChangeValue(*registry_, pot_norm);
    }
}

void UiController::PollControls()
{
    trails_.BeginFrame();

    mux_.Process();

    for(size_t i = 0; i < row_count_; ++i)
    {
        const PotMapping& map = pot_mappings_[i];
        rows_[i].UpdatePotPosition(mux_.Get(map.chain, map.channel));
    }

    cycle_btn_.Debounce();

    const bool cycle_held = cycle_btn_.IsHeld();
    if(cycle_held && !cycle_held_prev_)
    {
        pot_moved_during_hold_ = false;
        for(size_t i = 0; i < row_count_; ++i)
        {
            const PotMapping& map = pot_mappings_[i];
            scroll_anchor_[i]     = mux_.Get(map.chain, map.channel);
            last_scroll_ms_[i]    = daisy::System::GetNow();
        }
    }

    if(cycle_held_prev_ && !cycle_held)
    {
        for(size_t i = 0; i < row_count_; ++i)
        {
            rows_[i].SetCycleScrollActive(false);
            rows_[i].CommitScrollBinding(*registry_);
        }
    }
    cycle_held_prev_ = cycle_held;

    const bool was_reset_confirm = reset_confirm_;
    bool       pot_cancel_reset  = false;

    for(size_t i = 0; i < pot_count_; ++i)
    {
        const PotMapping& map   = pot_mappings_[i];
        const float       norm  = mux_.Get(map.chain, map.channel);
        const float       delta = mux_.GetDelta(map.chain, map.channel);

        if(std::fabs(delta) > kResetCancelThreshold)
            pot_cancel_reset = true;

        if(cycle_held && i < row_count_)
        {
            const float moved = norm - scroll_anchor_[i];
            if(std::fabs(moved) >= kScrollStepThreshold * 0.5f)
                pot_moved_during_hold_ = true;
        }

        if(std::fabs(delta) > kTurnThreshold && i < row_count_)
        {
            // From Dashboard, require a clearer turn so mux noise doesn't pop CycleView.
            if(screen_ == UiScreen::Dashboard
               && std::fabs(delta) < kOpenCycleThreshold)
                continue;
            HandlePotTurn(i, norm, delta);
        }
    }

    HandleCycleButton(cycle_btn_.Poll());

    trails_.PollEncoders();
    trails_.Process();
    trails_.ApplyEncoderSteps();

    SyncEngines();

    if(trails_.ActivityThisFrame() && !cycle_held)
    {
        TouchActivity();
        screen_ = UiScreen::Dashboard;
    }

    if(was_reset_confirm && (pot_cancel_reset || trails_.ActivityThisFrame()))
        reset_confirm_ = false;

    if(reset_confirm_ && daisy::System::GetNow() >= reset_deadline_ms_)
        reset_confirm_ = false;

    const uint32_t idle = daisy::System::GetNow() - last_activity_ms_;
    if(!reset_confirm_ && idle >= kInactivityMs)
        screen_ = UiScreen::Dashboard;
}

void UiController::UpdateScreen()
{
    const bool  show_cpu = capture_params_->cpu_meter >= 0.5f;
    const bool  show_ram = capture_params_->ram_meter >= 0.5f;
    const float cpu_load
        = cpu_load_ != nullptr
              ? cpu_load_->load(std::memory_order_relaxed)
              : 0.f;

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
                               cpu_load);
    }
    else
    {
        const CycleRow& row = rows_[active_row_];
        const size_t    col
            = row.InCycleScroll() ? row.ScrollIndex() : row.BoundIndex();
        display_.DrawCycleView(*registry_, row, col, -1.f, show_cpu, cpu_load);
    }

    display_.Present();
}

void UiController::Process()
{
    PollControls();
    UpdateScreen();
    seed_->DelayMs(kLoopDelayMs);
}

} // namespace perseids
