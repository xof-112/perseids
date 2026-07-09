#pragma once

#include "daisy_seed.h"

namespace perseids
{

// Reusable debounced gesture detector for Cycle, Trail Level push, and Rec button.
class ButtonGesture
{
  public:
    enum class Event : uint8_t
    {
        None,
        ShortPress,
        LongPress,
    };

    void Init(daisy::Pin pin, uint32_t long_press_ms = 800);

    void Debounce();

    Event Poll();

    bool IsHeld() const { return switch_.Pressed(); }
    bool RisingEdge() const { return switch_.RisingEdge(); }
    bool FallingEdge() const { return switch_.FallingEdge(); }
    float HeldMs() const { return switch_.TimeHeldMs(); }

  private:
    daisy::Switch switch_;
    uint32_t      long_press_ms_;
    bool          long_fired_;
};

} // namespace perseids
