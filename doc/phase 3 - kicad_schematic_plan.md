# Perseids — KiCad Schematic Plan (Existing Hardware + Phase 3)

Reference for building the schematic capture in KiCad 10. Two sections: what already exists
(Phase 1–2, digital control hardware) and what's new for Phase 3 (analog audio I/O
conditioning). Pin names match `ARCHITECTURE.md`, Section 4.5a.

---

## Part 1 — Existing Control Hardware (Phase 1–2)

### 1.1 Bill of Materials

| Ref | Qty | Part | Notes |
|---|---|---|---|
| U2, U3 | 2 | CD74HC4067 | 16-channel analog mux/demux — **U2 = Mux A, U3 = Mux B** |
| ENC1–ENC5 | 5 | EC11 rotary encoder w/ push | Trail Level ×5 |
| SW1 | 1 | Tactile pushbutton | Cycle button |
| SW2 | 1 | Tactile pushbutton | Rec button |
| J1 | 1 | 3.5mm mono jack | Trig input |
| POT1–POT4 | 4 (of 10 planned) | Potentiometer, value TBD (typ. 10kΩ linear) | Block pots — **currently only 4 populated on the breadboard** (2 on Mux A/U2, 2 on Mux B/U3), full design calls for 10; value still **unconfirmed**. **For Phase 3, only 2 of these 4 are actually needed** — Block 1 pot and Block 2 pot (see 4.6: one pot per block, not per sub-parameter). POT3/POT4 stay unused for now. |
| — | — | *No external pull-up resistors* | STM32 internal pull-ups confirmed in firmware (`GPIO::Pull::PULLUP`) — do not add external 10k resistors on CLK/DT/SW/button lines unless you deliberately want the extra margin |

### 1.2 Net / Connection Table

| Signal | Driver (output) | Receiver (input) | Direction |
|---|---|---|---|
| Mux select S0 | Daisy Seed D0 | U2 pin S0 + U3 pin S0 (tied) | Daisy → Mux |
| Mux select S1 | Daisy Seed D1 | U2 pin S1 + U3 pin S1 (tied) | Daisy → Mux |
| Mux select S2 | Daisy Seed D2 | U2 pin S2 + U3 pin S2 (tied) | Daisy → Mux |
| Mux select S3 | Daisy Seed D3 | U2 pin S3 + U3 pin S3 (tied) | Daisy → Mux |
| Mux A common | U2 common (SIG) | Daisy Seed D15 (A0) | Mux → Daisy |
| Mux B common | U3 common (SIG) | Daisy Seed D16 (A1) | Mux → Daisy |
| Cycle button | SW1 (switch contact) | Daisy Seed D5 | Switch → Daisy |
| Rec button | SW2 (switch contact) | Daisy Seed D12 | Switch → Daisy |
| Trig input | J1 (external gear) | Daisy Seed D13 | External → Daisy |
| Trail 1 encoder CLK | ENC1 CLK (switch contact) | Daisy Seed D4 | Encoder → Daisy |
| Trail 1 encoder DT | ENC1 DT (switch contact) | Daisy Seed D21 | Encoder → Daisy |
| Trail 1 push | ENC1 SW (switch contact) | Daisy Seed D14 | Encoder → Daisy |
| Trail 2 encoder CLK | ENC2 CLK | Daisy Seed D22 | Encoder → Daisy |
| Trail 2 encoder DT | ENC2 DT | Daisy Seed D23 | Encoder → Daisy |
| Trail 2 push | ENC2 SW | Daisy Seed D17 | Encoder → Daisy |
| Trail 3 encoder CLK | ENC3 CLK | Daisy Seed D24 | Encoder → Daisy |
| Trail 3 encoder DT | ENC3 DT | Daisy Seed D25 | Encoder → Daisy |
| Trail 3 push | ENC3 SW | Daisy Seed D18 | Encoder → Daisy |
| Trail 4 encoder CLK | ENC4 CLK | Daisy Seed D26 | Encoder → Daisy |
| Trail 4 encoder DT | ENC4 DT | Daisy Seed D27 | Encoder → Daisy |
| Trail 4 push | ENC4 SW | Daisy Seed D19 | Encoder → Daisy |
| Trail 5 encoder CLK | ENC5 CLK | Daisy Seed D28 | Encoder → Daisy |
| Trail 5 encoder DT | ENC5 DT | Daisy Seed D29 | Encoder → Daisy |
| Trail 5 push | ENC5 SW | Daisy Seed D20 | Encoder → Daisy |
| OLED CS | Daisy Seed D7 | Display module | Daisy → OLED |
| OLED SCK | Daisy Seed D8 | Display module | Daisy → OLED |
| OLED DC | Daisy Seed D9 | Display module | Daisy → OLED |
| OLED MOSI | Daisy Seed D10 | Display module | Daisy → OLED |
| OLED RST | Daisy Seed D11 | Display module | Daisy → OLED |
| 4× block pot wipers (currently populated) | POT1–POT4 (wiper) | Mux A/U2 ch0–1 + Mux B/U3 ch0–1 (2 per mux) | Pot → Mux |
| — Phase 3 assignment | POT1 → Block 1 (Count/Threshold/Cont.Rec/On-Off); POT2 → Block 2 (Buffer/Hold/Fade In/Fade Out) | POT3, POT4 unused this phase | — |

**Free pins (not yet assigned):** D6, D30, D31, D32.

