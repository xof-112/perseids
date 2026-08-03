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

**Performance:** all verified CPU / freeze / flash-budget measures are collected in
**§2a Performance Playbook** (A = transferable Daisy/Cortex-M7 patterns, B = Perseids engines).

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
5a. **Multi Dry/Wet is the final output blender.** Clean input (listen-through) on one side;
   the **entire wet chain** on the other — Spectra/Swarm, Resonator, Reverb, Filter inserts,
   **and every future sound-shaper / spatializer** (Pan Drift, Crossfade focus wave, …). New
   audio effects must insert **before** Multi Dry/Wet, never on the clean dry tap. **No effect
   may feed listen-through into the wet bus** (Reverb send, Filter Dest, …) — Multi@100% wet
   must be cloud-only. Local Mix pots stay inside their modules; Multi only balances input vs.
   the finished wet bus.
6. **Non-blocking ADC mux polling in the main loop**, never in the audio callback. Smooth
   incoming values with an exponential moving average (EMA) to suppress pot jitter — lightly
   on mod CV (sluggish modulation is audible), more heavily on pots.
   **Note:** The Daisy Seed only has 12 native ADC pins, and on this build every one of them
   is spoken for by encoders, buttons or mux ADC inputs — external mux ICs (CD74HC4067,
   16-channel) on the carrier PCB are mandatory, not optional. They do NOT sit on the Daisy
   Seed itself. **Correction (verified against implementation):** the 5 Trail Level controls
   and the Multi control are digital quadrature encoders, not potentiometers — they do NOT go
   through the ADC mux at all, they connect directly to GPIO pins (with pull-up, see the note
   on `perseids::QuadratureEncoder` below) instead, which costs 12 direct GPIO lines
   (2× CLK/DT per encoder × 6 encoders) that must be planned into the pin budget separately
   from the mux.
   **Three mux chains (Phase 10 hardware revision):** libDaisy's `InitMux` drives at most
   **three** select lines, so each chain is capped at **8 usable channels**. Two chains were
   not enough for Phase 10 (4 mod pots + 4 mod CV + 5 jack detects, against 5 free channels).
   A third chain, **Mux C**, was added on **D17**. D17 was freed by moving all five Trail
   Level pushes onto a single resistor ladder on Mux A C5 (see 4.4b) — which also freed D14
   and D18–D20 for the Clock, Trig and Imprint trigger jacks plus the Clock jack detect.
   **The budget is now fully used: 24 of 24 mux channels, and every GPIO on the classic Seed
   header.** Two expansion valves remain if something has to be added later: (a) S3 is wired
   to all three 4067s, so any chain can be bank-switched to 16 channels — at the cost of a
   halved sample rate and a hand-rolled settle/discard window that must cover a full DMA
   sweep, not just settling time; and (b) the Multi push can join the Trail ladder, which
   frees Mux B C5.
7. **Jack detection via hardware normalling**, not a voltage heuristic. Five jacks use
   switched types (PJ398SM): **Mod CV 1–4** and **Clock**. Plugged in = contact opens;
   unplugged = contact closed. For the mod jacks this selects internal source vs. external CV
   (see 4.10, Auto-Mod); for Clock it drives the Time Unit prompt (4.1a) — cable presence
   only, which stays strictly distinct from the clock-lost signal fallback. Wiring, and the
   reason the detect network also hands us a free CV zero reference: see 4.4b. Trig and
   Imprint need no detection and use plain unswitched jacks.
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

## 2a. Performance Playbook (verified — large headroom gain)

> **Find this first when hunting freezes or CPU.** Everything that moved Perseids from
> “regular overruns / freezes under dense Swarm” to comfortable headroom is collected here.
> Engine contracts in §4.1 still hold the sonic detail; this section is the *why it runs*
> catalogue. Split into **A — transferable** (reuse on any Daisy / Cortex-M7 audio firmware)
> and **B — Perseids-specific** (tied to this module’s engines). Measured outcome on the
> bench: freezes gone under previously lethal Swarm loads; CPU meter stays well below the
> governor engage point in normal play.

### A. Transferable (Daisy Seed / STM32H750 / any dense audio callback)

| # | Measure | Effect | Where in Perseids |
|---|---------|--------|-------------------|
| A1 | **CPU boost** — `hw.Init(true)` → **480 MHz** (libDaisy Boost; board default is 400 MHz) | ~**+20 %** compute headroom for free | `main.cpp`, `board_build.f_cpu = 480000000L` |
| A2 | **`-fno-math-errno`** | `sqrtf` / `fabsf` become single FPU ops (`VSQRT.F32`) instead of libm calls; flash-negative | `platformio.ini` `build_flags` |
| A3 | **`-ffp-contract=fast`** | `a*b+c` → `VFMA`; no audible FP-semantics change for audio | same |
| A4 | **`-O3` on the hot path, not only on libraries** | PlatformIO stm32cube defaults to **`-Os`**. libDaisy already forces `-O3` via its `library.json` script — so *only application `src/`* was left size-optimised. `build_src_flags = -O3` (after the platform `-Os`) fixed that. Cost ~+32 KB code | `platformio.ini`; DaisySP stays `-Os` |
| A5 | **Leave internal flash when it blocks `-O3` / features** — Daisy **`BOOT_SRAM`**: bootloader in internal flash, app in QSPI @ `0x90040000` → AXI-SRAM (480 KB code). Override `board_upload.maximum_size` / `maximum_ram_size` or PlatformIO still size-checks 128 KB and aborts | Unlocks `-O3` and Phase 10+; `.data`/`.bss` in **DTCM** (128 KB, zero wait-state — small speed win for globals; DMA cannot reach DTCM) | `STM32H750IB_sram.lds`, `-D BOOT_APP`, custom `dfu-util` upload |
| A6 | **Larger audio block size** (here **256** @ 48 kHz, was 128) | Cuts ISR / callback overhead so the main UI loop still runs under heavy DSP | `hw.SetAudioBlockSize(256)` |
| A7 | **Tabulate block-constant transcendental curves** | Per-sample `cos`/`sin`/`pow` in a dense loop will blow the block budget. Build the curve in the main loop into a LUT (double-buffered + dirty/quantize + rebuild rate-limit), callback only interpolates | Swarm window table — see B1 |
| A8 | **Hoist block-rate invariants out of the sample loop** | Anything constant for the block (gains, lengths, `1/sqrt(n)`, pan norms) computed once; bit-identical audio if done carefully | Swarm — see B2 |
| A9 | **Rational soft-clip on the bus instead of `tanh`** | Cheap `(27+a²)/(27+9a²)` form; major saving at the final mix. Keep `tanh` only where the *sound* depends on its saturation curve | Final Multi mix in `AudioCallback`; Swarm output keeps `tanh` (B3) |
| A10 | **Skip silent / open branches** | If a stage’s wet gain ≈ 0 or a filter is wide open, do not enter its Process | Engine blend, Resonator Mix≈0, Filter Dest Off / open LP |
| A11 | **Two CPU meters if you govern load** | Display average (~1 Hz) is too slow to catch a block overrun. A second meter with fast smoothing (~30 Hz) feeds the governor; the slow one stays for OLED | `g_cpu_meter` + `g_gov_meter` |
| A12 | **Decimate expensive FX** | Run the heavy core at half rate (hold / interpolate the rest) when the algorithm tolerates it | ReverbSc tank half-rate; Character still full-rate |
| A13 | **Prefer FPU-native / integer formatting over libm / `%f`** | Same as §2.9; also avoid per-sample `powf` when a closed form or table exists | Display + DSP |

**Rules of thumb that transferred well here:** profile the *sample loop* first (one `pow` × N voices × block size); never trust PlatformIO’s reported “Flash %” after a linker-script change without reading `arm-none-eabi-size -A`; do not enable global `-ffast-math` unless you accept denormal/NaN semantics changes — A2+A3 gave most of the win without that.

### B. Perseids-specific (engines & routing)

| # | Measure | Effect | Notes |
|---|---------|--------|-------|
| B1 | **Swarm grain-window LUT** (1024 pts, double-buffered) | **Main freeze cause removed.** Old path: `cos`+`sin`+`pow` per grain per sample × up to 16 grains. `SyncFromUi` rebuilds on Blur change (quantised 1/128, min 16 ms between rebuilds); callback linear-interpolates (~−110 dB vs direct eval) | `swarm_engine.cpp` |
| B2 | **Swarm per-block hoisting + direct buffer reads** | Trail play length / gain / scan incr / pan×drift `sqrt` norm once per block; grains index `trail_buffer` directly (skip non-inlinable `ReadLooped`); `inv_sqrt_[n]` table; Radiation `ReadInterp` once (was twice) | bit-identical where stated in §4.1 Block 5 |
| B3 | **Keep `tanh` on Swarm output** | Deliberate: rational soft-clip → `x/9` for large x and changes dense-cloud character; ~2 % CPU left on the table on purpose | do not “optimise away” without A/B listening |
| B4 | **Swarm load governor** | Second meter @ 30 Hz; above 85 % drop grain *spawn* cap by 1/block (by 2 above 92 %) to floor **5**; below 70 % recover 1 grain / 8 blocks. Running grains finish → no clicks. OLED **`L!`** while active | `UpdateGovernor` / `GovernorActive` |
| B5 | **Spectra off the audio thread** | FFT only in main loop (`ProcessAnalysis`, ~20 ms cadence); callback = `PushInput` + oscillator bank. Skip synthesis when Blend ≈ full Swarm; still feed the analysis ring so blend-back is not stale | §4.1 Block 4 |
| B6 | **Spectra: custom FastSin phasor bank**, not N× DaisySP `Oscillator` | Keeps additive synthesis inside budget at 32 partials / block 256 | CMSIS lite RFFT-512 (full archive was +51 KB; optional again after BOOT_SRAM) |
| B7 | **Engine blend skip** | `run_spectra` / `run_swarm` from equal-power wet gains; silent side not processed | `AudioCallback` |
| B8 | **Resonator** | Mix≈0 early-out; `UpdateTuning` / `UpdateGains` dirty-checked (sinf/powf only when Pitch/Decay/Damping/Scale/… actually move) | §4.1 Block 7 |
| B9 | **ReverbSc half-rate tank** | Character Chorus stays full-rate on held wet | §4.1 Block 6 |
| B10 | **Filter** | Dest Off skips SVF; wide-open LP ≈ dry → skip stereo bank; `SetFreq` block-rate unless Feedback FM (then every 4 samples) | `filter_engine.cpp` |

