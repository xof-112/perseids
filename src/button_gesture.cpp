#include "button_gesture.h"

namespace perseids
{

void ButtonGesture::Init(daisy::Pin pin, uint32_t long_press_ms)
{
    long_press_ms_ = long_press_ms;
    long_fired_    = false;
    switch_.Init(pin,
                 0.f,
                 daisy::Switch::TYPE_MOMENTARY,
                 daisy::Switch::POLARITY_INVERTED,
                 daisy::GPIO::Pull::PULLUP);
}

void ButtonGesture::Debounce()
{
    if(switch_.RisingEdge())
        long_fired_ = false;
    switch_.Debounce();
}

ButtonGesture::Event ButtonGesture::Poll()
{
    if(switch_.Pressed() && !long_fired_
       && static_cast<uint32_t>(switch_.TimeHeldMs()) >= long_press_ms_)
    {
        long_fired_ = true;
        return Event::LongPress;
    }

    if(switch_.FallingEdge() && !long_fired_)
        return Event::ShortPress;

    return Event::None;
}

} // namespace perseids
