# Perseids — Architecture & Development Plan (Master Reference)

> This document is the **single source of truth** for the AI-assisted development of this
> project. Implementations must never violate the principles, hardware constraints, or phase
> sequence defined here.

**Concept:** Inspired by the Coastline plugin (by Aqeel Aadam Sound), but independently
designed and developed from scratch: a global sound body (two resynthesis engines —
**Spectra** additive & **Swarm** granular — plus reverb, filter, and spectral resonator)
processes up to 5 simultaneous audio voices ("**Trails**") in a round-robin pool. DSP
processing is deliberately **global and pre-fader**; each Trail only gets a lightweight mixer
tap (Level/Lock/Solo) — this keeps the UI lean.

---

## 0. Cursor IDE Setup

Create a `.cursor/rules/` directory in the project root with at least one file
`architecture.mdc` (modern, recommended 2026 convention — the old flat `.cursorrules` file
still works but is considered deprecated). Frontmatter example:

```
---
description: Perseids Firmware Architecture
alwaysApply: true
---
Read ARCHITECTURE.md in the project root before every code suggestion. Respect the hardware
constraints of the Electrosmith Daisy Seed and the C++/DSP guardrails in Section 2. Write
exclusively embedded ARM Cortex-M7 code (libDaisy/DaisySP), never generic desktop C++ code.
```

**Project structure (standard PlatformIO convention):**

```
Project-Root/
├── platformio.ini          ← board config (electrosmith_daisy), build flags
├── src/
│   └── main.cpp             ← entry point, firmware code lives here
├── include/                 ← optional custom headers
├── lib/                      ← optional custom libraries
├── .cursor/
│   └── rules/
│       └── architecture.mdc
└── ARCHITECTURE.md
```

`main.cpp` lives in `src/`, directly next to `platformio.ini` in the project root — this is
not a Daisy Seed or libDaisy quirk, but a PlatformIO convention in general, since the build
process expects the entry point there by default. The skeleton (empty `src/`, `include/`,
`lib/`, `test/` plus `platformio.ini`) is created automatically by `pio project init --board
electrosmith_daisy` — the actual firmware code in `main.cpp` as well as the libDaisy/DaisySP
linkage (`lib_deps` in `platformio.ini`) still need to be written afterward; Cursor handles
that per the Phase 0 prompt.

**Reality check, PlatformIO + libDaisy:** The board ID `electrosmith_daisy` is officially
registered in PlatformIO, but libDaisy/DaisySP themselves are **not** officially maintained in
the PlatformIO registry — they're pulled in via `lib_deps` as direct GitHub references and end
up in a hidden `.pio/libdeps/` folder during the build (not in the visible `lib/`). The
Electro-Smith forum has recurring reports of build issues with this approach — so it's
possible that the very first build in Phase 0 won't go through cleanly and will need
adjustments to the `lib_deps` entries or compiler flags. That wouldn't be a mistake on your
part, just a known friction point of this combination. If PlatformIO doesn't cooperate at all,
the official fallback is the Makefile-based Daisy toolchain (libDaisy/DaisySP as git
submodules, `make` instead of `pio`) — more battle-tested, but without PlatformIO's comfort.

Keep this rule under 200 words (token cost with `alwaysApply: true`, since it's loaded on
every request).

---

## 1. Target Platform

