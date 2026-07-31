# Perseids

Eurorack-style firmware for the **Electrosmith Daisy Seed** (libDaisy / DaisySP): capture audio
into up to five **Trails**, then shape a stereo cloud with **Spectra** (additive) and **Swarm**
(granular), plus resonator, reverb, filter, and spatial motion.

DSP is **global and pre-fader**; each Trail only has a light mixer tap (Level / Lock / Solo).
**Multi Dry/Wet** is the final blender between clean listen-through and the finished wet bus.

Inspired by [Coastline](https://aqeelaadamsound.com/b/coastline) by Aqeel Aadam Sound —
independently designed, not affiliated.

![Perseids Phase 9 v003 bench](images/dev-phase9v003.jpg)

---

## Status — Phase 9 · `dev-phase9v003`

Phase 9 (**Pan Drift** + **Crossfade**) is complete. This bench line also folds in post-9
polish on the Home life bar, Multi → Settings defaults, and wet-bus balance.

| What’s in | Notes |
|-----------|--------|
| Capture (SDRAM rings, THR / CRE / OVR, Hold · FIN · FOUT) | Working |
| Home dashboard (VU, life bars, REC header, CPU / `L!`) | Working |
| Life-Bar **REC** styles | **PLR** (default) · **PRS** · **CTR** — Settings |
| Spectra · Swarm · Blend · Resonator · Reverb · Filter | Working |
| Pan Drift · Crossfade (focus ticks on Home) | Working |
| Multi (D/W · M1 · M2 · TU · SET) | Cycle short / Cycle-hold+Multi steps the list |
| Settings | CPU · RAM · SCL · INT · **REC** · **LVL** · **#T** |
| Default Trail level / count | LVL 0…100% @ 5% · #T 1…5 (boot 3 / 50%) |
| Daisy **BOOT_SRAM** | App in QSPI → AXI-SRAM; room for `-O3` |

| Still ahead | |
|-------------|--|
| **Phase 10** | Mod system (4 slots) |
| **Phase 11** | Multi macros, Time Unit, calibration, FX→Input |

Full roadmap & DSP contracts: [`ARCHITECTURE.md`](./ARCHITECTURE.md)

**Cheat sheets** (3-page)

- HTML (EN/DE toggle): [`docs/perseids-cheat-sheet.html`](./docs/perseids-cheat-sheet.html)
- PDF DE: [`docs/perseids_cheatsheet_3page.pdf`](./docs/perseids_cheatsheet_3page.pdf)
- PDF EN: [`docs/perseids_cheatsheet_3page_en.pdf`](./docs/perseids_cheatsheet_3page_en.pdf)

Bench photo: [`images/dev-phase9v003.jpg`](./images/dev-phase9v003.jpg)

---

## Hardware

- Electrosmith Daisy Seed (STM32H750 @ **480 MHz**, 64 MB SDRAM)
- SSD1309 OLED 128×64 (SPI)
- Custom carrier (10 block pots, Multi + 5 Trail encoders, Cycle / Imprint / Rec, jacks)

**Bootloader:** `BOOT_SRAM` — Daisy bootloader in internal flash; app binary at QSPI
`0x90040000`, copied to AXI-SRAM on boot. Reset into the ~2 s upload window (hold **BOOT** to
extend), then PlatformIO upload. ST DFU (BOOT+RESET) only to install/replace the bootloader.
See `ARCHITECTURE.md` §2a / Block 4.

---

## Building

```bash
git clone https://github.com/xof-112/perseids.git
cd perseids
# optional: git checkout <tag>   # e.g. when a phase9v003 tag is published

git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git lib/libDaisy
git clone --recurse-submodules https://github.com/electro-smith/DaisySP.git lib/DaisySP
# ReverbSc is LGPL — init DaisySP-LGPL if needed:
#   cd lib/DaisySP && git submodule update --init DaisySP-LGPL

# One-time: flash Daisy bootloader via ST DFU if the Seed is still stock
#   dfu-util -a 0 -s 0x08000000:leave \
#     -D lib/libDaisy/core/dsy_bootloader_v6_4-intdfu-2000ms.bin -d ,0483:df11

pio run
# Reset Daisy (LED pulses) — hold BOOT if needed — then:
pio run --target upload
```

---

## License

**GPL-3.0** — see [`LICENSE`](./LICENSE).  
libDaisy / DaisySP are MIT; **DaisySP-LGPL** (`ReverbSc`) is LGPL-2.1.
