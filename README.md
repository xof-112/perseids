# Perseids

Firmware for **Perseids**, a Eurorack-style granular/spectral audio module built on the
Electrosmith Daisy Seed (libDaisy/DaisySP).

Perseids captures up to 5 audio voices ("**Trails**") in a round-robin pool and processes
them through two global resynthesis engines — **Spectra** (additive) and **Swarm**
(granular) — followed by a spectral resonator, reverb, and filter stage. Processing is
deliberately global and pre-fader, with a lean per-Trail mixer (Level/Lock/Solo) to keep the
hardware UI simple despite the depth of the engine underneath.

Conceptually inspired by [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam
Sound, but independently designed and developed from scratch — not affiliated with or
endorsed by Aqeel Aadam Sound.

---

## ⚠️ Project Status

**Early development.** This project is currently at Phase 0/1 of the roadmap described in
[`ARCHITECTURE.md`](./ARCHITECTURE.md) — UI mechanics and parameter infrastructure are
being built on dummy values before any real audio DSP is implemented. Not yet functional as
an instrument.

---

## Hardware

- **MCU:** Electrosmith Daisy Seed (STM32H750, 480 MHz Cortex-M7, 64 MB SDRAM)
- **Display:** SSD1309 OLED, 2.42″, 128×64 px, monochrome
- **Controls:** 19 pots, 1 encoder, 2 buttons
- **I/O:** stereo in/out, clock in, trig in, 4 mod CV inputs (normalled)
- **Custom carrier PCB** (not included in this repo)

See [`ARCHITECTURE.md`](./ARCHITECTURE.md), Sections 1–4, for the full hardware and UI
specification.

---

## Documentation

[`ARCHITECTURE.md`](./ARCHITECTURE.md) is the single source of truth for this project —
architecture, DSP guardrails, UI mechanics, display design system, and the full phase-by-phase
development roadmap with ready-to-use prompts for AI-assisted development in Cursor.

---

## Building

This project uses [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/xof-112/perseids.git
cd perseids
git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git lib/libDaisy
git clone https://github.com/electro-smith/DaisySP.git lib/DaisySP
pio run
pio run --target upload
```

**Note:** libDaisy/DaisySP are not officially maintained in the PlatformIO registry — see the
"Reality check" note in `ARCHITECTURE.md`, Section 0, if the build doesn't go through
cleanly on the first try.

---

## License

Licensed under the **GNU General Public License v3.0** (GPL-3.0) — see [`LICENSE`](./LICENSE)
for the full text. In short: you're free to use, modify, and redistribute this code, including
commercially, but any derivative work that you distribute must also be licensed under
GPL-3.0 and its source made available.

libDaisy and DaisySP are MIT-licensed and compatible with GPL-3.0 as dependencies.

---

## Credits

- [libDaisy](https://github.com/electro-smith/libDaisy) & [DaisySP](https://github.com/electro-smith/DaisySP) by Electrosmith
- Conceptual inspiration: [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam Sound