- **MCU/Board:** Electrosmith Daisy Seed (STM32H750, 480 MHz Cortex-M7, 64 MB SDRAM)
- **Framework:** libDaisy (hardware abstraction) + DaisySP (DSP building blocks)
- **Language:** C++ (PlatformIO or Daisy's own CMake toolchain setup)
- **Display:** SSD1309 OLED, 2.42″, 128×64 px (SPI preferred for high frame rate)
- **Custom carrier PCB** for connecting all pots/encoders/buttons/jacks

---

## 2. Critical C++/DSP Guardrails (mandatory, non-negotiable)

1. **Strict separation of audio callback / UI thread.** The audio callback runs as a
   high-priority interrupt and must never block (no `System::Delay()`, no display update, no
   string formatting, no logging). Communication between the UI thread and the audio callback
   only via `std::atomic` or lock-free ring buffers.
2. **SDRAM mandatory for large buffers.** The 5 Trail ring buffers must live in the external
   64 MB SDRAM: `DSY_SDRAM_BSS float trail_buffer[5][BUFFER_SIZE];`. Never use `new`,
   `malloc`, `std::vector`, or other heap allocations inside the audio callback.
   **`BUFFER_SIZE` decided:** 30 seconds max per Trail at 48kHz mono float →
   `BUFFER_SIZE = 1,440,000` samples (30 × 48,000). Total across all 5 Trails: 1,440,000 × 5 ×
   4 bytes ≈ 28.8 MB, roughly 45% of the 64MB SDRAM — leaves comfortable headroom (~35MB) for
   the reverb tank, Spectral Resonator buffers, FFT scratch space, and anything else that needs
   SDRAM later. The "Buffer" cycle parameter in Block 2 controls how much of this fixed
   `BUFFER_SIZE` array is actually used for a given recording (up to this ceiling), not a
   per-Trail runtime-resizable allocation — all 5 Trails share the same compiled-in maximum.
   **✔ Conflict resolved: 30 seconds stands.** The code (`kMaxBufferSamples = 48000 * 5`, 5s
   ceiling) predates this decision and needs to be raised to match — `BUFFER_SIZE` /
   `kMaxBufferSamples` = 1,440,000 samples (30s at 48kHz), not 240,000. The pot range for
   Buffer should extend to the full 30s accordingly; the 2s boot default can stay as-is unless
   you want to change it separately.
3. **Central ParameterRegistry from Phase 1 onward.** Every modulatable parameter registers
   there with an ID, name, min/max/default, and a pointer to its current value. The mod system
   (Phase 10) and macro assignment (Phase 11) access exclusively through this registry, never
   through isolated variables.
4. **Mandatory input filtering.** Before being written to the SDRAM buffer, the input signal
   must pass through a 20 Hz high-pass and 20 kHz low-pass filter (protects the frequency
   analysis from mains hum/digital noise).
5. **Pre-fader routing everywhere.** Taps for filter destination, mod matrix, and reverb send
   occur strictly before the VCA mixer multipliers. A Trail muted to 0 remains active as a
   modulation source/effect send.
6. **Non-blocking ADC mux polling in the main loop**, never in the audio callback. Smooth
   incoming values with an exponential moving average (EMA) to suppress pot jitter.
   **Note:** The Daisy Seed only has 12 native ADC pins (confirmed via Electrosmith
   documentation) — given our channel count (14 pots — 10 block pots + the 4 mod-amplitude
   pots, which act as bipolar attenuverters, see 4.3 — plus CV inputs), external mux ICs (e.g.
   CD74HC4067, 16-channel) on the carrier PCB are mandatory, not optional. They do NOT sit on
   the Daisy Seed itself. **Correction (verified against implementation):** the 5 Trail Level
   controls and the Multi control are digital quadrature encoders, not potentiometers — they
   do NOT go through the ADC mux at all, they connect directly to GPIO pins (with pull-up,
   see the note on `perseids::QuadratureEncoder` below) instead. This lowers the mux channel
   count from what earlier drafts of this document assumed, but adds roughly 15 direct GPIO
   lines (2× CLK/DT + 1 push per encoder × 6 encoders) that need to be planned into the pin
   budget separately from the mux.
7. **Jack detection via hardware normalling**, not a voltage heuristic. Switched jacks:
   plugged in = contact opens = external CV is read; unplugged = contact closed = internal
   source active (see Section 4.10, Auto-Mod).
8. **4% center deadzone for ALL bipolar parameters (mandatory, no exceptions):** Waveshape,
   Umbra/Aurora macro, Atmosphere macro, Character macro, Multi macros, the 4 mod amplitudes
   (attenuverters, see 4.3), and Crossfade velocity (Block 9). ADC values between 0.48 and
   0.52 (= 4% of the travel, ±2% around center) are hard-forced to exactly 0.0 (center) to
   compensate for center-detent tolerances and ADC jitter.
9. **No `%f` in embedded printf (toolchain constraint, verified in dev-phase3v001).** The
   newlib-nano C library used in this build often lacks floating-point support in printf-style
   formatting. All seconds-based display values (Hold, Fade In/Out, Buffer, etc.) must be
   formatted via integer-based formatting instead (e.g. "1.50s", "15s", "INF"), never `%f`
   directly — otherwise the display silently degrades to just printing "s" with no number.

---

## 3. Controls — Overview

| Group | Count | Type | Function |
|---|---|---|---|
| Block pots (1–10) | 10 | Pot | Access to 2–4 sub-parameters each via cycle button (see 4.6) |
| Trail Level | 5 | Rotary encoder with push (not a potentiometer — digital quadrature, direct GPIO, not on the ADC mux) | Turn = this Trail's level; short press = Lock; long press = Solo |
| Mod slots | 4 | Pot | Amplitude (bipolar attenuverter); Destination/Divider via cycle button; source internal or CV in |
| Multi | 1 | Encoder | Dry/Wet, Macro1, Macro2, Settings — via cycle button like the block pots |
| Cycle button | 1 | Button (next to display) | See 4.6 and 4.7 |
| Rec button | 1 | Button (parallel to Trig jack) | Manual record trigger |
| Imprint button | 1 | Button (toggle, see 4.7b) | Locks/unlocks all currently active Trails at once |

**Total: 14 pots (10 block + 4 mod) + 6 encoders (5 Trail Level + 1 Multi) + 3 buttons
(Cycle, Rec, Imprint).**
Only the 14 pots and the mod CV inputs go through the ADC mux — see the corrected channel
count in Section 2, point 6. The 5 Trail Level encoders and the Multi encoder are digital
(quadrature) and connect directly to GPIO pins, not the mux/ADC chain.

---

## 4. Full Architecture & UI Mechanics

### 4.1 The 11 Function Blocks (global, not per Trail)

| # | Block | Cycle list (first entry = default) |
|---|---|---|
| 1 | **Trails** | Count (1–5), Threshold, Cont. Rec, On/Off |
| 2 | **Time** | Buffer (= ring buffer length/max. recording time per Trail, up to 30s ceiling — see Section 2, point 2), Hold (up to 30s, beyond that = infinite; boot default 15s, see 4.8), Fade In, Fade Out |
| 3 | **Engines** | Blend (Spectra↔Swarm), Pitch Spectra, Pitch Swarm |
| 4 | **Spectra Parameters** | Partials, Waveshape (Sine↔Saw↔Fold), Umbra/Aurora Macro, Ensemble/Drift |
| 5 | **Swarm Parameters** | Size, Spread, Scan, Atmosphere Macro (Blur↔Radiation) |
| 6 | **Reverb** | Mix, Decay, Damping, Character Macro (Chorus↔Friction) |
| 7 | **Spectral Resonator** (acts on Swarm output) | Mix, Decay, Pitch, Quantized (On/Off, scale from Settings) |
| 8 | **Pan Drift** | Phase, Amplitude, Velocity |
| 9 | **Crossfade across 5 Trails** | Amplitude, Velocity |
| 10 | **Filter Mix** | Cutoff, Resonance, Feedback (Drive), Destination |
| 11 | **Multi** (Encoder) | Global Dry/Wet, Macro1, Macro2, Time Unit (Clock↔Seconds, see 4.1a), Settings |

**Block 3 interim (verified Phase 5):** Engines CycleRow is **Swarm** (A/B toggle:
OFF=Spectra, ON=Swarm), **Pitch Spectra**, **Pitch Swarm**. Continuous Blend arrives in
Phase 6 — the toggle is deliberate scaffolding, not the final Blend control.

**Block 5 / Swarm engine (verified Phase 5 — implementation contract):**

- **Role:** granular cloud over Trail SDRAM buffers (`trail_buffer`), not the dry input.
  Complements Spectra’s stylized additive body; A/B select for now (Blend = Phase 6).
- **Trail access:** `CaptureEngine` publishes per-block `SwarmTrailView` (length + gain =
  level × fade × play_gain) after `Process`; Swarm reads the same callback. Recording /
  empty / solo-muted trails are skipped.
- **Grains:** up to **16** overlapping grains, linear-interpolated buffer reads, Hann window
  at Atmosphere center. Size maps ~8–180 ms. Spread = stereo pan width. Scan = scrub rate
  through each trail (0 = freeze). Pitch Swarm = `2^(±1 octave)` on grain playback rate.
- **Atmosphere:** Blur (negative) flattens grain envelopes; Radiation (positive) adds
  sample-hold lo-fi + BBD-style output slew.
- **Pot map:** Mux B C1 = Swarm, Mux B C2 = Settings (Spectra remains B C0).

**Block 8 detail (Phase):** Controls the phase offset between the per-Trail-independent Pan
Drift LFOs (0% = all Trails drift in sync, 100% = maximally offset against each other) —
prevents multiple Trails from panning in exactly the same rhythm.

**Block 9 detail (Crossfade across 5 Trails):** An amplitude wave (loudness focus) travels
continuously forward/backward through the active Trails, crossfading neighboring Trails
smoothly against each other — creates a slowly (or not so slowly) shifting focus, or an
additional inner motion within the sound. **Amplitude** = depth of the wave (0% = no effect,
all Trails equal; 100% = only the focused Trail fully audible). Non-focused Trails are
therefore normally only ATTENUATED, not removed — they only approach silence near 100%
amplitude. **Velocity** = travel speed, bipolar (sign = forward/backward direction, 4%
deadzone, center = focus frozen). The wave acts multiplicatively on the same VCA stage as
Trail Level, i.e. AFTER the pre-fader taps — Rule 2.5 remains unaffected, the mod matrix and
sends don't see the wave. The wave only runs across the Trails currently active per Block 1.
Solo overrides the wave (the soloed Trail stays fully audible); Lock only protects against
round-robin replacement, not against the wave.

**Block 4 detail (Umbra/Aurora Macro, bipolar, 4% deadzone):** 0% = neutral 1:1 resynthesis.
Negative values (Umbra) cut away fundamental frequencies, bringing quiet ambient noise
components forward (transparent/airy). Positive values (Aurora) apply a formant/chroma filter
over the partials for harmonic vocal emphasis (note tracking).

**Block 4 detail (Ensemble/Drift):** Slew-limiting on the FFT tracking plus slight detuning of
odd/even partials against each other — produces an organic chorus natively in the oscillator
bank, without external delay lines.

**Block 4 / Spectra engine (verified Phase 4 — implementation contract):**

- **Role:** stylized additive sound body, **not** a transparent 1:1 clone of the Trail audio.
  Peak-picked sine (or waveshaped) partials deliberately omit phase reconstruction and noise
  residual — later engines (Swarm, Resonator, Reverb) complete the cloud. Do not “fix”
  Spectra toward studio-fidelity resynthesis unless this contract is explicitly revised.
- **Analysis input:** pre-fader Trail sum × play gain (`trail_mix` from `CaptureEngine`), never
  the dry monitor path. Dry listen-through stays separate (see below).
- **Threading:** FFT only in the main loop (`ProcessAnalysis`); AudioCallback may only
  `PushInput` + run the oscillator bank (`Process`). Targets publish via seqlock (odd = write
  in progress, even = stable snapshot).
- **CMSIS-DSP:** classic in-place `arm_rfft_fast_f32(S, p, pOut, ifftFlag)` — no separate F32
  tmpBuf API in this tree. Hann window via `arm_mult_f32`, magnitudes via `arm_cmplx_mag_f32`.
  Full prebuilt `libarm_cortexM7lfdp_math.a` overflowed the 128 KB flash budget (~+51 KB); the
  build uses a **lite CMSIS** object set from `link_cmsis_dsp.py` (selective RFFT-512 tables +
  required transform/basicmath sources).
- **Sizes (CPU/Flash budget):** FFT **512**, hop **256**, Partials UI/engine **4…32** (default
  16). Architecture examples that mention 64 partials are aspirational — raise only when audio
  CPU and flash headroom allow. Audio block size **128** @ 48 kHz.
- **Resynthesis:** custom phasor bank + cheap FastSin (not 32× DaisySP `Oscillator`). Waveshape
  bipolar: center = sine, left → saw mix, right → wavefold. Peak pick + **frequency-continuity
  matching** across hops (nearest previous partial within ~2.5 bins / 8% relative). Absolute
  magnitude→amplitude scaling + silence/relative floor — **never** renormalize so peak sum =
  constant loudness (that boosted noise floor into “line interference”).
- **Pitch Spectra:** multiplies all partial target frequencies by `2^(bipolar ±1 octave)`.

**Block 5 detail (Atmosphere Macro, bipolar, 4% deadzone):** 0% = clean grains with a Hann
window. Negative values (Blur) smooth the grain envelopes heavily for edgeless ambient clouds.
Positive values (Radiation) reduce the sample rate (lo-fi) and smooth changes via a BBD-style
slew limiter (tape warble).

**Block 6 detail (Character Macro, bipolar, 4% deadzone):** 0% = untreated reverb tail.
Negative values (Chorus) apply slow modulation to the reverb tail for a wide, lushly
shimmering reverb character — unlike Ensemble/Drift in Spectra, this also affects the Swarm
content and the dry signal, since it sits on the shared reverb send. Positive values
(Friction) apply non-linear saturation (tanh soft clipping) directly into the reverb tank's
feedback loop — at high values, a dense overdrive wall. Chorus and Friction are deliberately
exclusive (one knob, two directions), not combinable at the same time.

**Block 10 detail (Destination):** Selects which signal stage the filter acts on — cyclable
through **Input → Spectra → Swarm → Reverb**.

**Block 11 Settings submenu** (own cycle entry point via "Settings" in the Multi cycle list):
1. CPU/SDRAM meter (On/Off, display on screen). **Bench interim:** CPU meter boots **On**
   while the Settings pot is unwired (`// TODO(release)` markers in `capture_params.h` /
   `main.cpp`) — final firmware must default it back to Off. Display format: CPU-only shows a
   trailing percent sign (`C42%`); combined CPU+RAM stays compact without it (`C42 R12`).
2. Instant Playback Mode (On/Off) — ON: resynthesis starts immediately, analysis refines live
   as the buffer fills up (reactive like a reverb/resonator). OFF: waits for a full buffer,
   then a single analysis pass (behaves like a delay/looper)
3. Scale (C Major, Minor, Pentatonic — extensible)
4. Intonation (Equal Temperament ↔ Just Intonation, for Block 7 Quantized)
5. Auto-Mod/Normalling (see 4.10)
6. **Audio Routing** (Stereo ↔ Sidechain) — see detail description below

**Block 11 detail (Audio Routing):**

**Implementation note (verified in dev-phase3v001):** the capture input is never hard-wired to
"L+R" in code. A `RecordSource` abstraction (mode `Stereo` | `Sidechain`) sits behind it, so
Sidechain mode (below) just swaps which signal `RecordSource` returns, without the rest of the
capture path needing to know which mode is active — this was built now, in Phase 3, precisely
so Sidechain mode (Phase 11) is a mode switch later, not a rewire.

- **Stereo (default):** In L and In R work as a normal stereo pair (or mono split). Both
  inputs are mixed and recorded into the 5 Trail ring buffers, then processed normally.
  **Mono-cable-friendly behavior:** if one channel is effectively silent (only one cable
  plugged into a mono source), `RecordSource` uses the active channel at full level instead of
  averaging it down with a silent channel; otherwise (both channels carry signal) it uses
  (L+R)/2 as before. This avoids a ~6dB level loss for the common case of a single mono cable
  patched into just one input.
- **Sidechain mode:** The jacks are logically separated:
  - **In L (main audio):** live instrument, is NOT recorded, runs directly/dry into the
    output mix (VCA/reverb send as usual, but without reaching the capture buffers)
  - **In R (sidechain capture):** runs exclusively into the threshold detection and the 5
    SDRAM ring buffers — only this signal feeds Spectra and Swarm
  - **Out L/R:** mix the dry main signal (In L) with the sound cloud generated from In R via
    Spectra/Swarm/Reverb — a live instrument can be "commented on" this way by a completely
    independent audio source, without the two signals interfering with each other's analysis

**Listen-through (temporary bench scaffolding through Phase 11 Dry/Wet):** Stereo mode still
dry-monitors the capture signal onto Out at ~85%. From Phase 4 onward, Spectra wet is **added**
on top of that dry path (`out = dry×0.85 + spectra`). This remains a scaffolding mix for
A/B listening on the bench, not the final design — it gets replaced/overridden by the real
Dry/Wet control (Multi encoder, Block 11). Don't treat the 85% figure as a final mix value.

### 4.1a Time Unit (Clock ↔ Seconds) for Buffer and Hold

Both Buffer and Hold (Block 2) can be displayed and set either in **seconds** (default,
free-running) or in **clock-synced bars/note values** (e.g. 1/4, 1/2, 1, 2, 4, 8 bars) when a
Clock signal is present on the Clock jack (4.4). This is one shared global setting, not two
independent ones — switching it affects both parameters' display/input mode at once.

**Two ways to switch:**
1. **Manual:** a new entry in the Multi encoder's cycle list, "Time Unit" — toggle between
   Clock and Seconds at any time via the normal cycle mechanism (4.6), independent of whether
   a clock cable is actually plugged in.
2. **Automatic prompt on jack state change:** when the Clock jack detection (hardware
   normalling, Section 2 point 7) reports a transition — cable freshly plugged in while
   currently in Seconds mode, or freshly unplugged while currently in Clock mode — a brief
   confirmation prompt appears on the display for **3 seconds** ("Switch to Clock?" /
   "Switch to Seconds?"). Pressing the Multi encoder button confirms the switch. This follows
   the same temporary-reinterpretation pattern already used for the Cycle button's reset
   confirmation (4.7): while this prompt is showing, a Multi encoder press means "confirm
   switch," not its normal short-press meaning (stepping through the Multi cycle list). If the
   3s timeout elapses without a press, the prompt disappears and the current unit stays
   unchanged — no forced switch happens silently.

**Clamp behavior, Buffer only:** Buffer remains hard-limited to the fixed 30-second
`BUFFER_SIZE` ceiling (Section 2, point 2) regardless of unit. In Clock mode, if the selected
bar/note value would exceed 30 seconds at the current tempo (e.g. "4 bars" at a slow BPM), the
effective value is clamped to the 30s ceiling and the display marks the value to indicate it's
clamped (e.g. a small asterisk next to the number), rather than silently allowing a value the
buffer can't actually hold.

**Hold has no such clamp** — it can already reach "infinite" regardless of unit, in either
Seconds or Clock mode, exactly as before.

**Signal-loss fallback (safety, distinct from jack presence):** jack normalling (Section 2,
point 7) only detects whether a cable is physically plugged in, not whether actual clock
pulses are arriving over it — a cable can be connected while the upstream clock source is
paused, stopped, or was never running. In Clock mode, if no clock edge is received for longer
than roughly 4× the last measured clock period (or a fixed ~3s timeout if no period has been
measured yet, e.g. right after connecting a cable that never pulses), Time Unit **silently and
automatically falls back to Seconds** — no confirmation prompt, since an undefined bar length
is a correctness problem, not a preference to ask about. A brief, non-blocking display notice
("Clock lost — back to Seconds") shows for ~2s so the sudden value change isn't confusing, but
doesn't require a button press to dismiss. Manually switching back to Clock mode (via the
Multi cycle entry) is possible again as soon as clock pulses resume.

