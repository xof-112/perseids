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
    swarm_           = &swarm;
    swarm_params_    = &swarm_params;
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

    for(size_t i = 0; i < kMaxCycleRows; ++i)
    {
        scroll_anchor_[i]  = 0.f;
        last_scroll_ms_[i] = 0;
        pot_prev_[i]       = 0.f;
        pot_baseline_[i]   = 0.f;
        pot_prev_ok_[i]    = false;
    }

    mux_.Init(seed);
    display_.Init(seed);
    cycle_btn_.Init(hw::kCycleButton, kLongPressMs);
    trails_.Init(seed, capture_);

    for(size_t n = 0; n < 48; ++n)
        mux_.Process();

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
    }
}

void UiController::SyncEngines()
{
    TrailMixerState mixer[TrailLevelController::kCount];
    trails_.FillMixerState(mixer);
    capture_->SyncFromUi(*capture_params_, mixer, playing_);
    spectra_->SyncFromUi(*spectra_params_);
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
            reset_confirm_ = false;
            playing_       = true;
            EnterDashboard();
        }
        return;
    }

    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        playing_ = !playing_;
        TouchActivity();
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

void UiController::HandlePotTurn(size_t row_idx, float pot_norm, float delta)
{
    if(row_idx >= row_count_)
        return;

    const bool opening_cycle = (screen_ == UiScreen::Dashboard);

    if(opening_cycle || std::fabs(delta) >= kEditThreshold)
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
        if(opening_cycle)
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

    const bool cycle_held = cycle_btn_.IsHeld();
    if(cycle_held && !cycle_held_prev_)
    {
        pot_moved_during_hold_ = false;
        for(size_t i = 0; i < n_pots; ++i)
        {
            const PotMapping& map = pot_mappings_[i];
            scroll_anchor_[i]     = mux_.Get(map.chain, map.channel);
            last_scroll_ms_[i]    = daisy::System::GetNow();
        }
    }

    if(cycle_held_prev_ && !cycle_held)
    {
        for(size_t i = 0; i < n_pots; ++i)
        {
            rows_[i].SetCycleScrollActive(false);
            rows_[i].CommitScrollBinding(*registry_);
        }
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

        if(std::fabs(steps[i]) > kResetCancelThreshold)
            pot_cancel_reset = true;

        if(cycle_held
           && std::fabs(norm - scroll_anchor_[i]) >= 0.02f)
            pot_moved_during_hold_ = true;
    }

    if(screen_ == UiScreen::Dashboard)
    {
        // Exactly one winner — largest travel from Dashboard baseline.
        size_t best   = n_pots;
        float  best_t = 0.f;
        for(size_t i = 0; i < n_pots; ++i)
        {
            if(travels[i] > best_t)
            {
                best_t = travels[i];
                best   = i;
            }
        }
        if(best < n_pots && best_t >= kOpenThreshold)
        {
            HandlePotTurn(best, norms[best], steps[best]);
            CapturePotBaselines();
        }
    }
    else if(active_row_ < n_pots)
    {
        // While a Block is open, ONLY its pot edits (others ignored until
        // idle returns to Dashboard). Drive it EVERY frame: pickup catch and
        // post-catch tracking need continuous samples — an edit threshold
        // here froze values after the catch (slow turns never exceeded it).
        // HandlePotTurn touches the idle timer only on real steps.
        HandlePotTurn(active_row_, norms[active_row_], steps[active_row_]);
    }

    HandleCycleButton(cycle_btn_.Poll());

    trails_.PollEncoders();
    trails_.Process();
    trails_.ApplyEncoderSteps();

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

    const uint32_t now = daisy::System::GetNow();
    if(last_display_ms_ == 0 || (now - last_display_ms_) >= kDisplayMinIntervalMs)
    {
        UpdateScreen();
        last_display_ms_ = now;
    }
    seed_->DelayMs(kLoopDelayMs);
}

} // namespace perseids
