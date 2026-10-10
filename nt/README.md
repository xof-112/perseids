# Perseids-core für den disting NT

Der Kern von Perseids als Algorithmus für den Expert Sleepers **disting NT**: fünf **Trails** aufnehmen, daraus mit **Spectra** (additive FFT-Resynthese) und **Swarm** (granular) eine Stereo-Wolke formen, dazwischen **Blend**. Was der NT selbst oder ein Modul im Pult schon kann, ist bewusst draußen (siehe unten).

Die Engines sind dieselben Quelldateien wie in der Daisy-Firmware (`../src/capture_engine.cpp`, `spectra_engine.cpp`, `swarm_engine.cpp`). Ein Fehler, der dort behoben wird, ist in beiden behoben. Dieser Ordner enthält nur die NT-Plattformschicht: Speicher, Routing, Parameter, Bedienung, Display.

Gebaut gegen distingNT_API v14 (Submodul `distingNT_API`, Stand 6975a63). GPL-3.0 wie Perseids.

## Installation

- **NT:** `plugins/perseids.o` nach `programs/plug-ins/` auf die microSD-Karte, NT neu starten, Algorithmus **Perseids** hinzufügen. Beim Hinzufügen fragt der NT nach **Trail seconds** (1–30 s, Standard 10): so viel Aufnahmezeit bekommt jeder Trail. 16 Bit pro Sample, also 5 × 10 s ≈ 4,9 MB DRAM, 5 × 30 s ≈ 14,5 MB.
- **nt_emu (VCV Rack, Windows):** `emu/perseids.dll` nach `%LOCALAPPDATA%\Rack2\disting-nt-plugins\`. Braucht dieselbe für Windows gebaute nt_emu-Fassung wie Duett (Anleitung in `disting-nt-plugins/duett-nt/README.md`, Abschnitt „Am PC ausprobieren“). Die DLL bringt ihre eigene Brücke mit (`emu/nt_win_shim.cpp`, aus Duett übernommen).

## Bedienung

| Element | Funktion |
|---|---|
| Poti L | **Blend** Spectra ↔ Swarm |
| Poti C | **Scan** (0 = Freeze) |
| Poti R | **Size** (Grain-Anzahl 4–24); Poti R drücken schaltet auf **Atmosphere** (Blur ← 0 → Radiation) und zurück |
| Encoder L drehen / klicken | Trail wählen / **Solo** für den gewählten Trail |
| Encoder R drehen / klicken | **Level** des gewählten Trails (2 % pro Raste) / **Lock** |
| Taste 3 | **Rec**: sofort in den nächsten Trail aufnehmen |
| Taste 4 | **Hold** unendlich ein/aus (gilt ab der nächsten Aufnahme; laufende Trails hält **Lock** fest) |

Tasten 1/2 bleiben beim NT. Ein Encoder-Klick zählt nur ohne Drehen; in nt_emu drückt jedes Ziehen am Encoder ihn mit, das wird als Drehen gewertet (wie bei Duett).

Display: Kopfzeile mit REC-Anzeige (bzw. dem nächsten Ziel-Trail), Eingangspegel L/R mit Threshold-Marke und Blend-Stellung. Darunter fünf Spalten: Trail-Nummer, Zustand (`REC`, `IN`, Sekunden bis zum Ausblenden, `INF`, `OUT`), Lebensbalken, Level-Balken, `L`/`S` und Level in %. Der gewählte Trail ist umrahmt, Trails über *Count* sind dunkel. Fußzeile: was die drei Potis gerade tun, `HOLD` bei unendlichem Hold, `CPU`, wenn der Swarm-Governor Grains spart.

**Lebensbalken wie am Modul:** Während der Aufnahme ziehen Funken durch den Balken, portiert aus `DrawTrailLifeBar` / `DrawRecSparkleFill` der Firmware (gleicher Hash, gleiche Tempi, gleiche Dichte pro Pixel, 200 ms weiches Erscheinen bzw. Verglühen). Stil über *Rec style* auf der Seite *Display*, wie Settings → REC: **PLR** (Standard, Funken links → rechts bis zur Aufnahme-Front), **PRS** (aus der Mitte), **CTR** (voller Balken aus der Mitte). Danach füllt Fade In den Balken von links, Hold steht voll, Fade Out leert ihn von links. Die Funken sind wie am Modul voll hell; der NT hat 16 Graustufen, deshalb zieht jeder Funke zusätzlich einen schwachen Pixel (Stufe 5) hinter sich her. Sieht man die Abstufung kaum, sieht es aus wie am Modul.

Bildschirm-Renderings: `render/perseids-screens.png` (Schriften angenähert, Positionen echt).

## Parameter

Alle Regler sind normale NT-Parameter, also per CV oder MIDI steuerbar.

| Seite | Parameter |
|---|---|
| Trails | Count 1–5 · Threshold % · Cont. Rec · Overwrite · Capture · Play · Clear trails (bestätigen; auch per Gate) |
| Time | Buffer 0,1–30 s (Obergrenze = *Trail seconds*) · Hold 0–30 s / INF · Fade in · Fade out (0–5 s) |
| Engines | Blend % · Pitch Spectra ±24 HT · Pitch Swarm ±24 HT · Output level −24…+24 dB |
| Spectra | Partials 4–32 · Waveshape (Saw ← 0 → Fold) · Umbra/Aurora · Ensemble |
| Swarm | Size · Spread · Scan · Scatter · Atmosphere · Direction (Fwd/Rev/Rnd) |
| Mixer | Level 1–5 · Lock 1–5 · Solo 1–5 |
| Display | Rec style PLR / PRS / CTR |
| Routing | In L · In R (optional) · Rec trig in (Flanke über 1 V) · Out L/R mit Add/Replace |

Unterschiede zur Firmware, alle durch den NT bedingt:

- **Pitch in Halbtönen** statt ±100 % mit *Pitch Both*: Pitch Both ist fest auf 1 (Spanne ±2 Oktaven), dann sind ±24 Halbtöne exakt.
- **Pegel:** NT-Busse führen Volt. Eingang ±5 V = Codec-Vollaussteuerung, Ausgang × 5 V. *Output level* steht ab Werk auf +12 dB, weil der Wet-Bus von Perseids deutlich unter dem Eingang liegt (Trail-Level 50 %, Wolken-Panorama −3 dB). Danach dieselbe weiche Begrenzung wie im Multi der Firmware.
- **Blend** steht ab Werk auf 50 % (Firmware: 0 = nur Spectra), damit beide Engines gleich zu hören sind.
- **Trails in 16 Bit** (±2,0 Vollaussteuerung, eine Oktave Reserve über dem Codec-Bereich). Gegen die Float-Fassung gemessen liegt der Unterschied am Ausgang unter der 16-Bit-Quantisierung der Test-WAVs (−45 bis −55 dB).

## Bewusst nicht im Kern

| Weggelassen | Ersatz |
|---|---|
| Reverb | NT-Reverbs, Milky Way |
| Filter | NT-Filter, Wasp, SEM |
| Pan Drift, Crossfade | NT-Mixer/LFO, Four Play (Trails stehen in der Mitte, Swarm bringt die Breite über *Spread*) |
| Multi Dry/Wet | NT-Routing: Out mit *Add* auf den Eingangsbus legen, Pegel über *Output level* |
| Mod-System (Phase 10) | CV-/MIDI-Mapping des NT |
| VU, Life-Bars, Settings-Seiten | stark vereinfacht im Display |
| Resonator | noch offen, siehe unten |

## Wie es auf dem NT läuft

- **Keine Main-Loop:** Auf dem Daisy läuft die FFT-Analyse in der Hauptschleife. Der NT ruft nur `step()` auf (in nt_emu mit 4 Frames, am NT mit größeren Blöcken). Deshalb zerlegt `SpectraEngine::AnalysisSlice()` die Analyse eines Hops in 10 Scheiben (Fenster, FFT, Magnituden, 4 × Umbra/Aurora, Glättung, Peak-Auswahl); `step()` führt alle 32 Samples eine aus. Genauso baut `SwarmEngine::WindowSlice()` die Grain-Hüllkurve nach einer Atmosphere-Änderung in 9 Scheiben neu. So kostet kein einzelner Block mehr als eine Scheibe.
- **Blockgröße:** Audio läuft in Stücken von höchstens 64 Frames; die Kanalerkennung der Aufnahme (RecordSource) rechnet unabhängig von der Blockgröße alle 256 Samples, der Swarm-Governor ebenso. Getestet: gleicher Pegel bei 4, 24 und 128 Frames pro `step()`.
- **CPU-Governor:** Am NT misst `step()` seine eigenen Zyklen (`NT_getCpuCycleCount`) und gibt Swarm als Last den Anteil an der Hälfte eines 600-MHz-Kerns. Ab 88 % davon (also ~44 % des NT) dünnt Swarm die Wolke aus, Untergrenze 6 Grains. In nt_emu ist er aus.
- **Speicher:** SRAM nur für die Instanz (1,4 KB); Engines (31 KB), FFT-Puffer (48 KB) und Trails im DRAM. Keine statischen Variablen, mehrere Instanzen teilen nichts.
- **FFT:** dieselbe CMSIS-DSP-Teilmenge wie die Firmware (RFFT 2048), als Quelltext mit eingebaut.
- **Offene Symbole**, die die NT-Firmware auflösen muss: die NT-API, dazu `sinf cosf tanf expf powf tanhf memcpy memset strlen` (gleiche Art wie bei den Airwindows-Beispielen und Duett). `make` listet sie nach jedem Build.

## Bauen

Voraussetzungen wie bei disting-nt-plugins: `arm-none-eabi-gcc`, `g++`, für Windows `x86_64-w64-mingw32-g++` (posix), für den Rauchtest `wine`, für die Renderings Python 3 mit Pillow.

```
git submodule update --init nt/distingNT_API
cd nt
make            # plugins/perseids.o
make win        # emu/perseids.dll
make wintest    # DLL unter Wine laden, Audio, Zeichnen, Bedienung
make test       # Simulation (braucht die nt_emu-Schriften, NT_EMU_PATH, Standard ../../disting-nt-plugins/emu/nt_emu)
make render     # render/perseids-screens.png
```

## Tests

`test/sim.cpp` läuft ohne Hardware und prüft 76 Punkte, u. a.:

- Speicherbedarf je *Trail seconds*, Stille rein = Stille raus
- Spectra trifft 220 Hz, *Pitch Spectra* +12 → 440 Hz; Swarm mit *Pitch Swarm* +12 → 440 Hz und stereo
- Sieben Klangeinstellungen auf 40 s synthetischem Material (gezupfte Saiten, gesungenes „Aah“, Atem, Pausen): endlich, Spitzenpegel, Mindestpegel, **keine Sprünge** (Klick-Detektor)
- gleicher Pegel bei 4 / 24 / 128 Frames pro Block
- Capture aus, Rec-Trigger-Eingang, Lebenszyklus bis Fade-out, *Clear trails*
- Add/Replace, zwei Instanzen ohne gemeinsamen Zustand
- Bedienung mit NT- und nt_emu-Tastensemantik (Potis, Encoder, Klicks, Ziehen ≠ Klick, Tasten 3/4)
- Display: alles innerhalb von 256 × 64, alle drei Rec-Stile über eine Aufnahme hinweg

Die Hörproben landen in `test/out/` (Eingang, Spectra, Swarm, Blend, Blur, Radiation, Umbra+Fold, Aurora+Saw). **Eigenes Material:** `./test/sim aufnahme.wav` schickt eine WAV-Datei (16/24/32 Bit oder Float) mit Blend 0/50/100 % durch das Plug-in und schreibt `test/out/yours_blend*.wav`.

Der Klick-Detektor hat in der Firmware-Engine einen echten Fehler gefunden: Spectra brach Partials ab, wenn ihre Anzahl schrumpfte (Commit „Spectra: Partials beim Schrumpfen der Anzahl ausblenden“). Behoben in `src/`, gilt also auch für den Daisy.

## Stand

- Nativ getestet (76 Prüfungen), DLL unter Wine mit einem nt_emu-artigen Host geladen und gespielt.
- **Noch nicht auf dem NT und nicht in echtem VCV Rack gelaufen.**
- Firmware: Umbau der Engines bitgleich (Golden-Test vor/nach, 40 s Audio), Firmware-Link mit libDaisy geprüft.

## Offene Fragen

- **Resonator:** gehört er zum Klang von Perseids? Er ist der nächste Kandidat für den Kern (braucht nur DaisySP).
- **DRAM-Grenze des NT** für Plug-ins ist nicht dokumentiert; 5 × 30 s (14,5 MB) erst am Gerät ausprobieren.
- **Taste 4:** Hold für neue Aufnahmen (jetzt) oder „alles festhalten“ wie Imprint in der Firmware (alle aktiven Trails locken)?
- **CPU-Budget** des Governors (Hälfte des NT) ist geschätzt, am Gerät nachmessen.