**Fallback value, important detail:** the fallback does NOT reset Buffer/Hold to a default —
it converts the current bar value to seconds using the last known valid tempo/period, frozen
at the moment the clock signal is lost, and keeps using that computed seconds value going
forward. This means the actual recording/hold duration doesn't audibly jump when the clock
drops out — only the display/input unit switches from bars to seconds, the underlying duration
stays continuous. Falling back to a factory default instead would cause an abrupt, audible
change in Buffer/Hold length while Trails may currently be playing — avoid that.

### 4.2 Trail Level (×5)

**Hardware:** digital quadrature rotary encoder with integrated push button (e.g. EC11-style),
not a potentiometer — see the correction in Section 2, point 6. Implemented in the codebase
via a custom `QuadratureEncoder` class (not `daisy::Encoder`), with pull-ups configured
explicitly on both phase pins.

- **Turn:** this Trail's loudness
- **Short press:** Lock (protects against round-robin replacement and hold-time fade-out)
- **Long press:** Solo

### 4.3 Mod Slots (×4)

Cycle list **Amplitude → Destination → Divider**. **Amplitude is bipolar (attenuverter):**
center = 0 (no modulation, 4% deadzone), turning right increases positive modulation depth,
turning left inverts the modulation — applied either to the internal source or, with a cable
plugged in, to the external CV signal. Destination references any parameter from the
ParameterRegistry (Blocks 1–11 or Trail Level). Divider = clock subdivision for the internal
source case. Internal sources: see Auto-Mod (4.10).

### 4.4 I/O Jacks

