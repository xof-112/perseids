
# Perseids

Firmware for **Perseids**, a Eurorack-style granular/spectral audio module on the
Electrosmith Daisy Seed (libDaisy/DaisySP).

Up to 5 audio voices (**Trails**) are captured in a round-robin pool and later processed
by global resynthesis engines (**Spectra** / **Swarm**), resonator, reverb, and filter.
Processing is global and pre-fader; each Trail only has a light mixer tap (Level/Lock/Solo).

Conceptually inspired by [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam
Sound — independently designed, not affiliated.

---

## Status — Phase 7 · Spectral Resonator

**Spectral Resonator is in:** Block 7 runs an 8-bandpass bank on the Swarm output
(Mix / Decay / Pitch / Quantized). Settings now expose Scale (Major/Minor/Pentatonic) and
Intonation (Equal/Just) for Quantized mode. Engine Blend (Phase 6) remains: continuous
Spectra↔Swarm crossfade. Remaining dummy menus: Reverb, Pan Drift, Crossfade, Filter.

![Perseids Phase 3 v001 bench setup](images/dev-phase6v001.jpg)

| Area | State |
|------|--------|
| UI / ParameterRegistry / Cycle rows | Working (all 10 block pots) |
| Trail Level, Lock/Solo, Rec/Trig | Working |
| Capture engine (SDRAM rings, threshold, Cont.Rec, Hold/FIN/FOUT) | Working |
| Dashboard (VU, life bars, Count-limited trails, CPU meter) | Working |
| Spectra engine (additive: Partials, Waveshape, Umbra/Aurora, Ensemble) | Working |
| Swarm engine (granular: Size, Spread, Scan, Atmosphere) | Working |
| Engine blend (continuous Spectra↔Swarm, equal-power, pre-fader) | Working |
| Spectral Resonator (on Swarm: Mix/Decay/Pitch/Quantized + Settings Scale/Intonation) | Working |
| Blocks 6/8–10 (Reverb, Pan Drift, Crossfade, Filter) | Dummy menus — engines follow in Phases 8–9 |
| Reverb & Filter Mix | **Next** — Phase 8 |

Tag: **`dev-phase6v001`** (bench photo) · Full roadmap: [`ARCHITECTURE.md`](./ARCHITECTURE.md)

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
