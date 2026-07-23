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

// Phase 1+ test wiring: pots on mux chains
//   Mux A C0, C1, C2  →  Trails, Time, Engines
//   Mux B C0, C1      →  Spectra, Swarm
// Settings CycleRow exists; map B2 only when a 6th pot is wired.
constexpr uint8_t kMuxChainA = 0;
constexpr uint8_t kMuxChainB = 1;

constexpr uint8_t kPotMuxA0 = 0;
constexpr uint8_t kPotMuxA1 = 1;
constexpr uint8_t kPotMuxA2 = 2;
constexpr uint8_t kPotMuxB0 = 0;
constexpr uint8_t kPotMuxB1 = 1;
constexpr uint8_t kPotMuxB2 = 2;

// Phase 2 — Trail Level rotary encoders (quadrature, NOT mux ADC).
// Each encoder: common/GND to ground, CLK + DT to GPIO (internal pull-up).
// Mux A C2..C6 is unused for trail level — encoders need digital phase pins.
struct TrailEncoderPins
{
    daisy::Pin clk; // Phase A
    daisy::Pin dt;  // Phase B
};

constexpr TrailEncoderPins kTrailEncoders[5] = {
    {daisy::seed::D4, daisy::seed::D21},
    {daisy::seed::D22, daisy::seed::D23},
    {daisy::seed::D24, daisy::seed::D25},
    {daisy::seed::D26, daisy::seed::D27},
    {daisy::seed::D28, daisy::seed::D29},
};

// SSD1309 2.42" 128×64 — SPI1 (matches libDaisy SSD130x4WireSpi defaults).
// VCC → 3v3 (pin 38), GND → DGND (pin 40). D6 is free (not used by display).
//   SCK  D8  (pin 9)   SCL / SPI1 SCK
//   MOSI D10 (pin 11)  SDA / SPI1 MOSI
//   CS   D7  (pin 8)   SPI1 NSS
//   DC   D9  (pin 10)
//   RST  D11 (pin 12)  RES
constexpr daisy::Pin kOledSck    = daisy::seed::D8;
constexpr daisy::Pin kOledMosi   = daisy::seed::D10;
constexpr daisy::Pin kOledCs     = daisy::seed::D7;
constexpr daisy::Pin kOledDc     = daisy::seed::D9;
constexpr daisy::Pin kOledReset  = daisy::seed::D11;
// Trail Level push switches (momentary, active low, pull-up).
// Pins avoid display SPI and mux ADC (A0/A1 = D15/D16).
constexpr daisy::Pin kTrailPush0 = daisy::seed::D14;
constexpr daisy::Pin kTrailPush1 = daisy::seed::D17;
constexpr daisy::Pin kTrailPush2 = daisy::seed::D18;
constexpr daisy::Pin kTrailPush3 = daisy::seed::D19;
constexpr daisy::Pin kTrailPush4 = daisy::seed::D20;

// Rec panel button — momentary, parallel to Trig jack (Section 4.5).
constexpr daisy::Pin kRecButton = daisy::seed::D12;

// Trig jack — 3.5 mm mono trigger input, one GPIO only (Section 4.4/4.5).
// Carrier PCB: tip = signal, sleeve = GND; conditioning (e.g. comparator/schmitt)
// presents a digital rising edge to this pin — same record trigger as kRecButton.
constexpr daisy::Pin kTrigInput = daisy::seed::D13;

constexpr daisy::Pin kTrailPushPins[5]
    = {kTrailPush0, kTrailPush1, kTrailPush2, kTrailPush3, kTrailPush4};

// Legacy aliases
constexpr uint8_t kPotRow0 = kPotMuxA0;
constexpr uint8_t kPotRow1 = kPotMuxA1;
constexpr uint8_t kPotRow2 = kPotMuxA2;
constexpr uint8_t kPotRow3 = kPotMuxB0;
constexpr uint8_t kPotRow4 = kPotMuxB1;

// Cycle button (momentary, active low with internal pull-up)
constexpr daisy::Pin kCycleButton = daisy::seed::D5;

} // namespace hw

struct PotMapping
{
    uint8_t chain;
    uint8_t channel;
};

} // namespace perseids