| Inputs | Outputs |
|---|---|
| Mono In L | Mono Out L |
| Mono In R | Mono Out R |
| Clock | |
| Trig (new Trail, parallel to Rec button) | |
| Mod CV 1–4 (with switched contact/normalling) | |

**8 inputs + 2 outputs = 10 jacks.**

### 4.4a Audio I/O Level Conditioning (mandatory, hardware safety)

**Critical:** The Daisy Seed's onboard codec expects strict line level, roughly 3.3V
peak-to-peak maximum. Feeding Eurorack-level audio (±5V, i.e. ~10Vpp) directly into In L/In R
through nothing but a DC-blocking capacitor massively overdrives the input and risks permanent
hardware damage over time. A DC-blocking cap alone is NOT sufficient — active level scaling is
required in addition, not instead of it.

**Signal path (Eurorack → Daisy, input):**
- Inverting op-amp stage scaling the signal down to roughly one third: 100kΩ input resistor,
  33kΩ feedback resistor (gain ≈ −33k/100k ≈ −0.33).
- A 10µF electrolytic capacitor in series, directly after the input jack, blocks DC offset
  that could otherwise overdrive the op-amp or the codec. This is in addition to the level
  scaling above, not a substitute for it.

**Signal path (Daisy → Eurorack, output):**
- Inverting op-amp stage scaling the Daisy's quiet 3.3V signal back up to Eurorack level,
  roughly ×3.3: 10kΩ input resistor, 33kΩ feedback resistor (gain ≈ −33k/10k = −3.3).
- Same 10µF series DC-blocking capacitor before the output jack.
- A 1kΩ series resistor directly before the output jack protects the op-amp output stage from
  short circuits while patching.

**IC:** TL074 (JFET-input quad op-amp) — the de facto standard for this kind of Eurorack
level-shifting stage, used in many commercial modules (e.g. Mutable Instruments) for the same
purpose.

**Power supply requirement:** The TL074 needs a symmetric supply, typically the Eurorack ±12V
rails. This is normally available for free in a Eurorack context, but if the breadboard setup
so far has only been powered via USB/the Daisy Seed's own 3.3V/5V rails, the ±12V supply needs
to be wired in separately before this stage can work — don't overlook this when moving from
pure digital/UI prototyping (Phases 0–2) to real audio I/O (Phase 3).

### 4.5 Rec Button

Momentary button, electrically parallel to the Trig jack — identical signal, triggers a new
recording independent of the threshold (same round-robin logic as the automatic trigger).

### 4.5a Pin Assignment (Daisy Seed GPIO, verified against Phase 2 implementation)

| Pin | Function |
|---|---|
| D0–D3 | Mux select S0–S3 (shared by both mux chains) |
| D4 | Trail 1 encoder CLK |
| D5 | Cycle button |
| D6 | *free* |
| D7 | OLED CS (SPI1 NSS) |
| D8 | OLED SCK (SPI1 SCK) |
| D9 | OLED DC |
| D10 | OLED MOSI (SPI1 MOSI) |
| D11 | OLED RST (RES) |
| D12 | Rec button |
| D13 | Trig input (3.5mm jack) |
| D14 | Trail 1 push (Lock/Solo) |
| D15 | Mux A ADC (A0) |
| D16 | Mux B ADC (A1) |
| D17 | Trail 2 push |
| D18 | Trail 3 push |
| D19 | Trail 4 push |
| D20 | Trail 5 push |
| D21 | Trail 1 encoder DT |
| D22 | Trail 2 encoder CLK |
| D23 | Trail 2 encoder DT |
| D24 | Trail 3 encoder CLK |
| D25 | Trail 3 encoder DT |
| D26 | Trail 4 encoder CLK |
| D27 | Trail 4 encoder DT |
| D28 | Trail 5 encoder CLK |
| D29 | Trail 5 encoder DT |
| D30–D32 | *free* |

Confirms the correction in Section 2, point 6: the two mux chains have **separate** ADC
inputs (A0/A1, not a shared common line), and the OLED runs on SPI1 in 4-wire mode (no MISO
needed, display is write-only). D6 and D30–D32 are free for future use (e.g. mod slots,
Multi encoder, jack detection lines — not yet assigned as of Phase 2).

**Phase 5 bench pot map (`hw_pins.h` / `main.cpp`, currently 5 pots mapped):**

| Mux | Channel | Block row |
|---|---|---|
| A | C0 | Trails |
| A | C1 | Time |
| A | C2 | Engines |
| B | C0 | Spectra |
| B | C1 | Swarm |
| B | C2 | Settings — **unmapped** (6th bench pot removed; re-add to `kPotMappings` when wired) |

Only map mux channels that physically have a pot: unmapped-but-polled floating channels
spuriously open Cycle views. `EnterDashboard` must be a **no-op when already on the
Dashboard** — an unconditional version re-armed timers/baselines every frame from
Trail-encoder noise and permanently locked the Block menus (symptom: works right after a
power-cycle, then sticks on the Dashboard). Trail-encoder/Level activity **no longer forces
a Dashboard return** at all (removed for the same reason); the idle timeout is the only
automatic return path until the Multi encoder (explicit return, 4.7a) exists.

### 4.6 Universal Cycle Mechanism (10 block pots + 4 mod pots + 1 Multi encoder)

| State | Action |
|---|---|
| Turning a control, cycle button **not** held | Changes the value of the last-bound parameter (start: first list entry = default) — with pickup/catch (see below), no direct jump |
| Cycle button **held** + turning a control | Scrolls through the parameter list — display bottom: name/position, top: current value. No value change |
| Cycle button **released** | Control is now bound to the last-displayed parameter |

**Pickup/catch on control rebinding:** As soon as a pot is newly bound to a parameter via the
cycle button, it almost never sits at the same physical position as the stored value. To
prevent the first turn afterward from causing a jump in the value, **pickup/catch instead of
jump** applies as a general rule: the stored value only starts changing once the physical pot
position "passes through" the stored value while turning — only from that moment on does the
value follow the pot movement 1:1. Until then, the stored value stays put, even while the pot
is being turned. Applies everywhere a discrepancy between pot position and stored value can
arise: cycle rebinding (here), later preset recall (see Section 8), and eventually rebinding
of mod-slot amplitude. Does **not** apply to the Multi encoder — as an endless encoder with no
fixed physical position, no discrepancy can arise there, so pickup isn't needed.

**Pickup arming (verified Phase 4):** arm pickup **once** when entering CycleView from the
Dashboard (if pot and stored value disagree), and **always** re-arm on cycle commit (button
release after scroll). Do **not** re-arm on every pot tick while already in CycleView — that
locks the value in perpetual catch-up while the pickup marker still moves.

**Display coupling (see 4.11):** During the catch-up phase, a solid horizontal line appears
showing the pot's actual physical position, while the bar itself keeps showing the stored,
not-yet-adopted value. Once the two coincide, the line "snaps" into the bar, and the pot takes
over direct control from that point on. This line is deliberately styled differently from the
dots that 4.11 describes for the modulated actual value — both operate at the same bar height
but mean different things (see the distinction there).

**UI robustness additions (verified in dev-phase3v001, implemented as a Cursor rule
`pot-end-catch.mdc` — content belongs here, keep the rule itself short and point back to this
section):**

- **Pot-end-catch:** mux pots rarely reach exactly 0% or 100% at physical end of travel. For
  parameter types where a clean extreme value matters (`HoldTime`, `CountNum`, `CountBar`,
  `Seconds`, and any future type with the same kind of endpoint), shared helpers in `CycleRow`
  apply (not a Hold-only hack):
  - **Value snap** when writing: pot ≥0.94 → 100%, ≤0.06 → 0% (`kEndCatchNorm`).
  - **Pickup meet-band** for a stored end value: pot ≥0.90 (top) / ≤0.10 (bottom)
    (`kEndCatchPot`) — slightly wider than the snap band because the ADC often tops out
    before 0.94 (otherwise Count=5 / Hold INF cannot be picked up).
  - Discrete counts round to the nearest whole number after denormalizing.
