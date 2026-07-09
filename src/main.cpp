#include "daisy_seed.h"

using namespace daisy;

DaisySeed hw;

int main(void)
{
    hw.Init();

    while(true)
    {
        hw.SetLed(true);
        hw.DelayMs(2000);
        hw.SetLed(false);
        hw.DelayMs(2000);
    }
}