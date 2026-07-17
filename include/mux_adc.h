#pragma once

#include "daisy_seed.h"
#include "hw_pins.h"

namespace perseids
{

// Non-blocking round-robin poller for two CD74HC4067 mux chains with EMA smoothing.
// One channel is serviced per Process() call (Section 2.6).
class MuxAdcPoller
{
  public:
    static constexpr size_t kChains    = 2;
    static constexpr size_t kChannels  = 16;
    static constexpr float  kEmaAlphaIdle  = 0.18f;  // heavier idle filter vs mux noise
    static constexpr float  kEmaSnapThresh = 0.018f; // only snap on real pot moves
    static constexpr uint32_t kSettleUs = 80;

    void Init(daisy::DaisySeed& seed);

    // Call every main-loop iteration — never from the audio callback.
    void Process();

    float Get(size_t chain, size_t channel) const;
    float GetDelta(size_t chain, size_t channel) const;

  private:
    void SetSelect(uint8_t channel);
    void UpdateEma(size_t chain, size_t channel, float sample);

    daisy::AdcHandle adc_;
    daisy::GPIO      sel_[4];
    uint8_t          adc_chain_idx_[kChains];
    float            ema_[kChains][kChannels];
    float            prev_ema_[kChains][kChannels];
    uint8_t          current_ch_;
    uint8_t          state_;
    uint32_t         settle_deadline_us_;
};

} // namespace perseids
