
# Perseids

Firmware for **Perseids**, a Eurorack-style granular/spectral audio module on the
Electrosmith Daisy Seed (libDaisy/DaisySP).

Up to 5 audio voices (**Trails**) are captured in a round-robin pool and later processed
by global resynthesis engines (**Spectra** / **Swarm**), resonator, reverb, and filter.
Processing is global and pre-fader; each Trail only has a light mixer tap (Level/Lock/Solo).

Conceptually inspired by [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam
Sound — independently designed, not affiliated.

---

## Status — Phase 6 · `dev-phase6v001`

**Engine Blend is in:** the Engines block now crossfades continuously between Spectra
(additive, FFT 512 + phasor bank) and Swarm (granular over the Trail SDRAM buffers) —
equal-power, pre-fader, with a subtle 50% marker and an SP/SW side hint on the display.
All 10 block pots are wired: Blocks 6–10 (Reverb, Resonator, Pan Drift, Crossfade,
Filter) already open full dummy menus for bench feedback until their engine phases land.

![Perseids Phase 3 v001 bench setup](images/dev-phase3v001.jpg)

| Area | State |
|------|--------|
| UI / ParameterRegistry / Cycle rows | Working (all 10 block pots) |
| Trail Level, Lock/Solo, Rec/Trig | Working |
| Capture engine (SDRAM rings, threshold, Cont.Rec, Hold/FIN/FOUT) | Working |
| Dashboard (VU, life bars, Count-limited trails, CPU meter) | Working |
| Spectra engine (additive: Partials, Waveshape, Umbra/Aurora, Ensemble) | Working |
| Swarm engine (granular: Size, Spread, Scan, Atmosphere) | Working |
| Engine blend (continuous Spectra↔Swarm, equal-power, pre-fader) | Working |
| Blocks 6–10 (Reverb, Resonator, Pan Drift, Crossfade, Filter) | Dummy menus — engines follow in Phases 7–9 |
| Spectral Resonator | **Next** — Phase 7 |

Tag: **`dev-phase6v001`** · Full roadmap: [`ARCHITECTURE.md`](./ARCHITECTURE.md)

---

## Hardware

- Electrosmith Daisy Seed (STM32H750, SDRAM)
- SSD1309 OLED 128×64 (SPI)
- Custom carrier PCB (pots, encoders, buttons, jacks)

---

## Building

```bash
git clone https://github.com/xof-112/perseids.git
cd perseids
git checkout dev-phase6v001   # this milestone
git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git lib/libDaisy
git clone https://github.com/electro-smith/DaisySP.git lib/DaisySP
pio run
pio run --target upload
```

---

## License

**GPL-3.0** — see [`LICENSE`](./LICENSE). libDaisy / DaisySP are MIT.
