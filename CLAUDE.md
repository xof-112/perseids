# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Perseids — Eurorack-style firmware for the Electrosmith Daisy Seed (STM32H750, Cortex-M7 @ 480 MHz,
64 MB SDRAM, SSD1309 128×64 OLED). Captures audio into up to 5 "Trails" (SDRAM ring buffers) and
shapes a stereo cloud with Spectra (additive) and Swarm (granular) engines, then Resonator, Reverb,
Filter, Pan Drift and Crossfade. PlatformIO + libDaisy + DaisySP.

**`ARCHITECTURE.md` is the source of truth** (~2100 lines). Read the relevant section before changing
code: §2 mandatory C++/DSP guardrails, §2a performance playbook (read first when debugging CPU or
freezes), §4.1 per-block DSP contracts, §6/§7 phase roadmap. `ARCHITECTURE-2026xxxx*.md` and
`.archive/` are historical snapshots. Do not edit `ARCHITECTURE.md` or `PANEL_and_PINOUT.md` —
report MATCHES / MISMATCHES / OPEN QUESTIONS instead (see `.claude/rules/`).

## Commands

```bash
pio run                      # build (env daisy_seed)
pio run --target upload      # flash via dfu-util to QSPI 0x90040000
pio run --target clean
pio run --target compiledb   # regenerate compile_commands.json (used by .clangd)
arm-none-eabi-size -A .pio/build/daisy_seed/firmware.elf   # real memory usage
```

- There is no test suite. Verification is on the bench; each phase has a test criterion
  (ARCHITECTURE.md §6).
- Upload: app runs under the Daisy bootloader (`BOOT_SRAM`). Reset the Seed and upload within the
  ~2.5 s grace window (hold BOOT to extend). ST DFU (BOOT+RESET) is only for installing the
  bootloader itself.
- `lib/libDaisy` and `lib/DaisySP` are vendored git clones, not `lib_deps`. ReverbSc needs the
  `DaisySP-LGPL` submodule and is compiled via `src/daisysp_reverbsc.cpp`.
- Don't trust PlatformIO's "Flash %": under BOOT_SRAM code lives in AXI-SRAM (480 KB) and
  `.data`/`.bss` in DTCM (128 KB — the tighter budget, ~59 % used). Check with `arm-none-eabi-size -A`.

## Build configuration gotchas (platformio.ini)

- `build_src_flags = -O3` applies to `src/` only; libDaisy forces `-O3` itself, DaisySP stays `-Os`.
- `-fno-math-errno -ffp-contract=fast` are deliberate; do not switch to global `-ffast-math`.
- `link_cmsis_dsp.py` builds a "lite" CMSIS-DSP subset (RFFT-2048 + its 1024 CFFT tables, mult,
  cmplx_mag). Any new CMSIS function or FFT size requires adding sources/table defines there.
- `-D PERSEIDS_SCAN_INTERVAL_PROBE` (USB serial + LED probe, `include/scan_interval_probe.h`) is a
  temporary bench measurement, meant to be removed afterward.
- `.clangd`, `compile_commands.json` and `.vscode/` contain absolute paths to the project folder;
  update them if the checkout moves.

## Architecture

**Two execution contexts, strictly separated** (`src/main.cpp`):
- `AudioCallback` (ISR, block size 256 @ 48 kHz): `g_capture.Process` → Spectra / Swarm (equal-power
  Blend, silent side skipped) → Resonator → Filter → Reverb → final Multi Dry/Wet with rational
  soft-clip. No allocation, blocking, logging, or string formatting here.
- Main loop: `ui.Process()` (encoders, 74HC165/595 shift-register chains, gestures, display), then
  `g_spectra.ProcessAnalysis()` (FFT, polled every 10 ms). FFT and LUT rebuilds never go in the
  callback.

**Engine pattern** (`include/*_engine.h`, `src/*_engine.cpp`): each engine has `Init(sample_rate)`,
`SyncFromUi(XxxParamValues)` (main loop; converts UI values, dirty-checks, rebuilds tables) and
`Process(...)` (audio callback only). Large buffers live in SDRAM via `DSY_SDRAM_BSS`; DTCM is scarce.

**Parameters**: each block has an `include/*_params.h` with a stable numeric ID enum (IDs grouped by
block, e.g. Swarm = 50–55) and a `*ParamValues` struct with boot defaults. Every parameter is
registered in `ParameterRegistry` (`param_registry.h`, max 128) with range, display type, abbrev and
value pointer. UI pages (Block / Mod / List page) are generic and data-driven — a parameter ID list
plus row split — and read/write only through the registry. No per-block UI code, no second data path.

**UI**: `ui_controller` (input → registry, page/gesture state), `display_renderer` (OLED drawing),
`cycle_row` (segmented carousel row), `button_gesture`, `quadrature_encoder`, `mux_adc` (4:1 mux for
the 4 mod CV inputs only). Pin assignments in `hw_pins.h`, matching `PANEL_and_PINOUT.md`.

**Capture**: `capture_engine` + `record_source.h` (block-rate input routing with hysteresis) +
`trail_level` (per-Trail Level/Lock/Solo, pre-fader taps).

## Non-obvious rules (from ARCHITECTURE.md §2)

- Routing/gate decisions (jack present, threshold crossed) are made per block on a smoothed envelope
  with wide hysteresis and slewed gains — never per sample against a fixed threshold.
- All effects sit before Multi Dry/Wet; nothing may feed the dry listen-through into the wet bus.
  Taps are pre-fader: a Trail at level 0 is still a source.
- No `%f` in printf (newlib-nano) — format seconds/values with integer math.
- 4 % bipolar center deadzone applies only to mod CV inputs, not encoders.
- Swarm output intentionally keeps `tanh` (B3); don't "optimise" it away without A/B listening.
- Never guess libDaisy/DaisySP APIs — grep the headers in `lib/` first.

## Workflow conventions

- Work proceeds in phases (ARCHITECTURE.md §7); Phase 12 (encoder UI rework) is built before
  Phases 10/11. Commit messages: `Phase N — <topic> YYYYMMDD`. Bench builds are tagged
  `dev-phaseNvXXX`.
- Branch `vcv-port` prepares a shared core for a VCV Rack 2 plugin (`vcv/`, `tools/host/` not yet
  present). The portability rules in `.claude/rules/vcv-port.md` already apply to shared core code
  (engines, registry, UI controller/renderer).

## disting NT plug-in (`nt/`)

- `nt/perseids_nt.cpp` is the NT platform layer; it compiles Capture/Spectra/Swarm from `src/`
  with `-DPERSEIDS_TRAIL_INT16`. `cd nt && make` (ARM .o), `make win` (nt_emu DLL),
  `make test` (native simulation, 76 checks), `make wintest` (Wine), `make render` (screens).
- Platform seams used by the engines: `CaptureEngine::TrailBank` (Trail storage),
  `SpectraEngine::Buffers`, `include/platform/trail_sample.h` (float on Daisy, int16 on NT),
  `SwarmEngine::SyncFromUi(params, now_ms)`. Engines no longer include libDaisy headers.
- The NT has no main loop: it uses `SpectraEngine::AnalysisSlice()` and
  `SwarmEngine::SetParams()/WindowSlice()` from the audio thread. The Daisy keeps
  `ProcessAnalysis()` / `SyncFromUi()`; both paths must stay equivalent.
- Changes to the engines affect both targets: run `nt/` `make test` and keep the firmware
  building.
