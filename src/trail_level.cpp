#include "trail_level.h"

#include "hw_pins.h"

namespace perseids
{

namespace
{
constexpr uint32_t kLongPressMs     = 800;
constexpr uint32_t kRecPulseMs      = 600;
constexpr float    kCoarseLevelStep = 0.02f;
constexpr float    kFineLevelStep   = 0.01f;
// TODO(audio): Re-tune fine/coarse steps once gain staging is finalized.
constexpr uint32_t kFineIntervalMs = 100;
constexpr int      kEncoderPolls   = 32;
// Lock out Rec/Trig re-fires (floating Trig used to spam every UI frame).
constexpr uint32_t kRecRetrigMs = 80;
} // namespace

void TrailLevelController::BeginFrame()
{
    activity_this_frame_   = false;
    level_edit_activity_   = false;
    for(size_t i = 0; i < kCount; ++i)
        pending_steps_[i] = 0;
}

void TrailLevelController::PollEncoders()
{
    for(size_t i = 0; i < kCount; ++i)
    {
        for(int n = 0; n < kEncoderPolls; ++n)
            encoders_[i].Debounce();
        pending_steps_[i] += encoders_[i].ConsumeSteps();
    }
}

void TrailLevelController::ApplyEncoderSteps()
{
    for(size_t i = 0; i < kCount; ++i)
        HandleEncoderStep(i, pending_steps_[i]);
}

void TrailLevelController::ResetAll()
{
    for(size_t i = 0; i < kCount; ++i)
    {
        trails_[i].level  = 0.5f;
        trails_[i].locked = false;
        trails_[i].solo   = false;
    }
    activity_this_frame_ = true;
    level_edit_activity_ = true;
    if(capture_)
        capture_->ClearAll();
}

void TrailLevelController::Init(daisy::DaisySeed& seed, CaptureEngine* capture)
{
    (void)seed;
    capture_             = capture;
    rec_flash_until_ms_  = 0;
    last_rec_trig_ms_    = 0;
    activity_this_frame_ = false;
    level_edit_activity_ = false;

    for(size_t i = 0; i < kCount; ++i)
    {
        trails_[i].level    = 0.5f;
        trails_[i].locked   = false;
        trails_[i].solo     = false;
        last_encoder_ms_[i] = 0;
        pending_steps_[i]   = 0;

        const hw::TrailEncoderPins& pins = hw::kTrailEncoders[i];
        encoders_[i].Init(pins.clk, pins.dt);
        push_[i].Init(hw::kTrailPushPins[i], kLongPressMs);
    }

    rec_btn_.Init(hw::kRecButton, kLongPressMs);
    trig_in_.Init(hw::kTrigInput, kLongPressMs);
}

void TrailLevelController::FillMixerState(TrailMixerState out[kCount]) const
{
    for(size_t i = 0; i < kCount; ++i)
    {
        out[i].level  = trails_[i].level;
        out[i].locked = trails_[i].locked;
        out[i].solo   = trails_[i].solo;
    }
}

uint8_t TrailLevelController::RecTrailSlot() const
{
    if(capture_)
        return capture_->RecTrailSlot();
    return 1;
}

bool TrailLevelController::RecTrigActive() const
{
    if(daisy::System::GetNow() < rec_flash_until_ms_)
        return true;
    return capture_ && capture_->RecActive();
}

void TrailLevelController::OnRecTrig()
{
    const uint32_t now = daisy::System::GetNow();
    if(now - last_rec_trig_ms_ < kRecRetrigMs)
        return;
    last_rec_trig_ms_    = now;
    rec_flash_until_ms_  = now + kRecPulseMs;
    activity_this_frame_ = true; // flash / UI only — not LevelEditActivity
    if(capture_)
        capture_->RequestManualTrigger();
}

void TrailLevelController::HandlePush(size_t index, ButtonGesture::Event event)
{
    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        trails_[index].locked = !trails_[index].locked;
        activity_this_frame_  = true;
        level_edit_activity_  = true;
        break;

    case ButtonGesture::Event::LongPress:
        trails_[index].solo = !trails_[index].solo;
        if(trails_[index].solo)
        {
            for(size_t i = 0; i < kCount; ++i)
            {
                if(i != index)
                    trails_[i].solo = false;
            }
        }
        activity_this_frame_ = true;
        level_edit_activity_ = true;
        break;

    default:
        break;
    }
}

void TrailLevelController::HandleEncoderStep(size_t index, int32_t steps)
{
    if(steps == 0)
        return;

    const uint32_t now       = daisy::System::GetNow();
    const uint32_t elapsed   = now - last_encoder_ms_[index];
    const float    step_size = (elapsed >= kFineIntervalMs) ? kFineLevelStep
                                                            : kCoarseLevelStep;

    last_encoder_ms_[index] = now;

    float level = trails_[index].level + static_cast<float>(steps) * step_size;
    if(level < 0.f)
        level = 0.f;
    if(level > 1.f)
        level = 1.f;

    trails_[index].level = level;
    activity_this_frame_ = true;
    // Single-step bounce is common on floating/noisy encoders — still nudge
    // level, but only clearer turns kick the UI back to Dashboard.
    if(steps >= 2 || steps <= -2)
        level_edit_activity_ = true;
}

void TrailLevelController::Process()
{
    for(size_t i = 0; i < kCount; ++i)
    {
        push_[i].Debounce();
        HandlePush(i, push_[i].Poll());
    }

    rec_btn_.Debounce();
    // Fire on press (RisingEdge). ShortPress-on-release alone missed quick taps
    // and differed from the Trig edge path.
    if(rec_btn_.RisingEdge())
        OnRecTrig();

    trig_in_.Debounce();
    // Hardware may present either polarity; accept either edge but rate-limit
    // so a floating jack cannot spam every frame (dashboard kick is separate).
    if(trig_in_.RisingEdge() || trig_in_.FallingEdge())
        OnRecTrig();
}

} // namespace perseids
