#include "mux_adc.h"

#include <cmath>

namespace perseids
{

void MuxAdcPoller::Init(daisy::DaisySeed& seed)
{
    (void)seed;

    for(size_t c = 0; c < kChains; ++c)
    {
        for(size_t ch = 0; ch < kChannels; ++ch)
        {
            ema_[c][ch]      = 0.f;
            prev_ema_[c][ch] = 0.f;
        }
    }

    // 4067 upper address lines stay low — libDaisy drives S0/S1 for C0–C2.
    sel_hi_[0].Init(hw::kMuxSel2, daisy::GPIO::Mode::OUTPUT);
    sel_hi_[1].Init(hw::kMuxSel3, daisy::GPIO::Mode::OUTPUT);
    sel_hi_[0].Write(false);
    sel_hi_[1].Write(false);

    daisy::AdcChannelConfig cfg[kChains];
    cfg[0].InitMux(hw::kMuxAdcA, kPollChannels, hw::kMuxSel0, hw::kMuxSel1);
    cfg[1].InitMux(hw::kMuxAdcB, kPollChannels, hw::kMuxSel0, hw::kMuxSel1);
    adc_.Init(cfg, kChains);
    adc_.Start();

    // Let the DMA scan visit every mux position a few times, then seed the
    // EMAs so boot values match the physical pots (no fake "turn" at start).
    daisy::System::Delay(10);
    for(size_t c = 0; c < kChains; ++c)
    {
        for(size_t ch = 0; ch < kPollChannels; ++ch)
        {
            const float v = adc_.GetMuxFloat(static_cast<uint8_t>(c),
                                             static_cast<uint8_t>(ch));
            ema_[c][ch]      = v;
            prev_ema_[c][ch] = v;
        }
    }
}

void MuxAdcPoller::UpdateEma(size_t chain, size_t channel, float sample)
{
    prev_ema_[chain][channel] = ema_[chain][channel];

    const float err = sample - ema_[chain][channel];
    if(std::fabs(err) > kEmaSnapThresh)
        ema_[chain][channel] = sample;
    else
        ema_[chain][channel]
            = kEmaAlphaIdle * sample + (1.f - kEmaAlphaIdle) * ema_[chain][channel];
}

void MuxAdcPoller::Process()
{
    for(size_t chain = 0; chain < kChains; ++chain)
    {
        for(size_t ch = 0; ch < kPollChannels; ++ch)
        {
            const float sample = adc_.GetMuxFloat(static_cast<uint8_t>(chain),
                                                  static_cast<uint8_t>(ch));
            UpdateEma(chain, ch, sample);
        }
    }
}

float MuxAdcPoller::Get(size_t chain, size_t channel) const
{
    if(chain >= kChains || channel >= kChannels)
        return 0.f;
    return ema_[chain][channel];
}

float MuxAdcPoller::GetDelta(size_t chain, size_t channel) const
{
    if(chain >= kChains || channel >= kChannels)
        return 0.f;
    return ema_[chain][channel] - prev_ema_[chain][channel];
}

} // namespace perseids