- **Dashboard→CycleView opening & focus policy (verified, Phase 5 UI stabilization):**
  opening requires **cumulative pot travel ≥ ~4%** (`kOpenThreshold = 0.040`) measured from a
  baseline captured when the Dashboard was entered. Two hard lessons baked into this:
  (a) never gate opening on per-frame EMA deltas — slow turns stay below any frame threshold
  and menus become unreachable; (b) never let the baseline re-center while idle ("quiet
  tracking") — it silently absorbs slow turns with the same symptom. Exactly **one winner per
  frame** (the pot with the largest travel) opens its Block; all baselines re-arm on open.
  **While a Block is open, only its own pot edits — all other pots are ignored** until the
  idle timeout returns to the Dashboard. This single-owner rule is what stopped Block menus
  thrashing (Trails↔Time↔Engines) from multi-pot ADC noise. The active pot drives
  `ChangeValue` **every frame**: pickup catch and post-catch tracking need continuous samples;
  gating edits behind a per-frame step threshold (~1.5%) froze values right after the catch.
  The small step threshold (`kEditThreshold = 0.015`) only feeds the activity/idle timer.
  A pending "delete all" confirmation (4.7) still aborts on pot movement ≥ ~3%.
- **Mux reading (verified — hard requirement):** use libDaisy's native mux support
  (`AdcChannelConfig::InitMux` + `GetMuxFloat`). libDaisy advances the select lines inside
  the ADC/DMA callback *after* caching the sample, so every value is guaranteed to come from
  the selected channel. A hand-rolled select→settle→read state machine in the main loop
  **races the free-running DMA ADC** and produces cross-channel bleed (values jumping between
  two pots' positions, e.g. bipolar flipping +99↔−50). Never reintroduce manual select
  switching. On top of the cache, apply light EMA (alpha ≈ 0.15, snap ≈ 0.05).
- **Cycle button read order (UI tick):** must be Mux poll → `cycle_btn_.Debounce()` → pot
  handling → `cycle_btn_.Poll()`/gesture evaluation → only then Trail encoders / Rec / Trig.
  libDaisy's `Switch` needs `Debounce()` and `Poll()` called without heavy work in between, or
  short/long press detection becomes unreliable (this was an actual bug on D5/Cycle button,
  now fixed by enforcing this order).
- **Cycle gesture timing:** long-press reset threshold = 1500ms, and only counts if there was
  no real pot-scroll intent during the hold — `pot_moved_during_hold` only triggers on a
  noticeable anchor movement (~5% scroll threshold), not on ordinary ADC noise, so a slightly
  twitchy pot doesn't accidentally cancel a deliberate long-press.

### 4.7 Cycle Button — Additional Functions (pressed alone, without turning a control)

| Gesture | Action |
|---|---|
| Short, alone | **Play/Pause** (global, all Trails) |
| Long, alone | **Reset confirmation:** display shows "Delete all Trails?" — a further short press within 3s confirms and deletes all Trails; timeout or moving a control cancels. During the confirmation, a short press counts as confirmation, NOT as Play/Pause |
| Held + turning a control | Cycle mode (4.6) |

**Multi encoder push (its own button on the encoder, Block 11):** Following the same pattern
as Trail Level push (4.2, short=Lock/long=Solo) and the cycle button (short/long assigned
differently):
- **Short:** Steps forward one position through the Multi block's own cycle list (Dry/Wet →
  Macro1 → Macro2 → Settings → back to Dry/Wet) — a faster direct-access shortcut than the
  usual "hold cycle button + turn the encoder" route (4.6), which continues to work in
  parallel.
- **Long:** **Return to the Home Dashboard** — global, regardless of which block/menu you're
  currently in (not only when Multi itself is bound). The only explicit "return" gesture in
  the entire UI concept.

### 4.7a Returning to the Home Dashboard

Two paths lead back to the Home Dashboard (4.9), complementing rather than competing with each
other:

1. **Explicit:** Multi encoder push, long (see above) — immediate return, regardless of the
   current context.
2. **Automatic via inactivity timeout:** If **no** pot has been turned and **no** button has
   been pressed, the display automatically jumps back to the Home Dashboard after **7
   seconds** — regardless of whether you're currently in a cycle display, a Settings submenu,
   or a segmented selection. The most recently bound pot assignments are unaffected by this;
   only the display changes, no controls get unbound. 7s is a starting value (target range
   5–10s, see the calibration note in 4.11) — long enough to read a value calmly, short enough
   to avoid staying unnecessarily "stuck" on a cycle display. Fine-tune this in practical use
   on real hardware. **Bench interim (Phase 5 UI stabilization): `kInactivityMs = 4000`
   (4s)** — deliberately shorter while the idle timeout is the *only* automatic return path
   (see 4.5a note: Trail-encoder return removed, Multi encoder not built yet). Revisit toward
   the 5–10s target once the explicit return gesture exists.

### 4.7b Imprint (new global function)

**Hardware:** a third, dedicated button (in addition to Cycle and Rec/Trig), using one of the
free GPIO pins (D6, D30–D32, see 4.5a).

**Gesture:** short press = toggle (engage/release, see below). Long press = unconditional
full release (see below) — this button uses both gestures, unlike a plain single-function
toggle, to also provide an emergency "release everything" path.

**Function (short press, toggle):** applies Lock (4.2) to all currently active Trails
simultaneously, freezing the present sound in place — conceptually similar to a "freeze"
function in other granular instruments (e.g. Mutable Instruments Clouds), but named Imprint
here to avoid a naming clash with Block 5's Scan parameter, where "0 = freeze" already means
something different (scan position frozen, not the whole Trail pool).

**Why a dedicated button instead of pressing all 5 Trail Level encoders individually:**
pressing 5 encoders in sequence takes long enough that a Trail could already be replaced by
round-robin before you reach it — the moment you wanted to capture may partly be gone by the
time you've locked the last one. A dedicated button locks all active Trails in the same
instant, with no gap between the first and the last.

**Selective release (short press, second time):** Imprint tracks which Trails it locked
itself, separately from Trails a Trail Level encoder had already locked manually before
Imprint was engaged. Releasing Imprint via a second short press only unlocks the Trails
Imprint itself locked — a Trail you had deliberately locked by hand beforehand stays locked.
This avoids Imprint silently undoing a manual decision you made earlier.

**Unconditional release (long press):** unlocks **all** Trails, regardless of whether they
were locked by Imprint or manually — an emergency "release everything" path, independent of
Imprint's current engaged/released state. No confirmation dialog, unlike the Reset gesture in
4.7: this action isn't destructive (no audio is lost, Trails simply continue playing normally
in the round-robin pool), so a confirmation step would only add friction against the
spontaneous, low-latency feel this function is meant to have.

**No new development phase needed:** Imprint doesn't require any new underlying mechanism —
it's a batch application of Lock, which already exists from Phase 2. Implementation is a small
addition wherever Phase 2's Lock logic lives (plus the tracking bitmask for selective release),
not a new phase of its own.

### 4.8 Capture Model (Trail Pool)

- **Count** (Block 1): how many of the 5 slots are actively used (1–5)
- **Buffer** (Block 2): length of the SDRAM ring buffer per Trail = maximum recording length
  of a single take, up to a fixed ceiling of **30 seconds** (`BUFFER_SIZE`, decided — see
  Section 2, point 2; the code needs updating from its current 5s ceiling to match)
- **Threshold**: triggers automatic recording into the oldest, non-locked active Trail
  (round-robin)
- **Single write-head (verified in dev-phase3v001):** at most one Trail may be in the
  `Recording` state at any given time. A new trigger (Threshold / Cont. Rec / Rec button /
  Trig) is only accepted if no take is currently active; `StartRecording` must clean up any
  stray leftover `Recording` state from a previous take before starting a new one. Visually,
  `Fade In` is a **distinct** state from `Recording` (see the Life-Bar phase table in 4.9) —
  don't conflate them, or Continuous Recording reads on the display as if it were double-
  recording the same take.
- **Cont. Rec** (Continuous Recording): keeps re-triggering new recordings for as long as the
  input signal stays above the threshold, instead of waiting for it to drop below
- **On/Off**: global bypass/enable for the capture system
- **Manual trigger** (Rec button/Trig): same round-robin logic
- **Hold** (Block 2): countdown up to max. 30s; any higher value logically snaps to infinite.
  **✔ Conflict resolved: 15 seconds stands as the boot default**, not infinite — this document
  previously said infinite, the implemented code's 15s boot default is now the decided value.
  Infinite remains reachable by turning Hold above 30s, it's just no longer the default at
  startup.
- **Fade In / Fade Out:** pot range in the implemented code currently goes up to **5 seconds**
  (not the 2s some earlier notes assumed) — Play/Pause performs a global crossfade over these
  times (see the Phase 3 prompt).
- **Instant Playback Mode** (Settings, 4.1): see description there

**Boot defaults, current implemented state (dev-phase3v001), for reference:**

