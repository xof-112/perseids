#pragma once

#include "button_gesture.h"
#include "capture_engine.h"
#include "capture_params.h"
#include "daisy_seed.h"
#include "quadrature_encoder.h"

#include <cstddef>
#include <cstdint>

namespace perseids
{

// Five Trail Level push+encoder controls — Section 4.2.
class TrailLevelController
{
  public:
    static constexpr size_t kCount = kTrailCount;

    void Init(daisy::DaisySeed& seed, CaptureEngine* capture);

    void Process();

    void PollEncoders();
    void BeginFrame();
    void ApplyEncoderSteps();

    void ResetAll();

    bool ActivityThisFrame() const { return activity_this_frame_; }

    const TrailSnapshot& Trail(size_t index) const { return trails_[index]; }

    void FillMixerState(TrailMixerState out[kCount]) const;

    uint8_t RecTrailSlot() const;
    bool    RecTrigActive() const;

  private:
    void HandlePush(size_t index, ButtonGesture::Event event);
    void HandleEncoderStep(size_t index, int32_t steps);
    void OnRecTrig();

    CaptureEngine*    capture_;
    QuadratureEncoder encoders_[kCount];
    ButtonGesture     push_[kCount];
    ButtonGesture     rec_btn_;
    ButtonGesture     trig_in_;

    TrailSnapshot trails_[kCount];
    uint32_t      rec_flash_until_ms_;
    uint32_t      last_encoder_ms_[kCount];
    int32_t       pending_steps_[kCount];
    bool          activity_this_frame_;
};

} // namespace perseids
