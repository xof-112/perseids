#pragma once

#include "daisy_seed.h"

// Carrier PCB pin map — placeholders until the final schematic is locked.
// All assignments live here so later phases only update one file.

namespace perseids
{
namespace hw
{

// CD74HC4067 mux select lines (shared across both chains)
constexpr daisy::Pin kMuxSel0 = daisy::seed::D0;
constexpr daisy::Pin kMuxSel1 = daisy::seed::D1;
constexpr daisy::Pin kMuxSel2 = daisy::seed::D2;
constexpr daisy::Pin kMuxSel3 = daisy::seed::D3;

// One ADC input per mux chain (SIG pin of each 4067)
constexpr daisy::Pin kMuxAdcA = daisy::seed::A0;
constexpr daisy::Pin kMuxAdcB = daisy::seed::A1;

// Phase 1 test wiring: 2 pots per mux chain
//   Mux A C0, C1  →  UI rows D1, D2
//   Mux B C0, C1  →  UI row D3 + spare mux test on dashboard
constexpr uint8_t kMuxChainA = 0;
constexpr uint8_t kMuxChainB = 1;

constexpr uint8_t kPotMuxA0 = 0;
constexpr uint8_t kPotMuxA1 = 1;
constexpr uint8_t kPotMuxB0 = 0;
constexpr uint8_t kPotMuxB1 = 1;

// Legacy aliases
constexpr uint8_t kPotRow0 = kPotMuxA0;
constexpr uint8_t kPotRow1 = kPotMuxA1;
constexpr uint8_t kPotRow2 = kPotMuxB0;

// Cycle button (momentary, active low with internal pull-up)
constexpr daisy::Pin kCycleButton = daisy::seed::D5;

// SSD1309 128×64 OLED — libDaisy SPI defaults match Daisy Seed breakout pins
constexpr daisy::Pin kOledDc    = daisy::seed::D9;
constexpr daisy::Pin kOledReset = daisy::seed::D30;

} // namespace hw

struct PotMapping
{
    uint8_t chain;
    uint8_t channel;
};

} // namespace perseids
