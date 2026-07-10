#pragma once

#include "button_gesture.h"
#include "daisy_seed.h"
#include "quadrature_encoder.h"

#include <cstddef>
#include <cstdint>

namespace perseids
{

struct TrailSnapshot
{
    float level; // 0..1 dummy loudness
    bool  locked;
    bool  solo;
};

// Five Trail Level push+encoder controls — Section 4.2 (dummy values until Phase 3).
class TrailLevelController
{
  public:
    static constexpr size_t kCount = 5;

    void Init(daisy::DaisySeed& seed);

    void Process();

    // High-rate encoder sampling — call more than once per UI tick if needed.
    void PollEncoders();

    void BeginFrame();

    void ApplyEncoderSteps();

    // Phase 2 dummy — clears lock/solo/level; Phase 3 will wipe ring buffers.
    void ResetAll();

    bool ActivityThisFrame() const { return activity_this_frame_; }

    const TrailSnapshot& Trail(size_t index) const { return trails_[index]; }

    // Next trail slot for round-robin recording (1..5). Idle display shows this.
    uint8_t RecTrailSlot() const
    {
        return RecTrigActive() ? rec_pulse_slot_ : rec_trail_slot_;
    }
    bool     RecTrigActive() const;

  private:
    void HandlePush(size_t index, ButtonGesture::Event event);
    void HandleEncoderStep(size_t index, int32_t steps);
    void OnRecTrig();

    QuadratureEncoder encoders_[kCount];
    ButtonGesture     push_[kCount];
    ButtonGesture     rec_btn_;
    ButtonGesture     trig_in_;

    TrailSnapshot trails_[kCount];
    uint8_t       rec_trail_slot_;
    uint8_t       rec_pulse_slot_;
    uint32_t      rec_trig_until_ms_;
    uint32_t      last_encoder_ms_[kCount];
    int32_t       pending_steps_[kCount];
    bool          activity_this_frame_;
};

} // namespace perseids