**What not to do again:** put FFT or LUT rebuilds in the audio callback; gate governor decisions on the 1 Hz display meter; put application DSP under `-Os` while libraries sit at `-O3`; rely on internal 128 KB flash once code approaches ~100 KB with features still pending.

**Cross-refs:** Swarm contract §4.1 Block 5 · Spectra sizes/clock/boot §4.1 Block 4 · Reverb/Filter §4.1 Blocks 6/10 · Upload grace window / `L!` display §4.9.

---

## 3. Controls — Overview

| Group | Count | Type | Function |
|---|---|---|---|
| Block pots (1–10) | 10 | Pot | Access to 2–4 sub-parameters each via cycle button (see 4.6) |
| Trail Level | 5 | Rotary encoder with push (not a potentiometer — digital quadrature on direct GPIO; the 5 pushes share one resistor ladder on Mux A C5, see 4.4b) | Turn = this Trail's level; short press = Lock; long press = Solo |
| Mod slots | 4 | Pot | Amplitude (attenuverter) + Offset (bias); Destination/Divider via cycle; source internal or CV |
| Multi | 1 | Encoder | Dry/Wet, Macro1, Macro2, Settings — via cycle button like the block pots |
| Cycle button | 1 | Button (next to display) | Hold+turn = cycle (4.6); long alone = delete-all confirm (4.7) |
| Rec button | 1 | Button (parallel to Trig jack) | Manual record trigger |
| Imprint button | 1 | Button (D6) + trigger jack (D19) | Short = Play/Pause; long = Imprint lock toggle; jack = gate-following lock (4.7b) |

**Total: 14 pots (10 block + 4 mod) + 6 encoders (5 Trail Level + 1 Multi) + 3 buttons
(Cycle, Rec, Imprint).**
The 14 pots and the 4 mod CV inputs go through the ADC mux as analog values; the Multi push,
the Trail-push ladder and the 4 mod jack detects also occupy mux channels, as digital-via-ADC
signals (full map in 4.5a). The 5 Trail Level encoders and the Multi encoder are digital
(quadrature) on direct GPIO. The 3 trigger inputs (Clock, Trig, Imprint) and the Clock jack
detect are plain GPIO — conditioning in 4.4b.

---

## 4. Full Architecture & UI Mechanics

### 4.1 The 11 Function Blocks (global, not per Trail)

| # | Block | Cycle list (first entry = default) |
|---|---|---|
| 1 | **Trails** | Count (1–5), Threshold, Cont. Rec, Overwrite, On/Off |
| 2 | **Time** | Buffer (= ring buffer length/max. recording time per Trail, up to 30s ceiling — see Section 2, point 2), Hold (up to 30s, beyond that = infinite; boot default 15s, see 4.8), Fade In, Fade Out |
| 3 | **Engines** | Blend (Spectra↔Swarm), Pitch Spectra, Pitch Swarm, Pitch Both (span ±1…±2 oct for PSP/PSW) |
| 4 | **Spectra Parameters** | Partials, Waveshape (Sine↔Saw↔Fold), Umbra/Aurora Macro, Ensemble/Drift |
| 5 | **Swarm Parameters** | Size, Spread, Scan, Direction (Fwd/Rev/Rnd), Atmosphere Macro (Blur↔Radiation) |
| 6 | **Reverb** | Mix, Decay, Damping, Character Macro (Chorus↔Friction) |
| 7 | **Spectral Resonator** (acts on Swarm output) | Mix, Decay (ring time 0.08–8 s), Damping (metal↔body), Spread (stereo fan), Pitch, Quantized (On/Off, scale from Settings) |
| 8 | **Pan Drift** | Phase, Amplitude, Velocity |
| 9 | **Crossfade across 5 Trails** | Amplitude, Velocity |
| 10 | **Filter Mix** | Cutoff, Resonance, Feedback (Drive), Destination |
| 11 | **Multi** (Encoder) | Global Dry/Wet, Macro1, Macro2, Time Unit (Clock↔Seconds, see 4.1a), Settings |

**Block 3 (verified Phase 6 — implementation contract):** Engines CycleRow is **Blend**,
**Pitch Spectra**, **Pitch Swarm**, **Pitch Both** — the Phase 5 A/B toggle is gone
(`kEnginesSelect` / `engine_swarm` replaced by `kEnginesBlend` / `blend`). Blend is
**unipolar** 0% = pure Spectra … 100% = pure Swarm (not bipolar — no center detent
semantics, no 4% deadzone), boot default 0 (Spectra). The Blend column draws a **subtle
50% hint**: one dot per side at half bar height, equal gap to the bar (`center_mark` flag
in `ParameterDef`, reusable for future crossfade-style unipolar params) — deliberately
quieter than the bipolar dashed zero line, since the middle means "equal mix", not "no
effect". The CycleView **value header** shows a **dynamic side hint** beside the value for
named-pole params (`seg_hint_low` / `seg_hint_high`): Blend **`SP`/`SW`**, Waveshape
**`SA`/`FO`**, Umbra/Aurora **`UM`/`AU`**, Atmosphere **`BL`/`RD`**, Character **`CH`/`FR`**.
Below 50% the low label sits before the number; above 50% the high label after; **nothing at
exact center**. On bipolar macros the pole name **replaces ±**; while a hint is visible the
trailing **`%` is omitted** (`UM 42` / `42 AU`, `SP 42` / `42 SW`) so the right-hand label
cannot overwrite the percent glyph. Pitch and Velocity stay plain `±%`. (Hints used to sit
behind the segment abbrev; that no longer fits with fixed 4-wide columns.) Segment list:
`BLD · PSP · PSW · PB`.
**Pitch Both (`PB`):** unipolar 0…100% — does **not** pitch by itself. It expands the
octave span of **both** Pitch Spectra and Pitch Swarm from **±1 oct (±100%)** at PB=0 to
**±2 oct (±200%)** at PB=100%. PSP/PSW pots keep their travel; the header ±% and the DSP
ratio scale with PB (`octaves = bipolar_norm × (1+PB)`). Bench / test control; **up-shifts
toward ±2 oct can introduce aliasing** (Spectra partials and Swarm grain reads are not
band-limited for extreme pitch-up). The crossfade is **equal-power**
(`cos/sin` of `blend·π/2`) on the two engine outputs, applied **pre-fader** in the audio
callback before the bench listen-through mix. Both engines run simultaneously mid-blend; at
the extremes (gain ≤ 0.001) the silent engine's synthesis is skipped to save audio CPU.
Spectra's `PushInput` always runs regardless of blend so the FFT analysis ring stays warm;
the main-loop `ProcessAnalysis` is skipped only at essentially full Swarm (blend ≥ 0.98).

**Block 5 / Swarm engine (verified Phase 5 — implementation contract):**

- **Role:** granular cloud over Trail SDRAM buffers (`trail_buffer`), not the dry input.
  Complements Spectra’s stylized additive body; blended continuously against Spectra via
  Block 3 Blend (Phase 6).
- **Trail access:** `CaptureEngine` publishes per-block `SwarmTrailView` (length + gain =
  level × fade × play_gain) after `Process`; Swarm reads the same callback. Recording /
  empty / solo-muted trails are skipped.
- **Grains:** up to **16** overlapping grains (load governor may hold this down to 5 — see
  below), linear-interpolated buffer reads, Hann window
  at Atmosphere center. Size maps ~8–180 ms. Spawn interval = `grain_length × 0.15`
  (~6–7 grains steady-state); per-grain amp = `Trail gain × 0.50` (overlap tame). Engine bus
  soft-limited before the final mix. Spread = stereo pan width. Scan = scrub rate through each
  trail (0 = freeze). Pitch Swarm = `2^(±1 octave)` on grain playback rate.
- **Direction** (OLED `DIR`, CountNum): **Fwd** / **Rev** / **Rnd**. Chosen at grain spawn
  (`incr = ±pitch`); Rnd is a per-grain coin flip. Scan scrub direction is independent
  (still advances forward through the buffer). Cycle list: SIZ · SPR · SCN · DIR · ATM
  (5 entries → CycleView 4-column window scrolls).
- **Atmosphere:** Blur (negative) flattens grain envelopes; Radiation (positive) adds
  sample-hold lo-fi + BBD-style output slew.
- **Grain envelope is tabulated, not evaluated:** the Hann↔Blur curve used to call
  `cos` + `sin` + **`pow`** per grain per sample — with 16 grains that alone exceeded the
  block budget and was the main freeze cause. Atmosphere is block-constant, so `SyncFromUi`
  (main loop) builds the **exact** curve for the current Blur into a 1024-point table and
  flips a double-buffer index; the callback only interpolates. Blur is quantised to 1/128
  before a rebuild is triggered, so an Atmosphere sweep costs a bounded number of rebuilds.
  Interpolation error ≈ −110 dB, i.e. inaudible against the direct evaluation.
  **Catalogue:** §2a B1 (transferable pattern A7).
- **Per-block hoisting (bit-identical results):** `SwarmViews()` is written once per block at
  the end of `Capture::Process`, so Trail play length, gain, scan increment and the
  grain-pan × Pan-Drift normalisation (`std::sqrt`) are all computed **block-rate** and cached.
  Grain reads index `trail_buffer` directly instead of going through the non-inlinable
  `CaptureEngine::ReadLooped`; `1/sqrt(n_on)` comes from a table. Radiation used to call
  `ReadInterp` **twice** and discard the first result — now once. `tanh` at the Swarm output
  is deliberately **kept**: the cheap rational soft-clip does not saturate (→ `x/9` for large
  x) and would change the character of dense clouds for ~2% of CPU. **Catalogue:** §2a B2–B3.
