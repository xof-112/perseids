#include "trail_level.h"

#include "hw_pins.h"

namespace perseids
{

namespace
{
constexpr uint32_t kLongPressMs     = 800;
constexpr uint32_t kRecPulseMs       = 600;
constexpr float    kCoarseLevelStep  = 0.02f;  // 2 % per detent — fast turn
constexpr float    kFineLevelStep    = 0.01f;  // 1 % per detent — slow turn
// TODO(audio): Re-tune fine/coarse steps once real Trail level gain is wired (Phase 3+).
constexpr uint32_t kFineIntervalMs   = 100;
constexpr int      kEncoderPolls     = 32;
}

void TrailLevelController::BeginFrame()
{
    activity_this_frame_ = false;
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
    rec_trail_slot_      = 1;
    rec_pulse_slot_      = 1;
    activity_this_frame_ = true;
}

void TrailLevelController::Init(daisy::DaisySeed& seed)
{
    (void)seed;

    rec_trail_slot_      = 1;
    rec_pulse_slot_      = 1;
    rec_trig_until_ms_   = 0;
    activity_this_frame_ = false;

    for(size_t i = 0; i < kCount; ++i)
    {
        trails_[i].level  = 0.5f;
        trails_[i].locked = false;
        trails_[i].solo   = false;
        last_encoder_ms_[i] = 0;
        pending_steps_[i]   = 0;

        const hw::TrailEncoderPins& pins = hw::kTrailEncoders[i];
        encoders_[i].Init(pins.clk, pins.dt);
        push_[i].Init(hw::kTrailPushPins[i], kLongPressMs);
    }

    rec_btn_.Init(hw::kRecButton, kLongPressMs);
    trig_in_.Init(hw::kTrigInput, kLongPressMs);
}

void TrailLevelController::OnRecTrig()
{
    rec_pulse_slot_      = rec_trail_slot_;
    rec_trig_until_ms_   = daisy::System::GetNow() + kRecPulseMs;
    activity_this_frame_ = true;
    rec_trail_slot_      = (rec_trail_slot_ % 5) + 1;
}

bool TrailLevelController::RecTrigActive() const
{
    return daisy::System::GetNow() < rec_trig_until_ms_;
}

void TrailLevelController::HandlePush(size_t index, ButtonGesture::Event event)
{
    switch(event)
    {
    case ButtonGesture::Event::ShortPress:
        trails_[index].locked = !trails_[index].locked;
        activity_this_frame_  = true;
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

    trails_[index].level   = level;
    activity_this_frame_ = true;
}

void TrailLevelController::Process()
{
    for(size_t i = 0; i < kCount; ++i)
    {
        push_[i].Debounce();
        HandlePush(i, push_[i].Poll());
    }

    rec_btn_.Debounce();
    if(rec_btn_.Poll() == ButtonGesture::Event::ShortPress)
        OnRecTrig();

    trig_in_.Debounce();
    if(trig_in_.Poll() == ButtonGesture::Event::ShortPress
       || trig_in_.RisingEdge() || trig_in_.FallingEdge())
        OnRecTrig();
}

} // namespace perseids
