# Perseids

Firmware for **Perseids**, a Eurorack-style granular/spectral audio module on the
Electrosmith Daisy Seed (libDaisy/DaisySP).

Up to 5 audio voices (**Trails**) are captured in a round-robin pool and later processed
by global resynthesis engines (**Spectra** / **Swarm**), resonator, reverb, and filter.
Processing is global and pre-fader; each Trail only has a light mixer tap (Level/Lock/Solo).

Conceptually inspired by [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam
Sound — independently designed, not affiliated.

---

## Status — Phase 3 · `dev-phase3v001`

**Phase 3: v001 — started to work on resynth engine (tbc).**

| Area | State |
|------|--------|
| UI / ParameterRegistry / Cycle rows | Working (Block 1 Trails + Block 2 Time) |
| Trail Level, Lock/Solo, Rec/Trig | Working |
| Capture engine (SDRAM rings, threshold, Cont.Rec, Hold/FIN/FOUT) | Working — direct buffer playback |
| Dashboard (VU, life bars, Count-limited trails) | Working |
| Spectra / Swarm resynthesis | **Next** — work started / tbc |

Tag: **`dev-phase3v001`** · Full roadmap: [`ARCHITECTURE.md`](./ARCHITECTURE.md)

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
git checkout dev-phase3v001   # this milestone
git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git lib/libDaisy
git clone https://github.com/electro-smith/DaisySP.git lib/DaisySP
pio run
pio run --target upload
```

---

## License

**GPL-3.0** — see [`LICENSE`](./LICENSE). libDaisy / DaisySP are MIT.
