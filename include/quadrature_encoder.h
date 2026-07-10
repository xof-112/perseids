#pragma once

#include "daisy_seed.h"

namespace perseids
{

// Two-pin quadrature decoder (CLK/DT) — no click pin; push handled separately.
class QuadratureEncoder
{
  public:
    void Init(daisy::Pin clk, daisy::Pin dt);

    void Debounce();

    int32_t ConsumeSteps();

  private:
    daisy::GPIO hw_clk_;
    daisy::GPIO hw_dt_;
    uint8_t     last_ab_;
    int32_t     accum_;
};

} // namespace perseids
