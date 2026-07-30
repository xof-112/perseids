#pragma once

#include <cstdint>

namespace perseids
{

// Block 11 — Multi encoder cycle (ARCHITECTURE 4.1). Dry/Wet is live;
// Macro1/2, Time Unit, Settings are UI stubs until Phase 11 finishes them.
enum MultiParamId : uint16_t
{
    kMultiDryWet   = 114,
    kMultiMacro1   = 115,
    kMultiMacro2   = 116,
    kMultiTimeUnit = 117,
    kMultiSettings = 118,
};

struct MultiParamValues
{
    // 0 = clean input (listen-through), 1 = full wet bus (engines+reso+filter+reverb).
    float dry_wet = 0.55f;
    // Dummies — stored/display only until macros / clock sync / Settings submenu.
    float macro1    = 0.50f;
    float macro2    = 0.50f;
    float time_unit = 0.f; // 0 = Sec, 1 = Clk
    float settings  = 0.f; // stub slot (single "···" label)
};

} // namespace perseids