- **Load governor** (`UpdateGovernor`, audio thread, block rate): fed by a **second**
  `CpuLoadMeter` smoothed at 30 Hz — the display average (1 Hz, ~160 ms) is far too slow to
  catch a block before it overruns. Above 85% load the grain cap drops by 1 per block (by 2
  above 92%) down to a floor of **5**; below 70% it recovers 1 grain every 8 blocks. Only
  *spawning* is capped — grains already running finish their envelope, so throttling never
  clicks. OLED: **`L!`** appears immediately left of the CPU figure (`L!C42`) and disappears
  by itself once the cap is back at 16. While it shows, the SDRAM figure is suppressed — it
  is a compile-time constant, and dropping it keeps the meter block narrow enough that a
  three-digit CPU reading still fits. The REC header is no longer pinned to x=54: it slides
  left (floor 48, the first column after `PERSEIDS`) whenever the meter block would reach it,
  which also fixes a pre-existing 2px overlap with `PAUSE` + both meters.
  **Catalogue:** §2a B4 / A11.
- **Pot map:** Mux A C3 = Swarm, Mux A C4 = Spectra (full bench map: see 4.5a; Settings
  has no pot — Block 11 = Multi encoder).

**Block 7 / Spectral Resonator (verified Phase 7 — implementation contract):**

- **Role:** parallel bandpass bank on the **Swarm** stereo output (not Spectra, not dry).
  Complements the granular cloud with a pitched resonant body.
- **Topology:** 8× DaisySP `Svf` bandpass on the Swarm mid `(L+R)/2`; each mode is weighted,
  panned and summed into a stereo wet pair, then soft-clipped per channel and blended with
  equal-power Mix. Mix≈0 skips the bank (CPU).
- **Params:** Mix, Decay, Damping, Spread (all unipolar), Pitch (bipolar ±1 octave on root
  C2 ≈ 65.4 Hz), Quantized (toggle). Six entries — the CycleRow scrolls (4.6).
- **Decay is a ring time, not a raw Q.** `Svf::SetRes` maps res through
  `damp = 2·(1 − res^0.25)`, and that fourth root squeezes everything musical into the last
  few percent of the knob: the old linear `res = 0.15…0.92` spanned only **Q 1.3 … 24**, i.e.
  half a second of ring at the maximum — coloration, never a pitched body. Decay now maps
  exponentially to **T60 = 0.08 … 8 s** and each mode derives the damping it needs from
  `damp = 2.199/(f·T60)` (bandpass envelope `exp(−π·d·f·t)`), inverting the Svf curve with
  `res = (1 − damp/2)^4` — exact to float precision. Frequency-correct by construction: high
  modes need less damping for the same ring time. The old full-scale maximum now sits at the
  **centre** of the knob.
- **Damping = the wood/metal axis.** Upper modes get `T60·(f/root)^−1.2·damping`. At 0 the bank
  is glassy and even (all modes ring equally); at 1 the 8th harmonic dies roughly an order of
  magnitude before the fundamental, which reads as a struck body.
- **Spread** fans the modes across the stereo field with equal-power pan, fundamental centred
  and upper modes alternating outward (`kModePan`). At 0 the bank is mono as before.
- **Level:** modes are weighted `1/√n` and normalised by the **vector** norm, not the plain
  sum — they sit on different frequencies and add incoherently, so the previous `1/8` average
  threw away ~9 dB. `Svf::SetDrive(0.5)` is the bank's safety net rather than a tone control:
  the filter subtracts `drive·band³`, so a mode settles near `√(damp/drive)` no matter how
  high Q goes. That, `kBankTrim` and the per-channel soft clip keep the new Q range in bounds.
- **UI-rate cost:** `UpdateTuning` runs `sinf`/`powf` per mode and is called from the main
  loop every iteration, so it is guarded by a dirty check on its six inputs; Spread alone only
  triggers the cheap `UpdateGains`.
- **Quantized OFF:** odd-harmonic series of the pitched root.
- **Quantized ON:** 8 scale degrees from Settings **Scale** (0 Major / 1 Minor / 2 Pentatonic)
  with Settings **Intonation** (0 Equal Temperament / 1 Just ratios). Scale/Intonation live
  in the Settings CycleRow (interim `CountNum` / `Toggle` until Phase 11 named enums).
- **Pot map:** Mux B C1 = Resonator.

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

**Block 8+9 (verified Phase 9 — implementation contract):**

- **Pan Drift:** per-Trail LFO (triangle/sine blend + light jitter), Phase spreads LFO
  offsets (0 = sync … 1 = even spacing), Amplitude = excursion, Velocity = bipolar rate
  (sign = direction, up to ~2 Hz). Constant-power pan coeffs feed Swarm grain pans and a
  VCA-weighted **CloudPan** on mono Spectra → stereo. Lives in `CaptureEngine` / wet bus
  **before Multi Dry/Wet** (2 / 5a); never on the clean dry tap.
- **Crossfade:** traveling cosine focus lobe across Block-1 Count Trails; Amplitude =
  depth; Velocity bipolar (sign = direction, UI 4% deadzone, center = freeze). Multiplies
  the same VCA as Trail Level (`level × fade × xfade`) for `trail_mix` + `SwarmViews.gain`.
- **Round-robin replace:** BBD-style one-pole slew (~60 ms τ) on `ArmingRecord` before
  overwrite (extends the earlier linear soft-replace).
- **Arming must always complete (fixed):** the slew normally runs inside the playback mix
  loop, which skips Trails outside `Count`, with `length == 0`, or muted by another Trail's
  Solo. Lowering Count past an arming slot — or soloing elsewhere while one armed — stranded
  it in `ArmingRecord`, so `arming_record_index_` stayed claimed, `RecordSlotBusy()` stayed
  true, and **every** later trigger (Threshold, Cont. Rec, Rec/Trig) was silently swallowed
  until power-cycle or Clear-All. The header showed a permanent `REC<n>`. A fallback after
  the mix loop now finishes any arming slot the loop could not reach, and releases a stale
  `arming_record_index_` whose voice is no longer `ArmingRecord`.
- **Display — Crossfade focus:** when Amplitude > ~0, matching 1px ticks travel vertically
  immediately left and right of each `T#` on the Home Dashboard (position = focus index
  among Count Trails; tick height scales with Amplitude). `T#` stays normal (not inverted).
  Hidden at Amplitude ≈ 0. Remaining Hold is shown in the Life-Bar countdown only
  (Wandering Beams removed — redundant).

**Block 4 detail (Umbra/Aurora Macro, bipolar, 4% deadzone):** 0% = neutral 1:1 resynthesis.
Negative values (Umbra) cut away fundamental frequencies, bringing quiet ambient noise
components forward (transparent/airy). Positive values (Aurora) apply a formant/chroma filter
over the partials for harmonic vocal emphasis (note tracking). Value header: **`UM` / `AU`**
(see 4.11 pole hints).

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
  required transform/basicmath sources). That +51 KB would fit since the BOOT_SRAM move, but the
  lite set covers everything in use, so there is no reason to link the full archive.
- **Sizes (CPU/Flash budget):** FFT **512**, hop **256**, Partials UI/engine **4…32** (default
  16). Architecture examples that mention 64 partials are aspirational — raise only when audio
  CPU and flash headroom allow. Audio block size **256** @ 48 kHz (was 128; larger blocks
  cut ISR overhead so the UI loop still runs under heavy DSP). ReverbSc runs at **half rate**
  inside `ReverbEngine`. Filter bypasses when open; `SetFreq` is block-rate unless Feedback
  drives cutoff FM (then every 4 samples).
- **Clock / build flags:** `hw.Init(true)` = libDaisy Boost → **480 MHz** (the 400 MHz default
  cost ~20% headroom). `-fno-math-errno` + `-ffp-contract=fast` make `sqrtf` a single
  `VSQRT.F32` instead of a libm call and contract `a*b+c` to `VFMA`; both are flash-negative.
  Optimisation level is deliberately split: libDaisy carries its own `library.json` →
  `platformio_extra_build_script.py`, which forces `-O3` on the whole library, while the
  stm32cube default is `-Os`. That left **only the Perseids engines** — the actual load — on
  `-Os`. `build_src_flags = -O3` now covers `src/` too (it lands after the platform's `-Os`, so
  it wins); DaisySP stays `-Os` because nothing hot lives there. Cost: +32 KB code, irrelevant
  against the 480 KB SRAM budget. `-O3` changes no floating-point semantics.
  Sample rate is fixed at **48 kHz**: libDaisy's `SaiHandle::Config::SampleRate` only offers
  8/16/32/48/96 kHz, so 44.1 kHz is not an option without a custom PLL3 config.
  **Catalogue:** §2a A1–A5.
- **Boot: Daisy bootloader, `APP_TYPE = BOOT_SRAM`.** The 128 KB internal flash was 97.1% full
  with Phases 10/11 still unwritten, so the app moved out of it. `dsy_bootloader_v6_4-intdfu-2000ms`
  now owns internal flash; the app lives in QSPI at **0x90040000** and the bootloader copies it to
  AXI-SRAM on every boot. Wiring in `platformio.ini`: `board_build.ldscript = STM32H750IB_sram.lds`,
  `-D BOOT_APP`, and `upload_protocol = custom` calling `dfu-util … -s 0x90040000:leave`.
  `board_upload.maximum_size/maximum_ram_size` must be overridden as well, otherwise PlatformIO
  keeps size-checking against the board manifest's 128 KB and aborts the link.
  New budgets — code in **SRAM 480 KB** (32% at `-O3`), `.data`/`.bss` in **DTCM 128 KB**
  (57%). DTCM is the tighter one from here on, and it is zero-wait-state, so the move is also a
  small speed win for globals. DMA cannot reach DTCM, but libDaisy already places its buffers in
  `RAM_D2_DMA`. Uploading needs the bootloader's **2 s** window after reset (hold BOOT to extend);
  the ST DFU mode (BOOT + RESET) is only needed to reflash the bootloader itself.