| Param | Default |
|---|---|
| Count | 3 |
| Hold | 15s (decided boot default, see §4.8 for the resolved conflict) |
| Fade In | 3s |
| Fade Out | 3s |
| Threshold | 0.12 |
| Buffer | 2s (range now 30s ceiling, decided — see §2 point 2, code needs updating) |

### 4.9 Display Concept

SSD1309, 128×64 px. Includes: cycle display (name at bottom, value on top), Home Dashboard
with Trail status (Level/Lock/Solo), input threshold VU meter with threshold marker,
CPU/SDRAM meter (top right, hideable via Settings), "Wandering Beams" — rotating rays around
the Trail symbol that shorten as hold time elapses, visualizing the remaining hold time
(shorter/slower = closer to fading out), reset confirmation dialog ("Delete all Trails?", see
4.7).

**Dashboard row layout (verified in dev-phase3v001):** each active Trail gets one row:

```
[VU] T# [3px] nnn% [1px] L [1px] S [1px] [Life-Bar]
```

Fixed spacing: keep the existing gap between the VU meter and the Trail number (`T#`), then a
firm 3px gap before the percentage, and 1px each between the percentage, `L` (Lock indicator),
`S` (Solo indicator), and the Life-Bar. The `L`/`S` columns are reserved space even when
inactive — so the Life-Bar never visually jumps left/right depending on Lock/Solo state.

**Count controls visible rows:** only as many Trail rows are drawn as `Count` (Block 1)
specifies — a Trail index beyond `Count` isn't shown, and the Rec/manual-trigger round-robin
index is likewise clamped to the range 1…Count.

**Life-Bar phases:**

| Phase | Rendering |
|---|---|
| Recording | Progress shown striped (deliberately distinct from Fade In, see the single-write-head note in 4.8) |
| Fade In | Solid fill, left → right |
| Hold | Full bar; countdown (or "INF") shown inverted, 4×6 font, centered, frame redrawn after each update |
| Fade Out | Empties from the left; remaining fill stays on the right |
| Empty | Outline only, no fill |

**VU meter detail:** the Threshold value is drawn as a marker line directly on the VU meter,
not just as a separate number. Keep the display boost mild (**×1.5**, not ×4) — an aggressive
boost fills the bar with ordinary bench/room noise even below the actual threshold, making the
meter look misleadingly "hot" at rest.

### 4.10 Auto-Mod / Normalling (internal sources for the mod CV jacks)

If jack detection (Section 2, point 7) reports a mod CV jack as "not plugged in," the
corresponding slot supplies an internal source. Which one is determined by the Settings choice
**OFF / Age / Pitch / Both** (Block 11):

- **OFF:** simple internal LFO (triangle/sine blend), rate = clock period × divider (default
  behavior, Phase 10).
- **Age (age of the recording):** a linear envelope over a Trail's lifespan — starts at 0% at
  the moment of recording and rises to 100% the closer the Trail gets to its end (hold-time
  fade-out). Musical use (modulation over time): sounds change autonomously as they age — e.g.
  slowly opening a filter, chopping the granular cloud into ever smaller pieces, or turning up
  the reverb send just before a sound cloud dies and gets replaced in the round-robin.
- **Pitch (automatic pitch detection):** the FFT analysis already knows the fundamental
  frequency of the recorded material; it's translated into a continuous control value (low
  notes = low value, high notes = high value). Musical use (modulation over pitch): classic
  key tracking — e.g. opening the filter cutoff further on high lead notes than on low bass
  drones, or reducing the reverb send on low notes so the low end of the mix doesn't get
  muddy.
- **Both:** both modulations act simultaneously — the internal slot value is the arithmetic
  mean of the Age envelope and the Pitch tracking value. Deliberately additive rather than
  multiplicative: both influences stay evenly and subtly audible across the whole lifecycle,
  instead of reinforcing or canceling each other out.

Rule of thumb: **Age = modulation over time** (how long has the voice been alive?), **Pitch =
modulation over note** (how high is the voice?).

**Source Trail:** With up to 5 simultaneous Trails, the **youngest non-locked active Trail**
supplies the Age/Pitch value. Fallback: if all active Trails are locked, the youngest active
Trail supplies it (regardless of lock). If no active Trail exists, the value is 0 — a pure
idle state with no practical relevance, since there's nothing audible to modulate anyway. Rule
2.5 still applies: a Trail sitting at level 0 but still active remains usable as an Age/Pitch
modulation source.

### 4.11 Display Design System: Cycle Parameter Representation

A generic display vocabulary for **every** parameter in **every** block — blocks now just
reference which of the four types applies to which parameter, instead of describing the
appearance from scratch each time. Applies equally to the block pots (1–10), the mod slots
(4), and the Multi encoder.

**Shared screen layout (128×64 SSD1309), top to bottom:**
1. Header row: block/context name on the left, position "n/m" on the right (e.g. "2/4"),
   rendered in the same reduced 4×6 font as the CPU% figure next to it (down from the
   previous larger header font) — the two sit visually consistent as a pair, rather than a
   small CPU% number next to an oversized position counter.
   **CPU% usage** (toggleable, Settings — see 4.1 Settings submenu) sits immediately to the
   left of the position counter, small digits in the same understated style as the Hold
   countdown in the Life-Bar (4.9: inverted, 4×6 font) — not a separate bar or meter, just a
   quiet number that doesn't compete with the position indicator next to it. Hidden entirely
   (no reserved space) when the Settings toggle is off, so the header row layout doesn't shift
   depending on the toggle state — "n/m" alone moves to occupy the same right-aligned spot
   either way, still at the smaller font size regardless of whether CPU% is shown.
2. A continuous horizontal **ceiling line** spanning the full width — a shared 100% reference
   for all parameters of the current block at once, so their bar heights can be compared
   directly against each other (not per individual bar)
3. Parameter area: up to 5 equal-width columns (for blocks with ≤5 cycle entries; mod
   slots/Multi have fewer)
4. A segmented, framed row with all parameter abbreviations (3–4 characters), the active entry
   fully inverted (white fill, black text)

**Active parameter — two vertical lines instead of a closed frame:** The column of the
currently bound parameter gets a vertical line on the left and right, starting seamlessly at
the ceiling line and running without a gap down into the segmented row below. No line closure
needed at top or bottom — the ceiling line and the box itself already serve that function. The
active parameter's numeric value sits freestanding between the header row and the ceiling
line, centered above its column.

**Four display types** (which type applies is defined per parameter within each block):

1. **Unipolar (0–100%)** — e.g. Buffer, Mix, Cutoff, unsigned Umbra/Aurora amount. The bar
   grows from the baseline (0%, at the segmented box) up to the ceiling line (100%). Active:
   wider (14px), value visible. Inactive: narrow (6px), no value shown — bar width is the only
   remaining distinction between active/inactive (no grayscale, since the display is
   monochrome).

2. **Bipolar (±100%, with a center value)** — e.g. Umbra/Aurora Macro, Atmosphere Macro,
   Character Macro, Crossfade/Pan Drift Velocity. The bar grows from the column's center
   either upward (positive) or downward (negative); ceiling line at top = +100%, segmented box
   at bottom = −100%. A dashed center line serves as the zero reference: **full column width
   for the active parameter**, **half column width for inactive bipolar parameters** (enough
   to signal "this is bipolar" without competing with the bar).

3. **Toggle (2 states, e.g. On/Off, Cont. Rec, Quantized, Instant Playback)** — no bar. Both
   states stay visible side by side (left/right, matching the pot's/encoder's turn direction),
   the current one shown inverted. All other parameters in the column row remain visible as
   normal — the toggle only occupies its own column, never the full screen width.

4. **Count value (fixed unit without a % reference, e.g. Partials, small second-based values
   like Fade In/Out)** — two sub-cases:
   - Value range large enough to benefit from a bar (e.g. Partials, **4–32** as of Phase 4;
     64 remains an aspirational ceiling if CPU/flash allow later): identical mechanism to
     unipolar, but the label shows the actual number instead of a percentage, and the ceiling
     line gets a small additional maximum indicator top right, next to the position display
     (e.g. "1/4 · max 32").
   - Value range small/quickly readable (e.g. Fade In/Out, Trail count): **no bar**, just the
     number itself — active: large and centered between the two side lines; inactive: small,
     at the bottom edge of the column.

**Unipolar zero floor (verified Phase 4):** an active unipolar bar at exactly 0% still draws a
**1px** floor on the baseline so the column does not look empty (e.g. Ensemble at default 0%).
The numeric header still shows `0%`.