**Current breadboard status (as of this session):** Only 4 of the 10 planned block pots are
populated — 2 on Mux A/U2 (channels 0–1), 2 on Mux B/U3 (channels 0–1). Scope for now is
Phase 3 (Capture Engine) — the mod slot pots (Phase 10) aren't needed yet and are deliberately
left out of this plan; add them back in once that phase is actually next.

**Note on the mux SIG pins:** the CD74HC4067 itself is physically a bidirectional analog
switch (SIG can be driven from either side), but in this design it's only ever used
one-directionally — pot wiper in, ADC pin out. No signal is ever written back through it.
**Nothing in this entire control-hardware section is bidirectional** — every line here is a
one-way signal, either "Daisy drives it" (select lines, OLED SPI) or "external component
drives it, Daisy reads it" (pots via mux, encoders, buttons, Trig).

---

## Part 2 — Phase 3: Audio I/O Level Conditioning (new)

### 2.1 Bill of Materials

| Ref | Qty | Part | Notes |
|---|---|---|---|
| U1 | 1 | TL074 | Quad JFET op-amp — all 4 sections used (In L, In R, Out L, Out R) |
| J2, J3 | 2 | 3.5mm mono jack | Audio In L, Audio In R |
| J4, J5 | 2 | 3.5mm mono jack | Audio Out L, Audio Out R |
| R1, R2 | 2 | 100kΩ | Input attenuator, In L / In R |
| R3, R4 | 2 | 33kΩ | Input attenuator feedback, In L / In R |
| R5, R6 | 2 | 10kΩ | Output amplifier input, Out L / Out R |
| R7, R8 | 2 | 33kΩ | Output amplifier feedback, Out L / Out R |
| R9, R10 | 2 | 1kΩ | Output protection resistor, Out L / Out R |
| C1, C2 | 2 | 10µF electrolytic | DC block, In L / In R |
| C3, C4 | 2 | 10µF electrolytic | DC block, Out L / Out R |
| C5, C6 | 2 | 100nF ceramic | Power decoupling, V+ / V− near U1 |

### 2.2 Net / Connection Table

| Signal | Driver (output) | Receiver (input) | Direction |
|---|---|---|---|
| Audio In L (external) | J2 (Eurorack gear) | C1 → R1 → U1 pin 2 (−In A) | External → op-amp |
| Audio In L (conditioned) | U1 pin 1 (Out A) | Daisy Seed audio In L (codec) | Op-amp → Daisy |
| — feedback (In L stage) | U1 pin 1 (Out A) → R3 (33k) → U1 pin 2 | internal to op-amp stage | n/a (feedback loop) |
| — bias (In L stage) | GND | U1 pin 3 (+In A) | GND → op-amp |
| Audio In R (external) | J3 (Eurorack gear) | (10µF) → (100k) → U1 pin 6 (−In B) | External → op-amp |
| Audio In R (conditioned) | U1 pin 7 (Out B) | Daisy Seed audio In R (codec) | Op-amp → Daisy |
| — feedback (In R stage) | U1 pin 7 → (33k) → U1 pin 6 | internal to op-amp stage | n/a (feedback loop) |
| — bias (In R stage) | GND | U1 pin 5 (+In B) | GND → op-amp |
| Daisy audio Out L | Daisy Seed audio Out L (codec) | R5 (10k) → U1 pin 9 (−In C) | Daisy → op-amp |
| Audio Out L (external) | U1 pin 8 (Out C) → R9 (1k) → C3 (10µF) | J4 → Eurorack gear | Op-amp → external |
| — feedback (Out L stage) | U1 pin 8 → R7 (33k) → U1 pin 9 | internal to op-amp stage | n/a (feedback loop) |
| — bias (Out L stage) | GND | U1 pin 10 (+In C) | GND → op-amp |
| Daisy audio Out R | Daisy Seed audio Out R (codec) | (10k) → U1 pin 13 (−In D) | Daisy → op-amp |
| Audio Out R (external) | U1 pin 14 (Out D) → (1k) → (10µF) | J5 → Eurorack gear | Op-amp → external |
| — feedback (Out R stage) | U1 pin 14 → (33k) → U1 pin 13 | internal to op-amp stage | n/a (feedback loop) |
| — bias (Out R stage) | GND | U1 pin 12 (+In D) | GND → op-amp |
| Power V+ | Eurorack +12V rail | U1 pin 4, decoupled with C5 (100nF) to GND | Rail → IC (power in) |
| Power V− | Eurorack −12V rail | U1 pin 11, decoupled with C6 (100nF) to GND | Rail → IC (power in) |

**Note on power:** this is the first subsystem in the project that needs the ±12V Eurorack
rails, not just the Daisy Seed's own 3.3V/5V — wire that in before populating this stage.

**Note on direction:** every signal in this section is also strictly one-directional — audio
flows one way through each op-amp stage (in→out), never back. The "feedback" rows aren't
signal-direction feedback in the I/O sense, just the resistor loop that sets the op-amp's
gain — that loop is internal to each stage, not a separate external signal. **Nothing here is
bidirectional either.** The four op-amp stages are fully independent of each other (In L, In
R, Out L, Out R each use one dedicated section of the shared TL074, only the ±12V power pins
are actually shared between all four).

---

## Open items to confirm before finalizing the KiCad schematic

1. **Pot resistance value** (Block pots, Mod pots) — not specified anywhere in ARCHITECTURE.md.
   Confirm before ordering parts.
2. **Jack connector footprint** — 3.5mm vs 6.35mm, panel-mount vs PCB-mount, not yet decided.
3. **Mux chip package** (THT DIP-24 vs SMD SOIC-24) — affects footprint choice in KiCad.
