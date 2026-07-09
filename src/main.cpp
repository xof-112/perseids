#include "daisy_seed.h"

#include "cycle_row.h"
#include "hw_pins.h"
#include "param_registry.h"
#include "ui_controller.h"

using namespace daisy;

namespace
{

enum ParamId : uint16_t
{
    // Dummy block 1 — unipolar (4 params, like most blocks)
    kD1Level = 1,
    kD1Width,
    kD1Depth,
    kD1Send,

    // Dummy block 2 — toggle
    kD2On,
    kD2Mode,
    kD2Sync,
    kD2Link,

    // Dummy block 3 — bipolar
    kD3Macro,
    kD3Vel,
    kD3Pan,
    kD3Tilt,
};

float g_d1_level = 0.5f;
float g_d1_width = 0.35f;
float g_d1_depth = 0.7f;
float g_d1_send  = 0.25f;

float g_d2_on   = 1.f;
float g_d2_mode = 0.f;
float g_d2_sync = 1.f;
float g_d2_link = 0.f;

float g_d3_macro = 0.5f;
float g_d3_vel   = 0.4f;
float g_d3_pan   = 0.55f;
float g_d3_tilt  = 0.48f;

const uint16_t kD1Ids[] = {kD1Level, kD1Width, kD1Depth, kD1Send};
const uint16_t kD2Ids[] = {kD2On, kD2Mode, kD2Sync, kD2Link};
const uint16_t kD3Ids[] = {kD3Macro, kD3Vel, kD3Pan, kD3Tilt};

const perseids::PotMapping kPotMappings[] = {
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA0}, // Pot 1 → D1
    {perseids::hw::kMuxChainA, perseids::hw::kPotMuxA1}, // Pot 2 → D2
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB0}, // Pot 3 → D3
    {perseids::hw::kMuxChainB, perseids::hw::kPotMuxB1}, // Pot 4 → mux B test
};

bool RegisterDummyParams(perseids::ParameterRegistry& reg)
{
    const perseids::ParameterDef defs[] = {
        {kD1Level, "Level", "LVL", 0.f, 1.f, 0.5f, &g_d1_level, perseids::ParamDisplayType::Unipolar, false},
        {kD1Width, "Width", "WID", 0.f, 1.f, 0.35f, &g_d1_width, perseids::ParamDisplayType::Unipolar, false},
        {kD1Depth, "Depth", "DEP", 0.f, 1.f, 0.7f, &g_d1_depth, perseids::ParamDisplayType::Unipolar, false},
        {kD1Send, "Send", "SND", 0.f, 1.f, 0.25f, &g_d1_send, perseids::ParamDisplayType::Unipolar, false},

        {kD2On, "On/Off", "ON", 0.f, 1.f, 1.f, &g_d2_on, perseids::ParamDisplayType::Toggle, false},
        {kD2Mode, "Mode A/B", "MOD", 0.f, 1.f, 0.f, &g_d2_mode, perseids::ParamDisplayType::Toggle, false},
        {kD2Sync, "Sync", "SYN", 0.f, 1.f, 1.f, &g_d2_sync, perseids::ParamDisplayType::Toggle, false},
        {kD2Link, "Link", "LNK", 0.f, 1.f, 0.f, &g_d2_link, perseids::ParamDisplayType::Toggle, false},

        {kD3Macro, "Macro", "MAC", 0.f, 1.f, 0.5f, &g_d3_macro, perseids::ParamDisplayType::Bipolar, true},
        {kD3Vel, "Velocity", "VEL", 0.f, 1.f, 0.4f, &g_d3_vel, perseids::ParamDisplayType::Bipolar, true},
        {kD3Pan, "Pan", "PAN", 0.f, 1.f, 0.55f, &g_d3_pan, perseids::ParamDisplayType::Bipolar, true},
        {kD3Tilt, "Tilt", "TLT", 0.f, 1.f, 0.48f, &g_d3_tilt, perseids::ParamDisplayType::Bipolar, true},
    };

    for(const auto& def : defs)
    {
        if(!reg.Register(def))
            return false;
    }
    return true;
}

} // namespace

DaisySeed hw;

int main(void)
{
    hw.Init();
    hw.SetLed(true);

    perseids::ParameterRegistry registry;
    RegisterDummyParams(registry);

    perseids::CycleRow rows[] = {
        perseids::CycleRow("D1 Mix", kD1Ids, 4),
        perseids::CycleRow("D2 Tog", kD2Ids, 4),
        perseids::CycleRow("D3 Bi", kD3Ids, 4),
    };

    perseids::UiController ui;
    ui.Init(hw,
            registry,
            rows,
            sizeof(rows) / sizeof(rows[0]),
            kPotMappings,
            sizeof(kPotMappings) / sizeof(kPotMappings[0]));

    while(true)
        ui.Process();
}