**Pickup line style (verified Phase 4):** catch-up uses **horizontal stubs only**, with a 1px
gap from the value bar — no vertical ticks.

**Enums with ≥3 named options** (Destination, Scale, Intonation, Audio Routing, Auto-Mod) do
NOT fall under this four-type system — they get their own horizontal segmented-control screen
(see the Auto-Mod example, 4.10), since a bar doesn't make sense here.

**Modulated actual value (when a mod slot targets this parameter):** The bar itself stays
unchanged as the registry's base value (bound to the pot, sitting still). Additionally, a
short **two-dot line** appears to the left and right at the height of the current modulated
actual value — it travels live up and down with the mod source. The dots deliberately sit
farther out than the catch-up line (4.6) ever reaches: dot 1 at the height of the catch-up
line's reach, dot 2 a bit farther out still. For bipolar parameters, the same applies relative
to the center instead of the baseline. Only appears when a mod slot (4.3) is currently routed
to the active parameter — otherwise only the normal bar is shown.

**Distinction from the catch-up display:** The dotted line (modulation) and the solid line
(catch-up, see 4.6) are deliberately styled with different width and density, even though both
can occur at the same bar height — dots = "running automatically with a mod source," line =
"waiting to be taken over by the pot." If both land at the same height, they deliberately merge
into a single visual impression: a line with a solid center (catch-up) and dotted tips
extending beyond it (modulation) — reads as "there's more going on here than just catch-up,"
not as a mix-up. Both states can therefore be visible at the same time without needing any
prioritization.

