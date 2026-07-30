#pragma once

#include "daisy_seed.h"
#include "hw_pins.h"

namespace perseids
{

// Pot poller on two CD74HC4067 mux chains using libDaisy's native ADC mux
// support (AdcChannelConfig::InitMux). libDaisy advances the shared select
// lines inside the ADC/DMA callback: each cached sample is guaranteed to be
// from the channel that was selected while it was converted.
//
// The previous hand-rolled select→settle→read state machine raced the
// free-running DMA ADC and produced cross-channel bleed (values jumping
// between two pots' positions). Do not reintroduce manual select switching.
class MuxAdcPoller
{
  public:
    static constexpr size_t kChains       = 2;
    static constexpr size_t kChannels     = 16;
    static constexpr size_t kPollChannels = 6; // C0–C5 (C5 = Multi Push on chain B)
    // EMA smoothing on top of the hardware-synced cache.
    static constexpr float kEmaAlphaIdle  = 0.15f;
    static constexpr float kEmaSnapThresh = 0.05f;

    void Init(daisy::DaisySeed& seed);

    // Call every main-loop iteration — never from the audio callback.
    void Process();

    float Get(size_t chain, size_t channel) const;
    float GetDelta(size_t chain, size_t channel) const;
    // Unsmoothed last sample — for digital mux buttons (Multi push).
    float GetRaw(size_t chain, size_t channel) const;

  private:
    void UpdateEma(size_t chain, size_t channel, float sample);

    daisy::AdcHandle adc_;
    daisy::GPIO      sel_hi_; // 4067 S3 held low (libDaisy drives S0–S2, C0–C7)
    float            ema_[kChains][kChannels];
    float            prev_ema_[kChains][kChannels];
    float            raw_[kChains][kChannels];
};

} // namespace perseids
