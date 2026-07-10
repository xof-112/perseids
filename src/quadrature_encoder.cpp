#include "quadrature_encoder.h"

namespace perseids
{

namespace
{
constexpr int8_t kQuadTable[16] = {
    0, -1, 1, 0, //
    1, 0, 0, -1, //
    -1, 0, 0, 1, //
    0, 1, -1, 0, //
};
}

void QuadratureEncoder::Init(daisy::Pin clk, daisy::Pin dt)
{
    accum_   = 0;
    last_ab_ = 0xff;

    hw_clk_.Init(clk, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
    hw_dt_.Init(dt, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);

    const uint8_t a = hw_clk_.Read() ? 2u : 0u;
    const uint8_t b = hw_dt_.Read() ? 1u : 0u;
    last_ab_        = static_cast<uint8_t>(a | b);
}

void QuadratureEncoder::Debounce()
{
    const uint8_t a  = hw_clk_.Read() ? 2u : 0u;
    const uint8_t b  = hw_dt_.Read() ? 1u : 0u;
    const uint8_t ab = static_cast<uint8_t>(a | b);

    if(ab == last_ab_)
        return;

    const uint8_t idx
        = static_cast<uint8_t>((last_ab_ << 2) | (ab & 0x03));
    last_ab_ = ab;

    const int8_t delta = kQuadTable[idx & 0x0f];
    if(delta != 0)
        accum_ += delta;
}

int32_t QuadratureEncoder::ConsumeSteps()
{
    const int32_t steps = accum_;
    accum_              = 0;
    return steps;
}

} // namespace perseids
