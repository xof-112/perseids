#include "mux_adc.h"

#include <cmath>

namespace perseids
{

namespace
{
enum State
{
    kSetSelect = 0,
    kWaitSettle,
    kRead,
};
}

void MuxAdcPoller::Init(daisy::DaisySeed& seed)
{
    current_ch_         = 0;
    state_              = kSetSelect;
    settle_deadline_us_ = 0;

    for(size_t c = 0; c < kChains; ++c)
    {
        for(size_t ch = 0; ch < kChannels; ++ch)
        {
            ema_[c][ch]      = 0.f;
            prev_ema_[c][ch] = 0.f;
        }
    }

    sel_[0].Init(hw::kMuxSel0, daisy::GPIO::Mode::OUTPUT);
    sel_[1].Init(hw::kMuxSel1, daisy::GPIO::Mode::OUTPUT);
    sel_[2].Init(hw::kMuxSel2, daisy::GPIO::Mode::OUTPUT);
    sel_[3].Init(hw::kMuxSel3, daisy::GPIO::Mode::OUTPUT);

    daisy::AdcChannelConfig cfg[kChains];
    cfg[0].InitSingle(hw::kMuxAdcA);
    cfg[1].InitSingle(hw::kMuxAdcB);
    adc_chain_idx_[0] = 0;
    adc_chain_idx_[1] = 1;
    adc_.Init(cfg, kChains);
    adc_.Start();

    SetSelect(0);
}

void MuxAdcPoller::SetSelect(uint8_t channel)
{
    sel_[0].Write((channel >> 0) & 1);
    sel_[1].Write((channel >> 1) & 1);
    sel_[2].Write((channel >> 2) & 1);
    sel_[3].Write((channel >> 3) & 1);
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
    const uint32_t now = daisy::System::GetUs();

    switch(state_)
    {
    case kSetSelect:
        SetSelect(current_ch_);
        settle_deadline_us_ = now + kSettleUs;
        state_              = kWaitSettle;
        break;

    case kWaitSettle:
        if(now >= settle_deadline_us_)
            state_ = kRead;
        break;

    case kRead:
        for(size_t chain = 0; chain < kChains; ++chain)
        {
            const float sample = adc_.GetFloat(adc_chain_idx_[chain]);
            UpdateEma(chain, current_ch_, sample);
        }
        current_ch_ = (current_ch_ + 1) % kChannels;
        state_      = kSetSelect;
        break;
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