**Calibration note:** All the details described above (line widths, extra labels like "max
64," dashed center lines) have so far only been simulated on screen. At 128×64 pixels and
~5–6px character height, fine detail can become unreadable on the real SSD1309 — verify
against real hardware before Phase 4 (the first block using all four display types at once:
Spectra Parameters) and simplify where necessary.

**Still open (Section 8):** Pot/encoder turn direction (clockwise = which state for toggles,
which direction for bipolar values) depends on the final hardware wiring and hasn't been
determined yet.

---

## 5. Development Principles

1. **UI mechanics first, entirely on dummy values**, before real DSP is added.
2. **Each phase = one Cursor prompt = one git commit.**
3. **A concrete, testable criterion after every phase** (see the table in Section 6).
4. **The cycle mechanism is implemented once, generically,** and reused for all 15 rows.
5. **The ParameterRegistry is built in from Phase 1 onward**, not bolted on afterward.
6. **ARCHITECTURE.md is the source of truth.** Changes happen here first, only then the next
   prompt.

---

## 6. Phase Roadmap

| Phase | Focus | Test criterion |
|---|---|---|
| 0 | Setup & `.cursor/rules/` | Project compiles, LED blinks, rule is active |
| 1 | ParameterRegistry, cycle mechanism, ADC mux polling, display skeleton | Cycle button + dummy rows work, EMA-smoothed mux values visible |
| 2 | Trail Level pushes, Rec button/Trig, menu button gestures | Lock/Solo/Level on dummy values, clean debouncing |
| 3 | Capture engine (SDRAM ring buffer, round-robin, Cont. Rec, Time block) | Real record/playback, threshold VU meter, hold countdown |
| 4 | Spectra engine (additive) ✔ | Partials/Waveshape/Umbra-Aurora/Ensemble-Drift audible (stylized; see 4.1 contract) |
| 5 | Swarm engine (granular) ✔ | Size/Spread/Scan/Atmosphere audible; A/B vs Spectra |
| 6 | Engine blend (Block 3) | Continuous crossfading Spectra↔Swarm |
| 7 | Spectral Resonator | Mix/Decay/Pitch/Quantized active, intonation from Settings effective |
| 8 | Reverb & Filter Mix | ReverbSc with Character Macro, SVF filter with feedback drive, destination routing |
| 9 | Pan Drift & Crossfade & Wandering Beams | Phase-offset pan LFOs, crossfade slew, display visualization |
| 10 | Mod system | 4 slots, jack normalling, registry destination, divider/clock |
| 11 | Multi & Settings & Calibration | Dry/Wet/Macros, Settings submenu complete, CV calibration |

---

## 7. Phases in Detail (Cursor Prompts)

### Phase 0 — Setup & Cursor Rules

```
Prompt for Cursor:

Read ARCHITECTURE.md in the project root first.

Set up a libDaisy/DaisySP project for the Electrosmith Daisy Seed (PlatformIO). Create
.cursor/rules/architecture.mdc per Section 0. Create main.cpp with a blink test to
verify the build/flash workflow. No audio code in this phase.
```

### Phase 1 — ParameterRegistry, Cycle Mechanism, ADC Mux

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Section 2 (points 3, 6), 4.6, 4.7.

Implement:
- ParameterRegistry class (ID, name, min/max/default, value pointer, registration)
- Generic CycleRow class (parameter list, bound index, Scroll() vs. ChangeValue())
  with 3 dummy instances
- Non-blocking polling of two CD74HC4067-style mux chains (or a direct connection,
  if not yet finalized) in the main loop, NOT in the audio callback, with EMA
  smoothing
- Cycle button gestures per 4.7 (held+turn / alone short / alone long); reuse the
  same gesture class later for Trail Level and the Rec button
- Display update: cycle display (name at bottom, value on top), simple dummy
  dashboard
- Implement the complete display design system from 4.11 (ceiling line, side
  lines instead of a frame, segmented row, inverted selection) already now, using
  the 3 dummy instances — e.g. D1 unipolar with a bar, D2 toggle, D3 bipolar with
  a dashed center line. This lets you test the complete display logic on real
  hardware before the first real parameters get registered in Phase 3+.

Build everything so that later phases only need to create real CycleRow instances
with real parameters registered in the ParameterRegistry — the display design
system itself stays reusable without changes.
```

### Phase 2 — Trail Level, Rec Button, Menu Gestures

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Sections 4.2 and 4.5.

Implement 5 Trail Level push pots (turn=dummy level, short=dummy lock,
long=dummy solo) using the gesture class from Phase 1. The Rec button and the
Trig input trigger the same dummy callback. Show Lock/Solo/Level for all 5
Trails on the dashboard.
```

### Phase 3 — Capture Engine

**Hardware note before starting:** This phase is the first to touch real audio I/O. Make sure
the level-conditioning circuit from 4.4a is built and tested before connecting anything to
In L/In R — feeding Eurorack-level signal directly into the Daisy Seed's line-level input
risks permanent damage.

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Section 2 (points 2, 4), 4.1 (Block 1+2),
4.8.

Implement the capture engine:
- 5 ring buffers via `DSY_SDRAM_BSS float trail_buffer[5][BUFFER_SIZE];`
- Real CycleRow for Block 1 (Count, Threshold, Cont. Rec, On/Off) and Block 2
  (Buffer, Hold, Fade In, Fade Out), replacing the dummy rows
- Round-robin recording on threshold crossing OR Rec button/Trig, into the
  oldest non-locked active Trail
- Continuous recording mode (re-triggers on level crossing without waiting to
  drop below threshold)
- Hold countdown up to 30s, then fade-out; values >30s = infinite. Boot default:
  15s (see 4.8; this document previously said infinite, the implemented code's
  15s is the decided value now)
- Lock protects against replacement and fade-out
- 20Hz high-pass/20kHz low-pass filtering of the input before writing to the
  SDRAM buffer (Section 2, point 4)
- Respect the audio routing mode (Block 11 Settings, see 4.1): by default
  (Stereo), the mixed In-L/In-R signal path feeds the buffers; in the later
  Sidechain mode (Phase 11), only In R is recorded instead, In L stays dry and
  bypasses the buffers entirely — build the signal source for buffer recording
  behind a swappable abstraction accordingly, not hard-wired to "In L + In R"
- Play/Pause from Phase 1 now controls real playback (crossfade over the fade
  times)
```

### Phase 4 — Spectra Engine

**✔ Completed (verified against implementation).** Key decisions locked in the Block 4 /
Spectra engine contract above; do not re-litigate FFT-in-callback, sum-normalization, or
Partials=64 without revising that contract first.

```
Prompt for Cursor (historical — already implemented):

Read ARCHITECTURE.md first, especially 4.1 (Block 3+4) and the Spectra engine contract.

Implement the additive Spectra engine: CMSIS-DSP classic in-place
`arm_rfft_fast_f32` / `arm_rfft_fast_init_f32` + Hann (`arm_mult_f32`) +
`arm_cmplx_mag_f32`. FFT 512 / hop 256 in the main loop only; AudioCallback =
PushInput + phasor bank (FastSin; waveshape sine/saw/fold). Partials 4–32.
Peak continuity matching; absolute mag→amp scaling (no sum renormalize). Analyse
pre-fader trail_mix. Lite CMSIS via link_cmsis_dsp.py if full math lib overflows
flash. CycleRow Block 4 + Pitch Spectra only in Block 3. Register in
ParameterRegistry. Listen-through dry×0.85 + Spectra wet.
```

### Phase 5 — Swarm Engine

**✔ Completed (verified against implementation).** See Block 5 / Swarm engine contract above.

```
Prompt for Cursor (historical — already implemented):

Read ARCHITECTURE.md first, especially 4.1 (Block 3+5).

Implement granular Swarm on trail_buffer via Capture SwarmTrailView snapshots.
CycleRow Block 5: Size, Spread, Scan, Atmosphere (Blur↔Radiation + BBD slew).
Pitch Swarm + temporary Engines A/B toggle (Swarm ON/OFF). Register in
ParameterRegistry. Audio: dry×0.85 + selected engine wet.
```

### Phase 6 — Engine Blend

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially 4.1 (Block 3).

Replace the A/B switch with real pre-fader crossfading between the Spectra and
Swarm outputs. Full CycleRow for Block 3: Blend, Pitch Spectra, Pitch Swarm —
consolidate the pitch values set directly in Phase 4/5 into this here.
```

### Phase 7 — Spectral Resonator

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially 4.1 (Block 7 and the Settings submenu).

Implement the Spectral Resonator on the Swarm output: a bank of tunable bandpass
resonators. CycleRow Block 7: Mix, Decay, Pitch, Quantized (On/Off, forces
pitches onto the scale chosen in Settings). Read the Intonation switch (Equal
Temperament / Just Intonation) from the Settings submenu (Block 11) and apply it
to the resonator tuning.
```

### Phase 8 — Reverb & Filter Mix

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Section 2 (point 5), 4.1 (Block 6+10).

Implement:
- Reverb: DaisySP ReverbSc as a global pre-fader send. CycleRow Block 6: Mix,
  Decay, Damping, Character Macro (bipolar, 4% deadzone: negative = slow chorus
  modulation on the reverb tail; positive = tanh soft-clipping saturation
  directly in the tank's feedback loop — see the 4.1 detail description)
- Filter: state-variable multimode filter (SVF: LP/BP/HP). CycleRow Block 10:
  Cutoff, Resonance, Feedback (audio-rate feedback into cutoff modulation for
  drive), Destination (cycles Input→Spectra→Swarm→Reverb, selects which stage
  gets filtered, pre-fader tap)
```

### Phase 9 — Pan Drift, Crossfade & Wandering Beams

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially 4.1 (Block 8+9) and 4.9.

Implement:
- Pan Drift: an independent LFO per Trail (triangle/sine blend + slight jitter),
  constant-power panning. CycleRow Block 8: Phase (phase offset between the
  Trails' LFOs, 0%=synchronous, 100%=maximally offset), Amplitude (excursion),
  Velocity (speed)
- Crossfade: a traveling amplitude wave across the active Trails per 4.1 (Block
  9 detail), multiplicative on the VCA stage AFTER the pre-fader taps. CycleRow
  Block 9: Amplitude (wave depth), Velocity (bipolar: sign = travel direction,
  4% deadzone, center = focus freeze); plus slew-rate limiting (BBD-style) on
  round-robin replacement of a Trail
- Display: "Wandering Beams" — rotating rays around each Trail symbol that
  shorten/slow down as hold time elapses
```

### Phase 10 — Mod System

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Section 2 (point 7), 4.3, 4.4.

Implement the 4 mod slots: jack detection via switched contact (not a voltage
heuristic) — plugged cable = external CV is read, otherwise an internal source
per the Auto-Mod setting (4.10): OFF = simple triangle/sine with rate = clock
period × divider; Age/Pitch/Both see 4.10 (the Age envelope and Pitch tracking
value get wired to the Settings menu in Phase 11, but set up the source
abstraction already here). CycleRow per slot: Amplitude (bipolar attenuverter,
4% deadzone, see 4.3), Destination (from the ParameterRegistry, all Blocks 1–11
plus Trail Level), Divider.
```

### Phase 11 — Multi, Settings & Calibration

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially 4.1 (Block 11, Settings submenu).

Implement:
- CycleRow for the Multi encoder: global Dry/Wet, Macro1, Macro2 (targets fixed
  in code for now, clearly commented — see the open points in Section 8),
  Settings
- Multi encoder push (short/long, see 4.7a): short = step through the Multi
  CycleRow, long = global return to the Home Dashboard
- Inactivity timeout (7s, see 4.7a): the display state machine automatically
  jumps back to the Home Dashboard when there's no pot/button activity,
  regardless of context
- Settings submenu: CPU/SDRAM meter toggle, Instant Playback Mode toggle, Scale
  selection (C Major/Minor/Pentatonic), Intonation toggle (Equal/Just),
  Auto-Mod/Normalling selection (OFF/Age/Pitch/Both, see 4.10 — controls what
  unplugged mod CV jacks supply internally as soon as jack detection reports
  "not plugged in"),
  Audio Routing toggle (Stereo/Sidechain, see the 4.1 detail description): in
  Sidechain mode, the buffer signal source prepared in Phase 3 gets switched
  exclusively to In R, In L is mixed directly (dry, bypassing the Trail
  buffers) with the Spectra/Swarm/Reverb output onto Out L/R
- Calibration routine (min/max learning mode) for the threshold and all CV
  inputs
- Review: check all display text for readability at 128×64px
```

---

## 8. Open Points

- **~~Exact GPIO pin assignment~~ Resolved:** see 4.5a (verified against Phase 2 implementation, D6/D30–D32 still free for later phases)
- **~~Spectra Phase 4 engine contract~~ Resolved:** see 4.1 Block 4 / Spectra engine (FFT 512, 32 partials, trail_mix analysis, stylized additive — not 1:1). Partials→64 only if CPU/flash allow later
- **~~Phase 4 bench pot map~~ Resolved:** see 4.5a (Mux A Trails/Time/Engines, Mux B Spectra/Settings)
- **~~Pickup arming / pot-end meet-band~~ Resolved:** see 4.6 (one-shot arm + `kEndCatchPot` 0.90)

- **~~Umbra/Corona name collision~~ Resolved:** the macro is now called Umbra/Aurora (incl. Section 2, point 8)
- **~~"Spread" assigned twice~~ Resolved:** Block 8 is now called Phase instead of Spread
- **~~Deadzone named inconsistently~~ Resolved:** unified 4% deadzone (0.48–0.52), mandatory for all bipolar parameters
- **~~Reset gesture risky (long press = immediate delete)~~ Resolved:** confirmation dialog on the display, see 4.7
- **~~Auto-Mod source Trail~~ Resolved:** youngest non-locked active Trail (fallback see 4.10)
- **~~Crossfade wave vs. Lock/Solo~~ Resolved:** Solo overrides the wave, Lock doesn't protect against it (4.1, Block 9 detail)
- **~~Both combination formula~~ Resolved:** arithmetic mean of Age and Pitch (4.10), deliberately subtle rather than heavy-handed
- **Macro1/Macro2 target assignment:** currently fixed in code (Phase 11), no front-panel UI decided for it yet
- **~~Multi encoder push function~~ Resolved:** short = step through the Multi cycle list, long = global return to the Home Dashboard (see 4.7a)
- **Pot/encoder turn direction:** clockwise = which state for toggles (left/right, see 4.11), which direction for bipolar values — depends on the final hardware wiring, not yet determined
- **Macro1/Macro2 in the display design system:** once the target assignment (see above) is settled, it needs to be determined which of the 4 display types from 4.11 applies, depending on what's assigned
- **Modulation mechanic on the registry base value:** additive directly on the value pointer (with min/max clamping), or a separate modulation offset that only gets combined with the base value in the audio callback? Also affects whether the display (see 4.11, modulated actual value) shows the actual registry value or a separate actual value
- Preset storage: architecturally prepared via the ParameterRegistry (flash persistence), but planned only after the V1.0 firmware stabilizes — pickup/catch (4.6) will then also apply to preset recall, once presets exist
