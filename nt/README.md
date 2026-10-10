# Perseids-core für den disting NT

Der Kern von Perseids als Algorithmus für den Expert Sleepers **disting NT**: fünf **Trails** aufnehmen, daraus mit **Spectra** (additive FFT-Resynthese) und **Swarm** (granular) eine Stereo-Wolke formen, dazwischen **Blend**. Was der NT selbst oder ein Modul im Pult schon kann, ist bewusst draußen (siehe unten).

Die Engines sind dieselben Quelldateien wie in der Daisy-Firmware (`../src/capture_engine.cpp`, `spectra_engine.cpp`, `swarm_engine.cpp`). Ein Fehler, der dort behoben wird, ist in beiden behoben. Dieser Ordner enthält nur die NT-Plattformschicht: Speicher, Routing, Parameter, Bedienung, Display.

Anleitung für Spieler: [`docs/perseids-nt-anleitung.html`](docs/perseids-nt-anleitung.html).

Gebaut gegen distingNT_API v14 (Submodul `distingNT_API`, Stand 6975a63). GPL-3.0 wie Perseids.

## Installation

- **NT:** `plugins/perseids.o` nach `programs/plug-ins/` auf die microSD-Karte, NT neu starten, Algorithmus **Perseids** hinzufügen. Beim Hinzufügen fragt der NT nach **Trail seconds** (1–30 s, Standard 10): so viel Aufnahmezeit bekommt jeder Trail. 16 Bit pro Sample, also 5 × 10 s ≈ 4,9 MB DRAM, 5 × 30 s ≈ 14,5 MB.
- **nt_emu (VCV Rack, Windows):** `emu/perseids.dll` nach `%LOCALAPPDATA%\Rack2\disting-nt-plugins\`. Braucht dieselbe für Windows gebaute nt_emu-Fassung wie Duett (Anleitung in `disting-nt-plugins/duett-nt/README.md`, Abschnitt „Am PC ausprobieren“). Die DLL bringt ihre eigene Brücke mit (`emu/nt_win_shim.cpp`, aus Duett übernommen).

## Bedienung

| Element | Funktion |
|---|---|
| Poti L | **Blend** Spectra ↔ Swarm; Poti L drücken schaltet auf **Dry/Wet** und zurück |
| Poti C | **Scan** (0 = Freeze); Poti C drücken schaltet auf **Reso mix** und zurück |
| Poti R | **Size** (Grain-Anzahl 4–24); Poti R drücken schaltet auf **Atmosphere** (Blur ← 0 → Radiation) und zurück |
| Encoder L drehen / klicken | Trail wählen / **Solo** für den gewählten Trail; über den letzten Trail hinaus: **Mod-Ansicht** |
| Encoder R drehen / klicken | **Level** des gewählten Trails (2 % pro Raste) / **Lock** |
| Taste 3 | **Rec**: sofort in den nächsten Trail aufnehmen |
| Taste 4 | **Hold** unendlich ein/aus (wirkt sofort, auch auf laufende Trails) |

Tasten 1/2 bleiben beim NT. Ein Encoder-Klick zählt nur ohne Drehen.

**Catch-up der Potis:** Nach dem Umschalten (oder wenn der Wert im Menü geändert wurde) übernimmt ein Poti erst, wenn es den gespeicherten Wert erreicht oder überfährt; bis dahin bleibt der Wert stehen und ist in der Fußzeile dunkel. So springt nichts, auch in nt_emu, wo das Soft Takeover des NT fehlt.

**In nt_emu (VCV Rack)** kommen bei Plug-ins mit eigener Oberfläche nur Poti- und Encoder-Drehungen und die Tasten 1–4 an, kein Druck auf Potis oder Encoder (so ist nt_emu gebaut, Stand ad2aa4b). Die Windows-DLL legt deshalb das Umschalten auf die freien Tasten: **Taste 1** = Poti L Blend ↔ Dry/Wet, **Taste 2** = Poti C und Poti R gemeinsam (Scan/Size ↔ Reso mix/Atmosphere). Meldet nt_emu doch einen Poti-Druck (beim Anfassen zum Drehen kommt das vor), ignoriert die nt_emu-Fassung ihn, damit Drehen das Ziel nicht zurückschaltet. Solo und Lock gehen in nt_emu nur über die Parameterseite *Mixer*. Tasten wirken in nt_emu beim Loslassen. Die Fußzeile der nt_emu-Fassung zeigt vor jedem Poti-Ziel die zuständige Taste (`1 BLEND`, `2 SCAN`, `2 SIZE`).

Display: Kopfzeile mit REC-Anzeige (bzw. dem nächsten Ziel-Trail), Eingangspegel L/R mit Threshold-Marke und Blend-Stellung. Darunter fünf Spalten: Trail-Nummer, Zustand (`REC`, `IN`, Sekunden bis zum Ausblenden, `INF`, `OUT`), Lebensbalken, Level-Balken, `L`/`S` und Level in %. Der gewählte Trail ist umrahmt, Trails über *Count* sind dunkel. Fußzeile: was die drei Potis gerade tun, `HOLD` bei unendlichem Hold, `CPU`, wenn der Swarm-Governor Grains spart, `MOD n`, wenn n Mod-Slots arbeiten.

**Lebensbalken wie am Modul:** Während der Aufnahme ziehen Funken durch den Balken, portiert aus `DrawTrailLifeBar` / `DrawRecSparkleFill` der Firmware (gleicher Hash, gleiche Tempi, gleiche Dichte pro Pixel, 200 ms weiches Erscheinen bzw. Verglühen). Stil über *Rec style* auf der Seite *Display*, wie Settings → REC: **PLR** (Standard, Funken links → rechts bis zur Aufnahme-Front), **PRS** (aus der Mitte), **CTR** (voller Balken aus der Mitte). Danach füllt Fade In den Balken von links, Hold steht voll, Fade Out leert ihn von links. Die Funken sind wie am Modul voll hell; der NT hat 16 Graustufen, deshalb zieht jeder Funke zusätzlich einen schwachen Pixel (Stufe 5) hinter sich her. Sieht man die Abstufung kaum, sieht es aus wie am Modul.

Bildschirm-Renderings: `render/perseids-screens.png` (Schriften angenähert, Positionen echt).

## Parameter

Alle Regler sind normale NT-Parameter, also per CV oder MIDI steuerbar.

| Seite | Parameter |
|---|---|
| Trails | Count 1–5 · Threshold % · Cont. Rec · Overwrite · Capture · Play · Clear trails (bestätigen; auch per Gate) |
| Time | Buffer 0,1–30 s (Obergrenze = *Trail seconds*) · Hold 0–30 s / INF (wirkt sofort auf laufende Trails) · Fade in · Fade out (0–5 s) |
| Engines | Blend % · Dry/Wet % · Level match Off/On (On) · Match speed Slow/Medium/Fast (Medium) · Pitch Spectra ±24 HT · Pitch Swarm ±24 HT · Output level −24…+24 dB |
| Spectra | Partials 4–32 · Waveshape (Saw ← 0 → Fold) · Umbra/Aurora · Ensemble |
| Swarm | Size · Spread · Scan · Scatter · Atmosphere · Direction (Fwd/Rev/Rnd) |
| Resonator | Reso mix (25 %) · Reso decay · Reso damping · Reso spread · Reso pitch ±12 HT · Reso quantize · Reso scale (Major/Minor/Pentatonic) · Reso tuning (Equal/Just) · Reso V/Oct in |
| Mixer | Level 1–5 · Lock 1–5 · Solo 1–5 |
| Mod 1–12 | Mod in (CV-Eingang, leer = internes LFO) · Mod dest · Mod amount ±100 % · Mod offset ±100 % · Mod LFO rate 0,01–20 Hz · Mod sync Free / ÷ / × |
| Display | Rec style PLR / PRS / CTR |
| Routing | In L · In R (optional) · Rec trig in (Flanke über 1 V) · Clock in (Flanke über 1 V) · Out L/R mit Add/Replace |

**Dry/Wet** wie der Multi der Firmware: überblendet gleichlaut zwischen dem sauberen Stereo-Eingang (In R leer → In L auf beiden Seiten) und der Wolke, das trockene Signal mit 0,85 wie in der Firmware. Ab Werk 100 % = nur Wolke, wie vor diesem Parameter. Unter 100 % die Ausgänge auf *Replace* oder auf eigene Busse legen, sonst kommt das Original doppelt. Auch Mod-Ziel.

Unterschiede zur Firmware, alle durch den NT bedingt:

- **Pitch in Halbtönen** statt ±100 % mit *Pitch Both*: Pitch Both ist fest auf 1 (Spanne ±2 Oktaven), dann sind ±24 Halbtöne exakt.
- **Pegel:** NT-Busse führen Volt. Eingang ±5 V = Codec-Vollaussteuerung, Ausgang × 5 V. *Output level* steht ab Werk auf +12 dB, weil der Wet-Bus von Perseids deutlich unter dem Eingang liegt (Trail-Level 50 %, Wolken-Panorama −3 dB). Danach dieselbe weiche Begrenzung wie im Multi der Firmware.
- **Blend** steht ab Werk auf 50 % (Firmware: 0 = nur Spectra), damit beide Engines gleich zu hören sind.
- **Trails in 16 Bit** (±2,0 Vollaussteuerung, eine Oktave Reserve über dem Codec-Bereich). Gegen die Float-Fassung gemessen liegt der Unterschied am Ausgang unter der 16-Bit-Quantisierung der Test-WAVs (−45 bis −55 dB).

## Modulation

Zwei Wege, die sich ergänzen:

- **Mod-Slots im Plug-in** (Seiten *Mod 1* bis *Mod 12*, am Ende der Seitenliste), aufgebaut wie die vier Mod-Slots des Moduls (ARCHITECTURE 4.3): *Mod in* wählt einen CV-Eingang oder Bus; bleibt er leer, arbeitet das interne LFO (Dreieck/Sinus wie am Modul, *LFO rate* 0,01–20 Hz) – das ist die Normalisierung der Mod-Buchsen. *Mod dest* ist jeder Klang- und Trail-Parameter (Trails, Time, Engines inkl. Dry/Wet, Spectra, Swarm, Resonator, Level/Lock/Solo je Trail) und **Amount, Offset und LFO rate jedes Slots** („Mod the mod“, eine Chunk-Länge ≈ 1 ms später) – 84 Ziele. *Mod sync* legt das interne LFO auf *Clock in* (Seite Routing, Flanke über 1 V, ab Werk In 3): **Free**, **/16 /8 /4 /3 /2** (ein Durchlauf über N Takte) oder **x1 x2 x3 x4 x8** (N Durchläufe pro Takt), am Takt neu gestartet (ARCHITECTURE 4.3 Divider, 4.10); **Rst /16 … Rst x8** = eigenes Tempo (*LFO rate*), aber im selben Raster neu gestartet (bei xN N-mal pro Takt aus der gemessenen Periode). *Mod mode*: **Around base** (Standard, der gespeicherte Wert ist die Mitte: Grundwert 50 %, Amount 10 % → 40–60 %) oder **Override** (der gespeicherte Wert zählt nicht, der Slot läuft von der Mitte des Bereichs, Amount 100 % = ganzer Bereich). Seite **Mod overview** vor Mod 1: *Reset all mods* (bestätigen; setzt jeden Slot ganz zurück: Eingang, Ziel, Amount, Offset, Rate, Sync, Mode), dann nur die aktiven Slots, je *Mod n dest* und *Mod n amount*; neue Slots und alles Weitere auf der Seite des Slots. Modulierte Parameter tragen in allen Menüs ein **„ ~“** am Namen. Eingang, Ziel, Mode und Sync eines Slots greifen erst, wenn sie 1,5 s stehen oder sobald die Perseids-Oberfläche wieder bedient wird – beim Durchblättern der Ziele wird nichts nebenbei moduliert; die Fußzeile zeigt dann `MOD*`. Amount, Offset und Rate wirken sofort. Ein Slot wird abgeschaltet, indem *Mod n dest* ganz nach links auf **None** gedreht wird (erster Eintrag). *Mod n amount* zeigt hinter dem eingestellten Wert, was der Slot gerade ausgibt („40 % > +23 %“), in der Übersicht wie auf der Slot-Seite; so sieht man die Bewegung. Bleibt die Clock 4 Perioden (höchstens 3 s) aus, läuft das LFO frei weiter; `CLK` in der Fußzeile zeigt eine laufende Clock. LFO rate bleibt bei höchstens 20 Hz: die Slots rechnen je 64 Samples, Modulation ist ein Steuersignal. Gerechnet wird wie am Modul: `Beitrag = Offset + Amount × Quelle`, `Ziel = Grundwert + Beitrag × voller Regelweg`, begrenzt. ±5 V = ±100 % Quelle. Mehrere Slots auf dasselbe Ziel addieren sich. Der gespeicherte Wert bleibt stehen, nur der wirksame Wert bewegt sich; Schalter und Zahlen (Lock, Count, Size, Direction …) werden gerundet. Ausgenommen sind Routing, *Clear trails* und *Rec style*.
- **CV-/MIDI-Mapping des NT:** Jeder Parameter, auch die Mod-Slots selbst, lässt sich zusätzlich im NT auf CV oder MIDI legen. Das verstellt den gespeicherten Wert direkt.

Typisch: Slot ohne Kabel, Amount 30 %, LFO 0,05 Hz auf *Scan* – die Wolke wandert von selbst. Oder ein Hüllkurvenfolger auf *Mod 1 in*, Ziel *Blend*: laute Stellen schieben zu Swarm.

## Level match

Swarm (überlappende Grains) klingt lauter als Spectra (ein ruhiger Satz Teiltöne), am stärksten bei kurzen, perkussiven Takes; in der Simulation 4–8 dB. Dazu wird Spectra lauter, wenn mehrere Trails dieselben Töne halten. Ohne Ausgleich überdeckt Swarm deshalb schon weit vor 50 % Blend.

*Level match* (Seite Engines, ab Werk On) gleicht beide Engines aus, wie ein sehr langsamer Kompressor: Beide werden aus derselben Trail-Summe gespeist, also wird jede gegen diese Summe gemessen. Ein Lautheitsfolger (50 ms Kurzzeit, steigt in 150 ms, fällt über 1,2 s, folgt also den lauten Stellen) auf der Trail-Summe und auf jedem Engine-Ausgang; daraus eine Verstärkung, die nachgeführt wird. *Match speed* stellt nur dieses Nachführen ein: **Slow** ≈ 4 s, **Medium** ≈ 1,5 s (ab Werk), **Fast** ≈ 0,4 s. Die Messung selbst bleibt bei jeder Stufe gleich, sonst würde sich mit der Geschwindigkeit auch die Balance verschieben. Die ersten 3 s mit Signal nach dem Laden oder nach *Clear trails* laufen immer auf Fast, damit der Ausgleich sofort einrastet statt nachzuziehen. Gelernt wird nur, solange die Engine läuft und Signal da ist (über −60 dB), in Pausen bleibt die Verstärkung stehen. Höchstens ±12 dB: es gleicht die Balance an, die Dynamik einzelner Anschläge bleibt. Swarm bekommt −2 dB auf sein Ziel, weil seine Grains bei gleichem Messwert spitzer klingen. Ergebnis in der Simulation: Glocke und Pad je unter 1 dB Unterschied zwischen Blend 0 und 100 %. **Trail-Level bleiben Akzente:** Level match vergleicht jede Engine mit der Trail-Summe, und die enthält die Trail-Level schon; dreht man einen Trail leiser, werden Summe und Engine gemeinsam leiser, das Verhältnis bleibt, der Ausgleich greift nicht ein (Test: Level 50 → 20 % ist rund 8 dB leiser, bei jeder Blend-Stellung). Kosten: ein paar Rechenschritte je 64 Samples. Nur im NT-Plug-in, die Firmware ist unverändert.

## Mod-Ansicht

Encoder L über den letzten Trail hinaus (Pfeil am rechten Rand) öffnet statt der Trail-Spalten eine Kachel je aktivem Slot (4 × 2, bei mehr als acht seitenweise): Slot, Ziel, Amount und eine kleine Oszilloskop-Spur des Ziels über gut 2 s, der Grundwert gepunktet (25 Werte/s aus `draw()`, im DRAM). Encoder L wählt die Kachel, Encoder R stellt ihren Amount (1 %/Raste); links von der ersten Kachel zurück zu den Trails. *Mod view* (Seite Display) schaltet auf **Numbers**: Grundwert > aktueller Wert in den Einheiten des Parameters, Offset, `ovr`. Solange die Ansicht offen ist, steht *Mod overview* als erste Parameterseite (`NT_updateParameterPages`), damit das Menü dort öffnet. Menü-Timeout und Startseite des Menüs bestimmt der Host; die API bietet dafür nichts.

## Resonator

Die spektrale Resonanzbank aus Block 7 der Firmware, unverändert derselbe Code: acht Bandpässe auf den Harmonischen von C2 (bzw. auf Skalenstufen mit *Reso quantize*), parallel auf den Swarm-Ausgang. *Reso decay* ist die Ausklingzeit (0,08–8 s), *Reso damping* macht obere Moden kürzer (links metallisch, rechts Holz/Körper), *Reso spread* fächert die Moden im Stereobild auf. *Reso mix* ab Werk 25 % wie im Modul. Wie in der Firmware liegt der Resonator nur auf Swarm: bei Blend 0 % (nur Spectra) ist er nicht zu hören.

NT-Zusätze: *Reso pitch* in Halbtönen (±12 = ±1 Oktave der Firmware) und **Reso V/Oct in**: 1 V pro Oktave auf den Grundton (0 V = C2), auf ganze Cent gerastet. Damit klingt die Resonanz in der Tonart einer Sequenz mit. *Reso scale* und *Reso tuning* sind am Modul Settings, hier eigene Parameter. Alle Reso-Parameter außer dem V/Oct-Eingang sind Mod-Ziele.

Im Firmware-Code kam dafür nur `SetRootOffset()` dazu (auf dem Daisy nie aufgerufen, Golden-Test bitgleich).

## Bewusst nicht im Kern

| Weggelassen | Ersatz |
|---|---|
| Reverb | NT-Reverbs, Milky Way |
| Filter | NT-Filter, Wasp, SEM |
| Pan Drift, Crossfade | NT-Mixer/LFO, Four Play (Trails stehen in der Mitte, Swarm bringt die Breite über *Spread*) |
| Multi-Makros, Settings | – (Dry/Wet selbst ist drin: Parameter *Dry/Wet*, Poti L drücken) |
| Mod-System: Auto-Mod Age/Pitch (4.10) | noch offen; die vier Mod-Slots selbst sind drin (siehe Modulation) |
| VU, Life-Bars, Settings-Seiten | stark vereinfacht im Display |

## Wie es auf dem NT läuft

- **Keine Main-Loop:** Auf dem Daisy läuft die FFT-Analyse in der Hauptschleife. Der NT ruft nur `step()` auf (in nt_emu mit 4 Frames, am NT mit größeren Blöcken). Deshalb zerlegt `SpectraEngine::AnalysisSlice()` die Analyse eines Hops in 10 Scheiben (Fenster, FFT, Magnituden, 4 × Umbra/Aurora, Glättung, Peak-Auswahl); `step()` führt alle 32 Samples eine aus. Genauso baut `SwarmEngine::WindowSlice()` die Grain-Hüllkurve nach einer Atmosphere-Änderung in 9 Scheiben neu. So kostet kein einzelner Block mehr als eine Scheibe.
- **Blockgröße:** Audio läuft in Stücken von höchstens 64 Frames; die Kanalerkennung der Aufnahme (RecordSource) rechnet unabhängig von der Blockgröße alle 256 Samples, der Swarm-Governor ebenso. Getestet: gleicher Pegel bei 4, 24 und 128 Frames pro `step()`.
- **CPU-Governor:** Am NT misst `step()` seine eigenen Zyklen (`NT_getCpuCycleCount`) und gibt Swarm als Last den Anteil an der Hälfte eines 600-MHz-Kerns. Ab 88 % davon (also ~44 % des NT) dünnt Swarm die Wolke aus, Untergrenze 6 Grains. In nt_emu ist er aus.
- **Speicher:** SRAM nur für die Instanz (2,3 KB); Engines (31 KB), FFT-Puffer (48 KB) und Trails im DRAM. Keine statischen Variablen, mehrere Instanzen teilen nichts.
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

`test/sim.cpp` läuft ohne Hardware und prüft 101 Punkte (NT-Fassung) bzw. 106 (nt_emu-Fassung, `test/sim_emu`), u. a.:

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

## Versionen

Die Version steht im Display neben „PERSEIDS“ und in der Algorithmus-Beschreibung, so ist sofort zu sehen, welche Fassung geladen ist.

| Version | Inhalt |
|---|---|
| 0.1 | Capture, Spectra, Swarm, Blend, Bedienung, Display |
| 0.2 | Aufnahme-Funken im Lebensbalken, Rec style |
| 0.3 | vier Mod-Slots |
| 0.4 | Dry/Wet auf Poti L |
| 0.5 | nt_emu: Taste 1/2 schalten die Poti-Ziele |
| 0.6 | Resonator (Spectral Resonator, Block 7) mit V/Oct |
| 0.7 | Versionsanzeige; nt_emu: Fußzeile zeigt die Taste je Poti-Ziel |
| 0.8 | nt_emu: Poti-Drücke werden ignoriert (nt_emu meldet beim Anfassen eines Potis einen Druck, Drehen schaltete das Ziel zurück); umgeschaltet wird nur mit Taste 1/2 |
| 0.9 | REC-Anzeige im Kopf nach rechts, verdeckt die Versionsnummer nicht mehr; Version heller |
| 0.10 | Hold wirkt sofort auf laufende Trails (INF stoppt den Countdown, kürzer als schon gespielt → Fade out, länger während des Fade out → Trail kommt zurück); Overwrite Off + INF: Threshold/Cont. Rec lassen INF-Trails stehen, nur Rec ersetzt den ältesten; Makefile mit Header-Abhängigkeiten; beides gilt auch in der Firmware (ARCHITECTURE §4.8) |
| 0.11 | Parameterseite *Resonator* direkt nach *Swarm*, vor *Mixer* (sie liegt auf dem Swarm-Ausgang) |
| 0.12 | Catch-up für Poti L/C/R: kein Sprung nach dem Umschalten oder nach einer Menü-Änderung; Wert dunkel, solange das Poti noch nicht übernommen hat |
| 0.13 | *Level match*: Spectra und Swarm gleich laut (Lautheitsfolger je Engine gegen die Trail-Summe, ±12 dB, ab Werk On) |
| 0.14 | *Match speed* Slow/Medium/Fast für Level match; Einrasten in den ersten 3 s immer schnell (kein Nachziehen am Anfang); Lautheitsfolger fällt jetzt über 1,2 s |
| 0.15 | *Clock in* mit *Mod sync* je Slot (Free, /16…/2, x1…x8, Reset am Takt, Clock-Verlust → frei); 12 Mod-Slots, Seiten am Ende; Mod the mod (Amount/Offset/Rate jedes Slots als Ziel) |
| 0.16 | *Mod mode* Around base / Override; Seite *Mod overview* mit *Reset all mods*; „ ~“ an modulierten Parametern im Menü; Mod sync *Rst …* (freies Tempo, Neustart im Takt-Raster); Slot-Eingang/Ziel/Mode/Sync greifen erst nach 1,5 s Stillstand oder beim Zurück auf die Oberfläche (`MOD*`); Clock in ab Werk In 3 |
| 0.17 | *Mod overview* schlanker: nur aktive Slots mit Namen, Ziel und Amount (Seite wird je Instanz neu gebaut, `NT_updateParameterPages`); Mod dest „Off“ heißt jetzt „None“ wie bei den Eingängen; *Mod n amount* zeigt die aktuelle Ausgabe des Slots live |
| 0.18 | Anhang „Wo steht was“ in der Anleitung (Seiten + nummerierte Mod-Ziele, erzeugt mit `./test/sim --list | python3 docs/appendix.py`); *Reset all mods* setzt jeden Slot ganz zurück (auch Eingang und Rate) und frischt die Menüs auf |
| 0.19 | Mod-Ansicht (Encoder L hinter dem letzten Trail): Kacheln mit Oszilloskop-Spur je aktivem Slot, Encoder R = Amount, *Mod view* Graphic/Numbers; Pfeil-Hinweis; Mod overview als erste Menüseite, solange die Ansicht offen ist |

## Stand

- Nativ getestet (über 110 Prüfungen je Fassung, NT- und nt_emu-Fassung), DLL unter Wine mit einem nt_emu-artigen Host geladen und gespielt.
- Läuft in nt_emu (VCV Rack, Windows), bestätigt am 10.10.2026. **Am NT selbst noch nicht getestet.**
- Firmware: Umbau der Engines bitgleich (Golden-Test vor/nach, 40 s Audio), Firmware-Link mit libDaisy geprüft.

## Offene Fragen

- **Speicher des NT:** Die Plug-in-API kennt vier Bereiche (SRAM für den Algorithmus, DRAM für große Puffer, DTC für zeitkritische Daten, ITC für Code), nennt aber keine Größen. Perseids braucht je Instanz ≈ 6 KB SRAM und ≈ 4,9 MB DRAM bei 10 s Trails. **DRAM-Grenze des NT** für Plug-ins ist nicht dokumentiert; 5 × 30 s (14,5 MB) erst am Gerät ausprobieren.
- **Taste 4:** Hold INF (jetzt, wirkt sofort auf alle laufenden Trails) oder „alles festhalten“ wie Imprint in der Firmware (alle aktiven Trails locken)?
- **Shift-Taste am NT (vorgemerkt):** am Gerät selbst die Poti-Ziele über eine Art Shift-Taste umschalten (gedrückt halten = zweite Belegung von Poti L/C/R) statt per Poti-Druck. Welche Taste, und ob halten oder umschalten, klärt sich am Gerät.
- **CPU-Budget** des Governors (Hälfte des NT) ist geschätzt, am Gerät nachmessen.