- **Resynthesis:** custom phasor bank + cheap FastSin (not 32× DaisySP `Oscillator`). Waveshape
  bipolar: center = sine, left → saw mix, right → wavefold. Value header poles **`SA` / `FO`**.
  Peak pick + **frequency-continuity matching** across hops (nearest previous partial within
  ~2.5 bins / 8% relative). Absolute magnitude→amplitude scaling + silence/relative floor —
  **never** renormalize so peak sum = constant loudness (that boosted noise floor into “line
  interference”).
- **Pitch Spectra:** multiplies all partial target frequencies by
  `2^(bipolar_norm × span)` where `span = 1…2` from Engines **Pitch Both** (PB); header ±%
  scales the same way (±100%…±200%). Plain `±%` — no pole hint.
**Block 5 detail (Atmosphere Macro, bipolar, 4% deadzone):** 0% = clean grains with a Hann
window. Negative values (Blur) smooth the grain envelopes heavily for edgeless ambient clouds.
Positive values (Radiation) reduce the sample rate (lo-fi) and smooth changes via a BBD-style
slew limiter (tape warble). Value header: **`BL` / `RD`** (see 4.11 pole hints).

**Block 6 / Reverb (verified Phase 8 — implementation contract):**

- **Role:** global pre-fader send/return on the **wet-chain engine bus** (Spectra/Swarm +
  Resonator / Filter Sp|Sw), **before Multi Dry/Wet**. Clean listen-through is **not** sent
  into the tank — Multi Dry/Wet is the only place dry input meets the wet bus (Section 2 / 5a).
  DaisySP-LGPL `ReverbSc` (delay tank in SDRAM via `DSY_SDRAM_BSS` on the `ReverbSc` object).
  Tank advances at **half sample rate** (CPU); Character Chorus still runs at full rate on
  the held wet.
- **Params:** Mix (unipolar, equal-power wet gain on the return), Decay → `SetFeedback`
  (0.55…0.97), Damping → `SetLpFreq` (16 kHz…800 Hz, more = darker), Character (bipolar,
  4% UI deadzone).
- **Character:** Chorus (negative) = DaisySP `Chorus` on the wet; Friction (positive) =
  `tanh` drive into the tank plus a soft external feedback around `ReverbSc` (no internal
  tank hook). Exclusive, one knob.
- **Pot map:** Mux B C2 = Reverb.

**Block 6 detail (Character Macro, bipolar, 4% deadzone):** 0% = untreated reverb tail.
Negative values (Chorus) apply slow modulation to the reverb tail for a wide, lushly
shimmering reverb character — shared wet return, so it colors Spectra and Swarm content in
the tank (not the Multi dry tap). Positive values (Friction) apply non-linear saturation
(tanh soft clipping) directly into the reverb tank's feedback loop — at high values, a dense
overdrive wall. Chorus and Friction are deliberately exclusive (one knob, two directions),
not combinable at the same time. Value header: **`CH` / `FR`** (see 4.11 pole hints).

**Block 10 / Filter Mix (verified Phase 8 — implementation contract):**

- **Role:** stereo DaisySP `Svf` lowpass insert on a selectable **wet-chain** stage (before
  Multi Dry/Wet). Never processes or injects listen-through into the wet bus.
- **Params:** Cutoff (exp 80 Hz…~16 kHz), Resonance (`SetRes`), Feedback (audio-rate
  feedback into cutoff + mild `SetDrive`), Destination (`CountNum` 1…5, labels
  **Off / Inp / Sp / Sw / Rv** via `ParameterDef::enum_labels`).
- **Destination:** **1 Off** (SVF skipped, CPU) → **2 Inp** = engine-sum bus (Spectra+Swarm
  after Blend, pre-reverb) → 3 Spectra → 4 Swarm → 5 Reverb wet. **Boot default = Off.**
  Reverb dest runs after the tank; all dests finish before Multi.
- **Mode:** LP is the Block 10 default (no Mode param on the CycleRow). BP/HP remain on the
  `Svf` for a later Mode entry if needed.
- **Pot map:** Mux B C4 = Filter.

**Block 10 detail (Destination):** Selects which wet-chain stage the filter acts on — cyclable
through **Off → Inp (engine bus) → Spectra → Swarm → Reverb**.

**Block 11 Settings submenu** (gateway "Settings" in the Multi list — enter after Cycle release + Multi turn):
1. CPU/SDRAM meter (On/Off, display on screen). **Bench interim:** CPU meter boots **On**
   while the Settings pot is unwired (`// TODO(release)` markers in `capture_params.h` /
   `main.cpp`) — final firmware must default it back to Off. Display format: CPU-only shows a
   trailing percent sign (`C42%`); combined CPU+RAM stays compact without it (`C42 R12`).
   While the Swarm load governor is throttling, **`L!`** takes the place of that trailing `%`
   and the SDRAM figure is suppressed (`L!C42`); with both meters Off a lone `L!` is shown.
   It clears itself when the grain cap is back at maximum (4.1 Block 5).
2. Instant Playback Mode (On/Off) — ON: resynthesis starts immediately, analysis refines live
   as the buffer fills up (reactive like a reverb/resonator). OFF: waits for a full buffer,
   then a single analysis pass (behaves like a delay/looper)
3. Scale (C Major, Minor, Pentatonic — extensible)
4. Intonation (Equal Temperament ↔ Just Intonation, for Block 7 Quantized)
5. **Rec bar (PRS / PLR / CTR):** Life-Bar recording style — **PLR** Perseids embers
   left→right (default), **PRS** same embers center→out, **CTR** solid center-out
   growth. Perseids styles soft-gate ~200 ms out before Fade In (PRS also soft-gates in).
   Fade In/Out stay solid L→R regardless.
6. **Trail lvl (LVL):** default Trail Level **0%…100% in 5% steps** (CountNum 0…20, boot
   **50%**). Applies on boot, after delete-all Reset, and when edited (unlocked Trails only;
   Lock keeps the manual level).
7. **Trail cnt (#T):** default active Trail count **1…5** (boot **3**). Applies on boot, after
   delete-all Reset, and when edited (also updates live Trails **CNT**). Live CNT pot still
   overrides for the session without rewriting this default.
   Multi → Settings gateway (visible in the Multi list) opens this CycleRow after Cycle
   is released and the Multi encoder is turned again (CPU / RAM / SCL / INT / REC / LVL / #T).
   Inside Settings: Multi turn edits; Cycle short / Cycle-hold+Multi steps. Scroll alone does
   not auto-enter the submenu. Last entry **BCK**: Multi turn returns to the Multi list on
   the Settings gateway.
8. Auto-Mod/Normalling (see 4.10)
9. **Audio Routing** (Stereo ↔ Sidechain) — see detail description below
10. **FX → Input (TODO — Phase 11):** how much listen-through is also fed into the wet FX
   chain (Reverb send / shared post-engine bus). Default **0** = current behavior (effects on
   processed cloud only; Multi@100% = no input bleed). Amount is an **11-step** control
   **0…1** (0.0 / 0.1 / … / 1.0, or discrete CountNum 0–10 displayed as percent). Accessed via Multi
   → Settings. See Section 8.

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

**Multi Dry/Wet (Block 11 encoder — live):** Global equal-power blender between **clean
input** (capture listen-through, never processed) and the **fully processed wet bus** —
everything that shapes the sound after capture. Today that includes Spectra/Swarm (with
Spectral Resonator on Swarm), Filter inserts (Dest Inp = engine bus, Sp/Sw/Rv), Reverb
return (engines-only send), **Pan Drift** (Block 8), and **Crossfade** focus on the Trail
VCA (Block 9) — all inside the wet bus before Multi (Section 2 point 5a). Local Mix pots
(Reverb Mix, Resonator Mix, …) still shape their modules *inside* wet — Multi only balances
input vs. that whole chain. Encoder on D13/D30; push on Mux B C5.
**Multi menu (interim):** open with Multi turn (default Dry/Wet). Step entries with
**Cycle short** (Multi/Settings open) or **Cycle held + Multi turn** — same idea as
Cycle-hold + block pot. Multi encoder push is deferred (unreliable on current hardware);
if it works later, short push still steps / opens Multi, long push → Home. Further steps:
Dry/Wet → Macro1* → Macro2* → Time Unit* → Settings → … (`*` = dummy UI only).
Land on **Settings** (do not auto-enter); after Cycle release, Multi turn opens the Settings submenu.
Encoder turn edits the bound entry (~0.02/detent). Boot Dry/Wet ~0.55. Reverb send stays
**pre-fader** (before Multi Dry/Wet). Soft-limit on the final bus. Bus trims at equal-power
Multi: dry **×0.85**, wet **×1.30** (engines present over listen-through). `trail_mix`
stays **analysis input only** (never mixed to the output). Internal Spectra/Swarm makeup
must stay conservative: raising MagToAmp / grain amp for loudness reintroduced crackle above
~50% Trail Level on both engines.

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
- **Short press:** **context-dependent**
  - **Home Dashboard:** Lock (protects against round-robin replacement and hold-time fade-out)
  - **CycleView / MultiView:** return to the Home Dashboard (any of the 5 Trail pushes).
    Lock is **not** applied on that press — press short again on the Dashboard to Lock.
    Mental model: “two pushes” when you came from a Block menu (Home, then Lock) — not a
    timed double-click detector.
- **Long press:** Solo (works from **any** screen — Dashboard, CycleView, or MultiView —
  because a clear long press is unambiguous; no need to leave the menu first)

### 4.3 Mod Slots (×4)

Cycle list **Amplitude → Offset → Destination → Divider**. Hardware (pot wiring, CV
conditioning, jack detect, mux channels): see 4.4b and 4.5a.

- **Amplitude is bipolar (attenuverter):** center = 0 (no modulation, 4% deadzone), turning
  right increases positive modulation depth, turning left inverts the modulation — applied
  either to the internal source or, with a cable plugged in, to the external CV signal.
- **Offset is bipolar (bias / shift, 4% deadzone at 0):** a DC shift added **after** the
  attenuverter so the modulation can sit entirely in the negative half, entirely in the
  positive half, or anywhere in between — not only swing symmetrically around the destination’s
  current value. Combined:

  ```
  contrib = Offset + Amplitude × source     // source normalised ≈ −1…+1 (or 0…1 for unipolar CV)
  dest    = clamp(base + contrib × span)    // span = destination’s full travel
  ```

  Examples with a bipolar LFO (`source` −1…+1) and `Amplitude = +0.5`:
  - `Offset = 0` → contrib −0.5…+0.5 (classic bipolar around the base)
  - `Offset = −0.5` → contrib −1…0 (**only downward** — e.g. Pitch Both / PSP / PSW only
    into the negative / lower range)
  - `Offset = +0.5` → contrib 0…+1 (**only upward**)

  Same idea applies when Amplitude is negative (inverted source): Offset still slides the
  window up or down. Destinations like Engines **Pitch Both** (PB span), Pitch Spectra/Swarm,
  or any other bipolar/unipolar registry target must honour this — a slot aimed at PB with
  Offset hard left must be able to *only shrink* the pitch span (or only deepen negative
  pitch), never force a symmetric ± wiggle if the user has biased it away.
- **Destination** references any parameter from the ParameterRegistry (Blocks 1–11 or Trail
  Level).
- **Divider** = clock subdivision for the internal source case. Internal sources: see
  Auto-Mod (4.10).

### 4.4 I/O Jacks

| Inputs | Outputs |
|---|---|
| Mono In L | Mono Out L |
| Mono In R | Mono Out R |
| Clock (switched contact/normalling) | |
| Trig (new Trail, parallel to Rec button) | |
| Imprint (trigger/gate, see 4.7b) | |
| Mod CV 1–4 (with switched contact/normalling) | |

**9 inputs + 2 outputs = 11 jacks.** The Imprint jack is new in the Phase 10 hardware
revision — panel layout and both cheat sheets have to follow.

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

### 4.4b CV, Trigger & Detect Conditioning (Phase 10 hardware, mandatory)

Unlike audio I/O (4.4a), the control inputs run **single-supply from 3V3 only** — no ±12V, no
TL074. A rail-to-rail op-amp (MCP6004 or TLV9064) powered from the same 3V3 that is also the
ADC reference physically cannot drive the mux or the ADC beyond their rails, which removes the
entire overvoltage failure mode a ±12V stage brings with it.

**Mod CV input (×4) — DC-coupled, no blocking capacitor.** Note the contrast with 4.4a: an
electrolytic in series would filter away exactly the slow modulation this input exists for. A
passive three-resistor network does scaling and offset in one step; the op-amp is only an
impedance converter (unity-gain buffer).

| Part | Value (1%) | From → To |
|---|---|---|
| Ra | 100k | Tip → summing node N |
| Rb | 60.4k | 3V3 → N |
| Rc | 150k | N → GND |
| D1 | BAT54S | N → 3V3 / GND |
| Rout | 100 Ω | buffer output → mux channel |

Maps ±5V onto **0.14…3.15V**, centre exactly 1.645V. The headroom to the rails is deliberate:
both op-amps only reach within ~25 mV of the supply, so full-scale CV does not clip. At ±10V
the node would want −1.37V / 4.65V — the BAT54S clamps it and Ra limits the current to a few
tens of µA. Ra = 100k is also the conventional Eurorack input impedance.

**Why this matters for the deadzone:** the offset term derives from the same 3V3 that is the
ADC reference, so it is **ratiometric** — the zero point does not move when the rail drifts.
The 4% centre deadzone (Section 2, point 8) depends on exactly that. The gain term remains
3V3-dependent, which is the harmless half of the error.

**Jack detect (×5: Mod CV 1–4 + Clock):**

| Part | Value | From → To |
|---|---|---|
| Rgnd | 10k | switch contact S → GND |
| Rs | 100k | S → detect node D |
| Rpu | 1M | 3V3 → D |
| Cd | 100 nF | D → GND |

Unplugged, S is internally tied to the tip and the 10k pulls both to 0V → D ≈ 0.30V. Plugged,
the switch opens and D = 3.3V. Over 3V of separation — unambiguous, and no voltage heuristic
anywhere. Rs additionally protects against the instant during insertion when the plug brushes
the switch contact. The 4 mod detects sit on mux channels (digital-via-ADC, same pattern as
`kMultiPush`); the Clock detect is a plain GPIO, since burning an ADC channel on a pure
digital signal would be wasteful and 0.30V / 3.3V are clean logic levels.

**Free CV zero reference:** because the 10k also holds the *tip* at 0V while unpatched, the
summing node sits exactly on its designed centre. Every unpatched mod jack therefore
self-calibrates continuously — read the channel while detect reports "unplugged" and store
that value as the channel's zero. No trimmers anywhere; the Phase 11 calibration routine only
has to persist the learned offsets.

Firmware: threshold ≈ 0.6 normalised, with hysteresis and ~20 ms debounce — otherwise a slot
chatters between internal source and CV while patching.

**Trigger inputs (×3: Clock, Trig, Imprint):** 100k in series from the tip, 1M pull-down to
GND, BAT54S to 3V3/GND, then into a **74HC14** Schmitt inverter on 3V3. The node sits at
≈ 0.91 × Vin; with VT+ ≈ 1.8V and VT− ≈ 0.9V the input fires at ≈ 2.0V and releases at
≈ 1.0V. That swallows 3.3V, 5V and 10V gates alike, and the hysteresis prevents
double-triggering on slow edges — an LFO abused as a clock, for instance. Negative excursions
are clamped.

Three channels × two inverters is exactly the six gates of one 74HC14, so use **double
inversion** and keep the GPIO logic non-inverting; it avoids a silent active-low footgun later.
100 nF decoupling at the IC. With the Clock cable pulled, the detect network's 10k holds that
tip at 0V — no phantom clocks, for free.

**Trail-push ladder (Mux A C5):** a **priority ladder**, not binary weighting, because it
tolerates resistor spread far better. 10k pull-up to 3V3, each push shorting its own resistor
to GND — 0 Ω, 2.2k, 4.7k, 10k, 22k. Levels 0.00 / 0.59 / 1.05 / 1.65 / 2.27V, idle 3.30V,
about 0.6V per step. 100 nF at the node for debouncing.

Trade-off: with two Trail pushes held at once, the lower resistance wins. There is no
documented multi-push gesture — Cycle sits separately on D5, Lock and Solo are single actions
— so this is acceptable. If a combination gesture is ever needed, binary weighting is the
rework.

**Sampling:** the **Clock** GPIO is read **once per sample inside the audio callback**. A
register read at 48 kHz is free and gives edge timestamps at ~21 µs. Block-rate polling (1 ms)
would put ~7% jitter on a 174 BPM / 24 PPQN period (~14 ms) and feed it straight through the
Divider into the mod-slot LFOs. Trig and Imprint are fine at block rate. All three set atomic
flags; evaluation happens in the UI tick.

**Mod pots (×4):** 10k linear with centre detent. CCW → GND, CW → 3V3 — the same 3V3 as the
ADC reference, a separate rail breaks the ratiometry — wiper → mux channel, 100 nF from wiper
to GND. The pot is a **cycled** control (Amplitude / Offset / Destination / Divider) and must
therefore be read as a raw position: it can never be an analog attenuverter sitting in front
of the CV.

**Termination:** every newly polled mux channel must be terminated by a pot, a buffer output
or a pull resistor. Floating channels spuriously open Cycle views (see 4.5a).

### 4.5 Rec Button

Momentary button, electrically parallel to the Trig jack — identical signal, triggers a new
recording independent of the threshold (same round-robin logic as the automatic trigger).

### 4.5a Pin Assignment (Daisy Seed GPIO — Phase 10 hardware revision)

| Pin | Function |
|---|---|
| D0–D2 | Mux select S0–S2 (shared by all three mux chains, libDaisy-driven) |
| D3 | Mux select S3 (wired to all three 4067s, held low — reserve for bank switching) |
| D4 | Trail 1 encoder CLK |
| D5 | Cycle button |
| D6 | Imprint button (Play/Pause + Imprint lock) |
| D7 | OLED CS (SPI1 NSS) |
| D8 | OLED SCK (SPI1 SCK) |
| D9 | OLED DC |
| D10 | OLED MOSI (SPI1 MOSI) |
| D11 | OLED RST (RES) |
| D12 | Rec button |
| D13 | Multi encoder CLK |
| D14 | **Clock trigger in** (via 74HC14, see 4.4b) |
| D15 | Mux A ADC (A0) |
| D16 | Mux B ADC (A1) |
| D17 | **Mux C ADC (A2)** — new |
| D18 | **Trig in** (new Trail, parallel to Rec — via 74HC14) |
| D19 | **Imprint trigger in** (via 74HC14) |
| D20 | **Clock jack detect** (digital, network per 4.4b) |
| D21 | Trail 1 encoder DT |
| D22 | Trail 2 encoder CLK |
| D23 | Trail 2 encoder DT |
| D24 | Trail 3 encoder CLK |
| D25 | Trail 3 encoder DT |
| D26 | Trail 4 encoder CLK |
| D27 | Trail 4 encoder DT |
| D28 | Trail 5 encoder CLK |
| D29 | Trail 5 encoder DT |
| D30 | Multi encoder DT (USB-HS D+ on classic Seed — OK if USB-HS unused) |
| D31–D32 | *not on classic Seed header* (Seed 2 DFM only) |

**D17 = A2 = ADC 2 — verified against the Electrosmith pinout.** The whole Mux C plan rests on
this one pin; it is confirmed, not assumed.

The three mux chains have **separate** ADC inputs (A0/A1/A2, not a shared common line), and
the OLED runs on SPI1 in 4-wire mode (no MISO needed, the display is write-only).

**What changed vs. the Phase 2 assignment:** Trail pushes 1–5 no longer sit on D14 and
D17–D20. They share one priority resistor ladder on Mux A C5 (4.4b), which freed D17 for
Mux C and D14 / D18–D20 for the three trigger jacks plus the Clock detect. The earlier note
"if a Trig jack is added later, prefer mux" is therefore **obsolete** — Trig now has a
dedicated GPIO and can be edge-timed properly.

**Multi encoder wiring:**

| Multi encoder | Pin / channel | Notes |
|---|---|---|
| CLK (A) | **D13** | GPIO (`kMultiEncClk`) |
| DT (B) | **D30** | GPIO (`kMultiEncDt`) — USB-HS D+; OK if USB-HS unused |
| Push | **Mux B C5** | digital via ADC (`kMultiPush*`); pull-up to 3V3, switch to GND |
| Common | GND | |

**Mux channel map — 24 of 24 used:**

| Mux | Channel | Assignment |
|---|---|---|
| A | C0 | Trails pot |
| A | C1 | Time pot |
| A | C2 | Engines pot |
| A | C3 | Swarm pot |
| A | C4 | Spectra pot |
| A | C5 | Trail-push ladder (Trail 1–5) |
| A | C6–C7 | Jack detect Mod CV 1–2 |
| B | C0 | Pan Drift pot |
| B | C1 | Resonator pot |
| B | C2 | Reverb pot |
| B | C3 | Crossfade pot |
| B | C4 | Filter pot |
| B | C5 | Multi push |
| B | C6–C7 | Jack detect Mod CV 3–4 |
| C | C0–C3 | Mod CV 1–4 (conditioned, analog) |
| C | C4–C7 | Mod pots 1–4 (attenuverter) |

There is **no spare channel and no spare GPIO left** — see Section 2, point 6 for the two
expansion valves (S3 bank switching, Multi push onto the ladder).

All ten block pots are live (no remaining `dummy_params` CycleRows). The Settings CycleRow has
**no pot** (Block 11 = Multi encoder, Phase 11) but includes Scale + Intonation for the
Resonator; the CPU meter stays default-On for the bench. Mux polling uses libDaisy's native
support (`AdcChannelConfig::InitMux` + `GetMuxFloat`) with three select lines, 8 channels per
chain; S3 stays low. Only map channels that are physically terminated — unmapped-but-polled
floating channels spuriously open Cycle views. `EnterDashboard` must be a **no-op when already
on the Dashboard** — an unconditional version re-armed timers/baselines every frame from
Trail-encoder noise and permanently locked the Block menus (symptom: works right after a
power-cycle, then sticks on the Dashboard). Trail-encoder/Level activity **no longer forces a
Dashboard return** at all (removed for the same reason); the idle timeout is the only
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
  `Seconds`, **all** `Unipolar` — Mix/Decay/Damping/Cutoff/Size/Blend/… — and **all**
  `Bipolar` — Pitch/Character/Atmosphere/…), shared helpers in `CycleRow` apply (not a
  per-param hack). `Toggle` is excluded. (`center_mark` on Blend is display-only: 50% dots /
  SP|SW hints — end-catch no longer requires it.)
  - **Value snap** when writing: pot ≥0.94 → 100%, ≤0.06 → 0% (`kEndCatchNorm`).
  - **Pickup meet-band** for a stored end value: pot ≥0.90 (top) / ≤0.10 (bottom)
    (`kEndCatchPot`) — slightly wider than the snap band because the ADC often tops out
    before 0.94 (otherwise Count=5 / Hold INF / Mix=100% / Blend=100% cannot be picked up).
  - Discrete counts round to the nearest whole number after denormalizing.
- **Dashboard→CycleView opening & focus policy (verified, Phase 5 UI stabilization; refined):**
  opening requires **cumulative pot travel ≥ ~2.8%** (`kOpenThreshold = 0.028`) from a
  baseline captured when the Dashboard was entered, **plus** a short same-direction motion
  burst (`kOpenConfirmTravel ≈ 1.2%`) so slow mux drift cannot open a menu. To restore the
  older travel-only 4% policy: ask the agent *"Revert pot-menu open to baseline"* (see
  `REVERT` comment on `kOpenThreshold` in `ui_controller.h`). Two hard lessons baked in:
  (a) never gate opening on per-frame EMA deltas alone — slow turns stay below any frame
  threshold and menus become unreachable; (b) never let the baseline re-center while idle
  ("quiet tracking") — it silently absorbs slow turns with the same symptom. Exactly **one
  winner per frame** (largest qualifying travel) opens its Block; all baselines re-arm on open.
  **While a Block is open, its own pot edits every frame.** Other pots are ignored **unless**
  one exceeds a stricter **switch** threshold (`kSwitchThreshold ≈ 6%` travel +
  `kSwitchConfirmTravel ≈ 1.8%` same-direction burst) — then that Block steals focus
  (pickup re-armed, baselines reset). That lets you jump Block→Block while performing
  without waiting for the idle timeout, while still rejecting mux jitter. The active pot
  drives `ChangeValue` **every frame**: pickup catch and post-catch tracking need continuous
  samples; gating edits behind a per-frame step threshold (~1.5%) froze values right after
  the catch. The small step threshold (`kEditThreshold = 0.015`) only feeds the activity/idle
  timer. A pending "delete all" confirmation (4.7) still aborts on pot movement ≥ ~3%.
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
| Short, alone | **Interim:** while Multi or Settings is open → step that list (Multi push unreliable). On Dashboard / block CycleView still unused. Short during Reset confirmation still confirms delete-all |
| Long, alone | **Reset confirmation:** display shows "Delete all Trails?" — a further short press within 3s confirms and deletes all Trails; timeout or moving a control cancels. During the confirmation, a short press counts as confirmation, NOT as anything else |
| Held + turning a control | Cycle mode (4.6) |

**Why Play/Pause left Cycle:** hold+scroll release was often classified as ShortPress and
paused playback while editing Block menus. Transport belongs on the dedicated Imprint button.

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

Paths back to the Home Dashboard (4.9), complementary:

1. **Explicit — Multi encoder push, long** (see above) — immediate return from any context.
2. **Explicit — any Trail Level short press** while in CycleView or MultiView — immediate
   return (does **not** toggle Lock; Lock only on the Dashboard). Cycle short is **not** used
   for Home (reserved / previously collided with transport when Play lived on Cycle).
3. **Automatic via inactivity timeout:** If **no** pot has been turned and **no** button has
   been pressed, the display automatically jumps back to the Home Dashboard after **7
   seconds** — regardless of whether you're currently in a cycle display, a Settings submenu,
   or a segmented selection. The most recently bound pot assignments are unaffected by this;
   only the display changes, no controls get unbound. 7s is the target (range 5–10s, see the
   calibration note in 4.11). **`kInactivityMs = 7000`.** With cross-Block pot switching (4.6)
   and Trail-short Home, the timeout is no longer the only way out of a CycleView — keep it
   calm for reading values; do not shorten further just for navigation.
   **Idle-timer arming (verified):** `HandlePotTurn` runs every frame for the open Block, so
   the timer must not refresh on every call. It refreshes on Block enter, on pot motion above
   `kActivityThreshold` (0.4% — below the 1.5% edit chatter gate so slow turns still count),
   on each Cycle-hold scroll step, and on Cycle press/release. The old gate on `kEditThreshold`
   alone meant careful scrolling through a 6-param row (Resonator) could expire mid-gesture.

### 4.7b Imprint (global button D6 + trigger jack D19)

**Hardware:** dedicated button on **D6** (momentary, active-low pull-up) *and* an **Imprint
trigger/gate jack on D19** (conditioned per 4.4b), in addition to Cycle (D5) and Rec/Trig.

**Button gestures:**

| Gesture | Action |
|---|---|
| Short | **Play/Pause** (global, all Trails — Fade In/Out times from Block 2) |
| Long | **Imprint lock toggle** (~800 ms hold) — see below |

**Jack behaviour — gate-following (default):** gate high = all currently active Trails locked,
gate low = release. A trigger has no length, so the button's short/long split cannot be
reproduced one-to-one — and gate-following is the musically stronger reading anyway: a Grids
or Leo Leo gate freezes the cloud for exactly one bar and releases it, which is precisely the
gesture Imprint was built for, only clocked instead of hand-played. The button keeps its
toggle semantics unchanged.

**Shared owner set (required, not optional):** Imprint tracks which Trails it locked itself,
separately from Trails a Trail Level encoder had already locked manually (selective release,
below). Button and jack must write to **one shared owner set** — otherwise one of them
releases what the other is holding. Small extension to the existing logic, but it has to be
there before the jack is wired.

**Alternative jack modes (Settings, Block 11):** pulse = Play/Pause, or gate-length
discrimination mirroring the button (< 800 ms = Play/Pause, longer = lock toggle). The latter
mirrors the button most faithfully but turns every short gate in a set into a transport
command — risky in a Techno/DnB context. Default stays gate-following; the Settings entry
lets it be decided at the bench instead of on paper.

**Imprint lock (long press, toggle):** applies Lock (4.2) to all currently active Trails
simultaneously, freezing the present sound in place — conceptually similar to a "freeze"
function in other granular instruments (e.g. Mutable Instruments Clouds), but named Imprint
here to avoid a naming clash with Block 5's Scan parameter, where "0 = freeze" already means
something different (scan position frozen, not the whole Trail pool).

**Why a dedicated button instead of pressing all 5 Trail Level encoders individually:**
pressing 5 encoders in sequence takes long enough that a Trail could already be replaced by
round-robin before you reach it — the moment you wanted to capture may partly be gone by the
time you've locked the last one. A dedicated button locks all active Trails in the same
instant, with no gap between the first and the last. The jack sharpens this further: a gate
edge has no human reaction time at all.

**Selective release (long press, second time):** Imprint tracks which Trails it locked
itself, separately from Trails a Trail Level encoder had already locked manually before
Imprint was engaged. Releasing Imprint via a second long press only unlocks the Trails
Imprint itself locked — a Trail you had deliberately locked by hand beforehand stays locked.
This avoids Imprint silently undoing a manual decision you made earlier. The same rule applies
to the gate's falling edge.

**Unconditional unlock-all:** still available by unlocking each Trail via its Level push
(short = Lock toggle), or via delete-all Reset (4.7). A dedicated "unlock everything" long
gesture on Imprint is deferred so Short can stay Play/Pause without gesture overload.

**No new development phase needed:** Imprint doesn't require any new underlying mechanism —
it's a batch application of Lock, which already exists from Phase 2, plus Play/Pause which
already existed on Cycle. The jack is wired in Phase 10 together with the rest of the
control I/O.

### 4.8 Capture Model (Trail Pool)

- **Count** (Block 1): how many of the 5 slots are actively used (1–5)
- **Buffer** (Block 2): length of the SDRAM ring buffer per Trail = maximum recording length
  of a single take, up to a fixed ceiling of **30 seconds** (`BUFFER_SIZE`, decided — see
  Section 2, point 2; the code needs updating from its current 5s ceiling to match)
- **Threshold**: triggers automatic recording into the oldest eligible, non-locked active
  Trail (round-robin; eligibility depends on **Overwrite**, below)
- **Overwrite** (Block 1 toggle, OLED `OVR`, default **ON**):
  - **ON** — current behaviour: prefer Empty, else steal the oldest unlocked Playing /
    FadingOut Trail (including mid finite Hold), with soft-replace before overwrite
  - **OFF** — Hold-Lock: only Empty slots (plus **INF** Hold Trails, still stealable);
    finite Hold and Fade-Out are protected until the slot becomes Empty; triggers are
    ignored while the pool is full. Same rule for Threshold, Cont. Rec, and Rec/Trig
  - User **Lock** remains stronger: never round-robin, never Hold fade-out
- **Single write-head (verified in dev-phase3v001):** at most one Trail may be in the
  `Recording` state at any given time. A new trigger (Threshold / Cont. Rec / Rec button /
  Trig) is only accepted if no take is currently active; `StartRecording` must clean up any
  stray leftover `Recording` state from a previous take before starting a new one. Visually,
  `Fade In` is a **distinct** state from `Recording` (see the Life-Bar phase table in 4.9) —
  don't conflate them, or Continuous Recording reads on the display as if it were double-
  recording the same take.
- **Loop-seam crossfade (fixed in Phase 6):** recording writes ~40 ms past the loop end;
  `FinishRecording` equal-power-blends that overflow into the head so the hard wrap
  x[len−1]→x[0] is continuous *in the buffer*. Playback uses the **full** length — no
  shortened play-length / runtime CF (those fought the baked seam and left crackle on
  *both* Spectra and Swarm at ≥~50% Trail Level). All readers share this material
  (trail_mix → Spectra, trail_buffer → Swarm). Spectra `MagToAmp` stays coherent-only;
  Swarm grain amp stays overlap-tamed; raw `trail_mix` is analysis-only (never mixed out).
- **Soft replace before overwrite (anti-click):** when round-robin steals a still-playing
  Trail (Overwrite ON, or INF under Overwrite OFF), the engine fades that voice out
  (BBD-style ~60 ms τ) before `Recording` begins. Hard-muting an audible Trail at the
  moment the previous take finished was the click heard only while Play was on (inaudible
  in Pause because `play_gain`=0). Swarm grains follow the live Trail gain so they mute
  with the same fade.
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
CPU/SDRAM meter (top right, hideable via Settings, with an `L!` prefix while the Swarm load
governor throttles), record slot indicator (`R1`…`R5` idle /
`REC1`…`REC5` while armed/recording — same understated **Font_4x6** as the CPU meter),
Remaining Hold is shown in the Life-Bar countdown (numeric / INF). Reset confirmation dialog
("Delete all Trails?", see 4.7).

**Dashboard row layout (verified in dev-phase3v001):** each active Trail gets one row:

```
[VU] [xf] T# [xf] [3px] nnn% [1px] L [1px] S [1px] [Life-Bar]
```

Fixed spacing: 1px Crossfade focus ticks flank `T#` when Block-9 Amplitude > ~0 (`T#` is
shifted 1px right so both ticks are equal width). Then a firm 3px gap before the percentage,
and 1px each between the percentage, `L` (Lock indicator), `S` (Solo indicator), and the
Life-Bar. The `L`/`S` columns are reserved space even when inactive — so the Life-Bar never
visually jumps left/right depending on Lock/Solo state.

**Count controls visible rows:** only as many Trail rows are drawn as `Count` (Block 1)
specifies — a Trail index beyond `Count` isn't shown, and the Rec/manual-trigger round-robin
index is likewise clamped to the range 1…Count.

**Life-Bar phases:**

| Phase | Rendering |
|---|---|
| Recording | Settings **REC**: **PRS** = 1×1 embers drift center→out; **PLR** = same embers travel full-bar, revealed L→R (density peaks near mid-bar, thins toward the right); **CTR** = solid center→both sides. PRS soft-gates ~200 ms in; PRS/PLR soft-gate ~200 ms out before Fade In (UI-only). |
| Fade In | Solid fill, left → right |
| Hold | Full solid bar; countdown (or "INF") shown inverted, 4×6 font, centered, frame redrawn after each update |
| Fade Out | Empties left → right; remaining fill stays on the right |
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
3. Parameter area: **5** visual slots (`kCyclePageCols`) spanning the same total width as
   four equal columns: **half | full | center | full | half**. The active parameter sits in
   the true screen-center slot (`kCycleFocusSlot = 2`); outer half-slots preview wrap
   neighbors (abbrev omitted — too narrow). Indices **wrap** at list ends (same as Cycle
   scroll). Header `n/m` always reflects the full list length (e.g. `5/5`), not the visible
   page size.
4. A segmented, framed row with the **visible** parameter abbreviations (3–4 characters),
   the active entry fully inverted (white fill, black text)

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
   Character Macro, Waveshape, Pitch, Crossfade/Pan Drift Velocity. The bar grows from the
   column's center either upward (positive) or downward (negative); ceiling line at top =
   +100%, segmented box at bottom = −100%. A dashed center line serves as the zero reference:
   **full column width for the active parameter**, **half column width for inactive bipolar
   parameters** (enough to signal "this is bipolar" without competing with the bar).

**Value-header pole hints (verified):** named-pole parameters optionally set
`seg_hint_low` / `seg_hint_high` on `ParameterDef`. The CycleView **value header** (not the
segmented row — 4-wide cells are too narrow) draws those labels in Font_4x6 beside the %:
low label **before** the value below 50%, high label **after** above 50%, **nothing at exact
center** (bipolar deadzone → center). Covered today:

| Param | low (&lt;50%) | high (&gt;50%) |
|---|---|---|
| Blend (unipolar) | `SP` | `SW` |
| Waveshape | `SA` (Saw) | `FO` (Fold) |
| Umbra/Aurora | `UM` | `AU` |
| Atmosphere | `BL` (Blur) | `RD` (Radiation) |
| Character | `CH` (Chorus) | `FR` (Friction) |

On **bipolar** macros the pole name **replaces ±** (`UM 42`, not `UM -42%`). While a pole
hint is visible the trailing **`%` is omitted** as well — otherwise the right-hand label
overwrote the percent glyph on the SSD1309. Plain ± pitch / velocity (PSP, PSW, PB span
display, Pan/Crossfade Velocity, …) and all other params **keep `±%` / `%`**. Blend uses
`SP 42` / `42 SW` the same way. Center (no hint) still shows a bare number or `0%` /
`+0%` per type as before when no pole is named.
3. **Toggle (2 states, e.g. On/Off, Cont. Rec, Overwrite, Quantized, Instant Playback)** — no bar. Both
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
| 6 | Engine blend (Block 3) ✔ | Continuous crossfading Spectra↔Swarm |
| 7 | Spectral Resonator ✔ | Mix/Decay/Damping/Spread/Pitch/Quantized active, intonation from Settings effective |
| 8 | Reverb & Filter Mix ✔ | ReverbSc + Character; SVF LP Filter Mix with Destination |
| 9 | Pan Drift & Crossfade ✔ | Phase-offset pan LFOs, crossfade slew, focus ticks on Home |
| 10 | Mod system & control I/O | 4 slots, 3rd mux chain, Trail-push ladder, jack normalling, Clock/Trig/Imprint jacks, Amplitude+Offset+Destination+Divider, half-range bias |
| 11 | Multi & Settings & Calibration | Dry/Wet/Macros, Settings submenu complete, Imprint jack mode, persisted CV zero offsets |

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
- Real CycleRow for Block 1 (Count, Threshold, Cont. Rec, Overwrite, On/Off) and Block 2
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
CycleRow Block 5: Size, Spread, Scan, Direction (Fwd/Rev/Rnd), Atmosphere
(Blur↔Radiation + BBD slew).
Pitch Swarm + temporary Engines A/B toggle (Swarm ON/OFF). Register in
ParameterRegistry. Audio: dry×0.85 + selected engine wet.
```

### Phase 6 — Engine Blend

**✔ Completed (verified against implementation).** See the Block 3 contract above
(equal-power pre-fader crossfade, extreme-skip, FFT gating at blend ≥ 0.98).

```
Prompt for Cursor (historical — already implemented):

Read ARCHITECTURE.md first, especially 4.1 (Block 3).

Replace the A/B switch with real pre-fader crossfading between the Spectra and
Swarm outputs. Full CycleRow for Block 3: Blend, Pitch Spectra, Pitch Swarm —
consolidate the pitch values set directly in Phase 4/5 into this here.
```

### Phase 7 — Spectral Resonator

**✔ Completed (verified against implementation).** See Block 7 / Spectral Resonator contract above.

```
Prompt for Cursor (historical — already implemented):

Read ARCHITECTURE.md first, especially 4.1 (Block 7 and the Settings submenu).

Implement the Spectral Resonator on the Swarm output: a bank of tunable bandpass
resonators. CycleRow Block 7: Mix, Decay, Pitch, Quantized (On/Off, forces
pitches onto the scale chosen in Settings). Read the Intonation switch (Equal
Temperament / Just Intonation) from the Settings submenu (Block 11) and apply it
to the resonator tuning.
```

### Phase 8 — Reverb & Filter Mix

**✔ Completed (verified against implementation).** See Block 6 / Reverb and Block 10 / Filter
Mix contracts above. ReverbSc lives in DaisySP-LGPL (include + compile shim); engine BSS in
SDRAM.

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

### Phase 9 — Pan Drift & Crossfade

**✔ Completed (verified against implementation).** See Block 8+9 contract above (Pan Drift LFOs +
CloudPan, Crossfade focus lobe on VCA, BBD soft-replace, Home focus ticks; Wandering Beams
removed as redundant with the Life-Bar).

```
Prompt for Cursor (historical — already implemented):

Read ARCHITECTURE.md first, especially 4.1 (Block 8+9) and 4.9.

Implement:
- Pan Drift: an independent LFO per Trail (triangle/sine blend + slight jitter),
  constant-power panning. CycleRow Block 8: Phase (phase offset between the
  Trails' LFOs, 0%=synchronous, 100%=maximally offset), Amplitude (excursion),
  Velocity (speed). **Route inside the wet bus before Multi Dry/Wet** (2 / 5a).
- Crossfade: a traveling amplitude wave across the active Trails per 4.1 (Block
  9 detail), multiplicative on the VCA stage AFTER the pre-fader taps. CycleRow
  Block 9: Amplitude (wave depth), Velocity (bipolar: sign = travel direction,
  4% deadzone, center = focus freeze); plus slew-rate limiting (BBD-style) on
  round-robin replacement of a Trail. Still part of the wet chain Multi blends
  against clean input (2 / 5a) — do not touch the Multi dry tap.
- Display: Crossfade focus ticks beside `T#` on Home; Hold remaining via Life-Bar
```

### Phase 10 — Mod System & Control I/O

This phase carries a **hardware revision**, not just firmware: a third mux chain, the
Trail-push ladder, single-supply CV conditioning and three trigger jacks. Read 4.4b and 4.5a
before touching `hw_pins.h` — the pin map from Phase 2 is no longer valid.

```
Prompt for Cursor:

Read ARCHITECTURE.md first, especially Section 2 (points 6 and 7), 4.3, 4.4,
4.4b, 4.5a and 4.7b.

Hardware layer first — the pin map changed:
- Add Mux C as a third AdcChannelConfig: InitMux(D17, 8, D0, D1, D2).
  Mux C C0-C3 = Mod CV 1-4, C4-C7 = Mod pots 1-4.
- Move Trail pushes 1-5 off D14/D17-D20 onto the priority resistor ladder on
  Mux A C5. Window decoding at ~0.6V spacing plus debounce; the existing
  Lock/Solo long-press timers stay untouched, only the source changes.
- Jack detect: Mod CV 1-4 on Mux A C6-C7 and Mux B C6-C7 (digital via ADC,
  same pattern as kMultiPush), Clock detect on GPIO D20. Threshold ~0.6
  normalised, hysteresis, ~20ms debounce.
- Trigger inputs on GPIO: D14 Clock, D18 Trig, D19 Imprint. Read the Clock pin
  once per sample inside the audio callback and timestamp it with the sample
  counter; Trig and Imprint at block rate. All three set atomic flags,
  evaluation happens in the UI tick.
- Trig shares the RequestManualTrigger path with the Rec button (4.5); the
  legacy kTrigInput alias can now point at a real pin.
- Clock: edge detect, period measurement, feed Buffer/Hold when Time Unit =
  Clock, drive the mod Dividers (4.3) and Auto-Mod rates (4.10). Jack-detect
  transitions raise the Time Unit prompt (4.1a); clock loss (no edge for ~4x
  the last period, or ~3s) is a separate fallback and must not be conflated
  with cable presence.
- Imprint jack: gate-following lock per 4.7b, writing to the SAME owner set as
  the D6 button. Add the Settings entry for the alternative jack modes as a
  stub; wiring it live is Phase 11.
- CV zero: while a mod jack reports "unplugged", its ADC channel reads the
  designed centre - use that as a continuously learned per-channel zero
  offset. Persisting it is Phase 11.

Then the 4 mod slots: jack detection via switched contact (not a voltage
heuristic) - plugged cable = external CV is read, otherwise an internal source
per the Auto-Mod setting (4.10): OFF = simple triangle/sine with rate = clock
period x divider; Age/Pitch/Both see 4.10 (the Age envelope and Pitch tracking
value get wired to the Settings menu in Phase 11, but set up the source
abstraction already here). CycleRow per slot: Amplitude (bipolar attenuverter,
4% deadzone), **Offset** (bipolar bias after the attenuverter, 4% deadzone -
shifts the contrib window fully into the negative half, fully into the positive
half, or anywhere between; required so destinations like Pitch Both / Pitch
Spectra/Swarm can be modulated *only downward* or *only upward*, not merely
scaled symmetrically - see 4.3), Destination (from the ParameterRegistry, all
Blocks 1-11 plus Trail Level), Divider. Apply
`contrib = Offset + Amplitude x source` then clamp onto the destination.

The 4% deadzone applies to Amplitude and Offset ONLY - never to Destination or
Divider, even though all four share one physical pot.

EMA smoothing: light on mod CV (sluggish modulation is audible), heavier on
pots.
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
  Imprint jack mode (Gate-follow/Pulse Play-Pause/Gate-length, see 4.7b —
  default Gate-follow),
  Audio Routing toggle (Stereo/Sidechain, see the 4.1 detail description): in
  Sidechain mode, the buffer signal source prepared in Phase 3 gets switched
  exclusively to In R, In L is mixed directly (dry, bypassing the Trail
  buffers) with the Spectra/Swarm/Reverb output onto Out L/R,
  **FX → Input** amount (11-step 0…1, default 0 — how much listen-through is
  also sent into the wet FX chain; see 4.1 Settings item 7 / Section 8)
- Calibration: persist the per-channel mod CV zero offsets that Phase 10
  already learns continuously from unpatched jacks (4.4b) into flash, plus a
  min/max learning mode for the threshold. No trimmers exist on the carrier,
  so this routine is the only calibration path there is
- Review: check all display text for readability at 128×64px
```

---

## 8. Open Points

- **~~Exact GPIO pin assignment~~ Resolved (revised for Phase 10):** see 4.5a. **Multi
  live:** CLK **D13**, DT **D30**, Push **Mux B C5**; Dry/Wet default + Multi menu
  (Macro1/2/Time Unit/Settings dummies); long push = Home. Full Settings submenu / macros =
  Phase 11. Rec **D12**. **D14 is now the Clock trigger** — Trail pushes moved to the ladder
  on Mux A C5.
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
- **TODO — FX → Input amount (Settings via Multi):** optional send of clean listen-through
  into the wet FX chain (alongside engines), so effects can also color the input. Lives in
  the **Settings submenu** reached from the Multi encoder cycle. **11 steps, 0…1** (0.0 /
  0.1 / … / 1.0). **Boot default = 0** (effects on processed signal only — Multi@100% wet
  stays cloud-only). At >0, scale listen-through into Reverb send / shared FX bus by that
  amount without breaking Multi Dry/Wet as the final blender. Wire in Phase 11 with the full
  Settings submenu.
- **✔ Resolved — Spectral Resonator presence:** the culprit was not makeup gain but the Decay
  mapping, which never reached a Q that rings (see Block 7 above). Decay is now a real T60,
  the `1/8` average became a vector norm, and Damping + Spread were added. Still Swarm-only —
  routing was left alone deliberately.
- **TODO — Play / Rec illuminated switch LEDs:** panel switches may have built-in LEDs
  (Play = green solid / blink for playing vs paused; Rec = red while armed/recording).
  Drive from Daisy GPIO if pin budget allows — **it no longer does.** After the Phase 10
  revision every GPIO and all 24 mux channels are assigned (4.5a), so LEDs need carrier glue
  (shift register / LED driver on a shared bus), or one of the two expansion valves in
  Section 2 point 6 has to be spent on them. Confirm polarity/current on the carrier before
  assigning anything. Not required for V1 audio; UI polish.
- **~~Swarm grain playback direction~~ Resolved:** Block 5 **Direction** (`DIR`) —
  Fwd / Rev / Rnd as CountNum; Rnd = per-grain coin flip at spawn; Scan scrub unchanged.
- **~~Clock jack + Trig jack~~ Resolved (Phase 10):** both now have dedicated GPIO with
  74HC14 conditioning (4.4b) and a fixed place in the pin map (4.5a). Clock = **D14** with
  jack detect on **D20**, Trig = **D18** on the `RequestManualTrigger` path, Imprint trigger
  = **D19**. Expected levels, thresholds and hysteresis are specified in 4.4b; edge detect,
  period measurement, the Time Unit prompt and the clock-lost fallback are part of the
  Phase 10 prompt. Still to nail down at the bench: minimum/maximum usable tempo, behaviour
  while Time Unit is still a Multi dummy, and the interaction with Cont. Rec / Overwrite.
- **Macro1/Macro2 target assignment:** currently fixed in code (Phase 11), no front-panel UI decided for it yet
- **~~Multi encoder push function~~ Resolved:** short = step through the Multi cycle list, long = global return to the Home Dashboard (see 4.7a)
- **Pot/encoder turn direction:** clockwise = which state for toggles (left/right, see 4.11), which direction for bipolar values — depends on the final hardware wiring, not yet determined
- **Macro1/Macro2 in the display design system:** once the target assignment (see above) is settled, it needs to be determined which of the 4 display types from 4.11 applies, depending on what's assigned
- **Modulation mechanic on the registry base value:** additive directly on the value pointer (with min/max clamping), or a separate modulation offset that only gets combined with the base value in the audio callback? Also affects whether the display (see 4.11, modulated actual value) shows the actual registry value or a separate actual value
- Preset storage: architecturally prepared via the ParameterRegistry (flash persistence), but planned only after the V1.0 firmware stabilizes — pickup/catch (4.6) will then also apply to preset recall, once presets exist
