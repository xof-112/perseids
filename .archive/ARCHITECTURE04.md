# Flaresum — Architektur & Entwicklungsplan (Master-Referenz)

> Dieses Dokument ist die **einzige Referenzquelle (Source of Truth)** für die KI-gestützte
> Entwicklung dieses Projekts. Implementierungen dürfen niemals gegen die hier definierten
> Prinzipien, Hardware-Grenzen oder Phasen-Abfolgen verstoßen.

**Konzept:** Inspiriert vom Plugin Coastline, aber eigenständig weiterentwickelt: ein globaler
Klangkörper (zwei Resynthese-Engines **Spectra** additiv & **Swarm** granular, Reverb, Filter,
Spectral Resonator) verarbeitet bis zu 5 simultane Audio-Stimmen ("**Flares**") im
Round-Robin-Pool. Die DSP-Verarbeitung ist bewusst **global und pre-fader**, pro Flare gibt es
nur einen schlanken Mixer-Zugriff (Level/Lock/Solo) — das hält die Bedienoberfläche schlank.

---

## 0. Cursor-IDE-Setup

Lege ein Verzeichnis `.cursor/rules/` im Projekt-Root an mit mindestens einer Datei
`architecture.mdc` (moderne, empfohlene Konvention 2026 — die alte flache `.cursorrules`-Datei
funktioniert zwar noch, gilt aber als veraltet). Frontmatter-Beispiel:

```
---
description: Flaresum Firmware Architecture
alwaysApply: true
---
Lies vor jedem Code-Vorschlag ARCHITECTURE.md im Projekt-Root. Beachte die Hardware-Grenzen
der Electrosmith Daisy Seed und die C++/DSP-Leitplanken in Abschnitt 2. Schreibe ausschließlich
Embedded-ARM-Cortex-M7-Code (libDaisy/DaisySP), keinen generischen Desktop-C++-Code.
```

Halte diese Regel unter 200 Wörtern (Token-Kosten bei `alwaysApply: true`), da sie bei jeder
Anfrage geladen wird.

---

## 1. Zielplattform

- **MCU/Board:** Electrosmith Daisy Seed (STM32H750, 480 MHz Cortex-M7, 64 MB SDRAM)
- **Framework:** libDaisy (Hardware-Abstraktion) + DaisySP (DSP-Bausteine)
- **Sprache:** C++ (PlatformIO oder Daisy-eigenes CMake-Toolchain-Setup)
- **Display:** SSD1309 OLED, 2,42″, 128×64 px (SPI bevorzugt für hohe Framerate)
- **Custom Carrier-PCB** für die Anbindung aller Potis/Encoder/Taster/Buchsen

---

## 2. Kritische C++/DSP-Leitplanken (zwingend, nicht verhandelbar)

1. **Strikte Trennung Audio-Callback / UI-Thread.** Der Audio-Callback läuft als
   hochpriorisierter Interrupt und darf niemals blockieren (kein `System::Delay()`, kein
   Display-Update, keine String-Formatierung, kein Logging). Kommunikation zwischen UI-Thread
   und Audio-Callback ausschließlich über `std::atomic` oder lock-freie Ringbuffer.
2. **SDRAM-Zwang für große Buffer.** Die 5 Flare-Ringbuffer müssen im externen 64 MB SDRAM
   liegen: `DSY_SDRAM_BSS float flare_buffer[5][BUFFER_SIZE];`. Im Audio-Callback niemals
   `new`, `malloc`, `std::vector` oder andere Heap-Allokationen.
3. **Zentrale ParameterRegistry ab Phase 1.** Jeder modulierbare Parameter meldet sich dort mit
   ID, Name, Min/Max/Default und einem Zeiger auf den aktuellen Wert an. Das Mod-System
   (Phase 10) und die Macro-Zuweisung (Phase 11) greifen ausschließlich über diese Registry zu,
   nie über isolierte Variablen.
4. **Pflicht-Input-Filtering.** Eingangssignal durchläuft vor dem Schreiben in den SDRAM-Buffer
   zwingend ein 20 Hz Hochpass- und 20 kHz Tiefpassfilter (schützt die Frequenzanalyse vor
   Netzbrummen/Digitalfiepen).
5. **Pre-Fader-Routing überall.** Abgriffe für Filter-Destination, Mod-Matrix und Reverb-Send
   erfolgen strikt vor den VCA-Mixer-Multiplikatoren. Eine auf 0 gemutete Flare bleibt weiterhin
   als Modulationsquelle/Effekt-Send aktiv.
6. **Non-blocking ADC-Mux-Polling im Main-Loop**, niemals im Audio-Callback. Eingelesene Werte
   per Exponential Moving Average (EMA) glätten, um Poti-Jitter zu unterdrücken.
   **Hinweis:** Die Daisy Seed hat nur 12 native ADC-Pins (bestätigt via Electrosmith-
   Dokumentation) — bei unserer Kanalzahl (19 Potis + Attenuverter + CV) sind externe
   Mux-ICs (z.B. CD74HC4067, 16-Kanal) auf der Carrier-PCB zwingend nötig, nicht optional.
   Sitzen NICHT auf der Daisy Seed selbst.
7. **Jack-Erkennung über Hardware-Normalling**, keine Spannungs-Heuristik. Buchsen mit
   Schaltkontakt: gesteckt = Kontakt öffnet = externer CV wird gelesen; ungesteckt = Kontakt
   geschlossen = interne Quelle aktiv (siehe Abschnitt 5.4, Auto-Mod).
8. **2%-Mitten-Deadzone für bipolare Parameter** (Waveshape, Umbra/Corona-Macro,
   Atmosphere-Macro, Multi-Macros). ADC-Werte zwischen 0.48 und 0.52 werden hart auf exakt 0.0
   (Mitte) gezwungen, um Center-Detent-Toleranzen und ADC-Jitter auszugleichen.

---

## 3. Bedienelemente — Übersicht

| Gruppe | Anzahl | Typ | Funktion |
|---|---|---|---|
| Block-Potis (1–10) | 10 | Poti | Je Zugriff auf 2–4 Unterparameter via Cycle-Taster (siehe 4.6) |
| Flare-Level | 5 | Poti mit Push | Turn = Level dieser Flare; kurz = Lock; lang = Solo |
| Mod-Slots | 4 | Poti | Amplitude; Destination/Divider via Cycle-Taster; Quelle intern oder CV-In |
| Multi | 1 | Encoder | Dry/Wet, Macro1, Macro2, Settings — via Cycle-Taster wie die Block-Potis |
| Cycle-Taster | 1 | Taster (neben Display) | Siehe 4.6 und 4.7 |
| Rec-Taster | 1 | Taster (parallel zur Trig-Buchse) | Manueller Aufnahme-Trigger |

**Gesamt: 19 Potis + 1 Encoder + 2 Taster.**

---

## 4. Vollständige Architektur & UI-Mechanik

### 4.1 Die 11 Funktionsblöcke (global, nicht pro Flare)

| # | Block | Cycle-Liste (erster Eintrag = Default) |
|---|---|---|
| 1 | **Flares** | Anzahl (1–5), Threshold, Cont. Rec, On/Off |
| 2 | **Time** | Buffer, Hold (bis 30s, darüber = infinite/Default), Fade In, Fade Out |
| 3 | **Engines** | Blend (Spectra↔Swarm), Pitch Spectra, Pitch Swarm |
| 4 | **Spectra Parameter** | Partials, Waveshape (Sine↔Saw↔Fold), Umbra/Aurora-Macro, Ensemble/Drift |
| 5 | **Swarm Parameter** | Size, Spread, Scan, Atmosphere-Macro (Blur↔Radiation) |
| 6 | **Reverb** | Mix, Decay, Damping, Character-Macro (Chorus↔Friction) |
| 7 | **Spectral Resonator** (wirkt auf Swarm-Ausgang) | Mix, Decay, Pitch, Quantized (On/Off, Skala aus Settings) |
| 8 | **Pan Drift** | Phase, Amplitude, Velocity |
| 9 | **Crossfade über 5 Flares** | Amplitude, Velocity |
| 10 | **Filter Mix** | Cutoff, Resonance, Feedback (Drive), Destination |
| 11 | **Multi** (Encoder) | Dry/Wet global, Macro1, Macro2, Settings |

**Block-8-Detail (Phase):** Steuert den Phasenversatz der pro Flare unabhängigen Pan-Drift-LFOs
zueinander (0% = alle Flares wandern synchron, 100% = maximal gegeneinander versetzt) —
verhindert, dass mehrere Flares im exakt gleichen Panorama-Takt wandern.

**Block-4-Detail (Umbra/Aurora-Macro, bipolar, 2% Deadzone):** 0% = neutrale 1:1-Resynthese.
Negative Werte (Umbra) schneiden fundamentale Frequenzen weg, bringen leise
Ambient-Rauschanteile nach vorn (transparent/luftig). Positive Werte (Aurora) legen ein
Formant-/Chroma-Filter über die Partials für harmonische Vokalbetonung (Note-Tracking).

**Block-4-Detail (Ensemble/Drift):** Slew-Limiting auf das FFT-Tracking plus leichte
Gegeneinander-Verstimmung gerader/ungerader Partials — erzeugt einen organischen Chorus
nativ in der Oszillatorbank, ohne externe Delay-Lines.

**Block-5-Detail (Atmosphere-Macro, bipolar, 2% Deadzone):** 0% = saubere Grains mit
Hann-Window. Negative Werte (Blur) glätten die Grain-Hüllkurven extrem für kantenlose
Ambient-Wolken. Positive Werte (Radiation) reduzieren die Sample-Rate (Lofi) und glätten
Änderungen über einen BBD-Style-Slew-Limiter (Tape-Warble).

**Block-6-Detail (Character-Macro, bipolar, 2% Deadzone):** 0% = unbehandelte Hallfahne.
Negative Werte (Chorus) legen eine langsame Modulation auf den Reverb-Tail (das eigentliche
"Lush"-Element aus dem Coastline-Original) — wirkt, anders als Ensemble/Drift in Spectra, auch
auf Swarm-Anteile und trockenes Signal, da es am gemeinsamen Reverb-Send sitzt. Positive Werte
(Friction) legen eine nicht-lineare Sättigung (Tanh-Soft-Clipping) direkt in den Feedback-Loop
des Reverb-Tanks — bei hohen Werten dichte Overdrive-Wall. Chorus und Friction sind bewusst
exklusiv (ein Regler, zwei Richtungen), nicht gleichzeitig kombinierbar.

**Block-10-Detail (Destination):** Wählt, auf welche Signalstufe der Filter wirkt — cycelbar
durch **Input → Spectra → Swarm → Reverb** (angelehnt an Coastlines Original-Verhalten, bei
dem der Filter standardmäßig nur auf den Resynthese-Engines läuft, aber auf Input/Reverb
erweiterbar ist).

**Block-11-Settings-Untermenü** (eigener Cycle-Einstieg über "Settings" in der Multi-Cycle-Liste):
1. CPU/SDRAM-Meter (On/Off, Anzeige im Display)
2. Instant Playback Mode (On/Off) — ON: Resynthese startet sofort, Analyse verfeinert sich live
   während der Buffer vollläuft (reaktiv wie ein Reverb/Resonator). OFF: wartet auf vollen
   Buffer, dann einmalige Analyse (verhält sich wie ein Delay/Looper)
3. Scale (C-Major, Minor, Pentatonik — erweiterbar)
4. Intonation (Equal Temperament ↔ Just Intonation, für Block 7 Quantized)
5. Auto-Mod/Normalling (siehe 5.4)
6. **Audio Routing** (Stereo ↔ Sidechain) — siehe Detailbeschreibung unten

**Block-11-Detail (Audio Routing):**
- **Stereo (Default):** In L und In R arbeiten als normales Stereo-Paar (bzw. Mono-Split).
  Beide Eingänge werden gemischt in die 5 Flare-Ringbuffer aufgenommen und ganz normal
  weiterverarbeitet.
- **Sidechain Mode:** Die Buchsen werden logisch getrennt:
  - **In L (Main Audio):** Live-Instrument, wird NICHT aufgenommen, läuft direkt/trocken
    zum Ausgangs-Mix (VCA/Reverb-Send wie gehabt, aber ohne in die Capture-Buffer zu
    gelangen)
  - **In R (Sidechain Capture):** Läuft exklusiv in die Threshold-Erkennung und die 5
    SDRAM-Ringbuffer — nur dieses Signal speist Spectra und Swarm
  - **Out L/R:** Mischen das trockene Main-Signal (In L) mit der aus In R generierten
    Spectra/Swarm/Reverb-Klangwolke — ein Live-Instrument kann so über eine völlig
    unabhängige Audioquelle "kommentiert" werden, ohne dass sich beide Signale
    gegenseitig in die Analyse einmischen

### 4.2 Flare-Level (×5)

- **Turn:** Lautstärke dieser Flare
- **Kurz drücken:** Lock (schützt vor Round-Robin-Ersetzung und Hold-Time-Ausklingen)
- **Lang drücken:** Solo

### 4.3 Mod-Slots (×4)

Cycle-Liste **Amplitude → Destination → Divider**. Destination referenziert einen beliebigen
Parameter aus der ParameterRegistry (Block 1–11 oder Flare-Level). Divider = Clock-Unterteilung
für den Fall interner Quelle.

### 4.4 I/O-Buchsen

| Eingänge | Ausgänge |
|---|---|
| Mono In L | Mono Out L |
| Mono In R | Mono Out R |
| Clock | |
| Trig (neuer Flare, parallel zum Rec-Taster) | |
| Mod CV 1–4 (mit Schaltkontakt/Normalling) | |

**8 Eingänge + 2 Ausgänge = 10 Buchsen.**

### 4.5 Rec-Taster

Momentan-Taster, elektrisch parallel zur Trig-Buchse — identisches Signal, löst unabhängig vom
Threshold eine neue Aufnahme aus (gleiche Round-Robin-Logik wie automatischer Trigger).

### 4.6 Universeller Cycle-Mechanismus (10 Block-Potis + 4 Mod-Potis + 1 Multi-Encoder)

| Zustand | Aktion |
|---|---|
| Regler drehen, Cycle-Taster **nicht** gehalten | Ändert den Wert des zuletzt gebundenen Parameters (Start: erster Listeneintrag = Default) |
| Cycle-Taster **gehalten** + Regler drehen | Blättert durch die Parameterliste — Display unten: Name/Position, oben: aktueller Wert. Keine Wertänderung |
| Cycle-Taster **loslassen** | Regler ab jetzt an zuletzt angezeigten Parameter gebunden |

### 4.7 Cycle-Taster — Zusatzfunktionen (allein gedrückt, ohne Reglerbewegung)

| Geste | Aktion |
|---|---|
| Kurz, allein | **Play/Pause** (global, alle Flares) |
| Lang, allein | **Reset** (alle Flares sofort löschen) |
| Gehalten + Regler drehen | Cycle-Modus (4.6) |

### 4.8 Capture-Modell (Flare-Pool)

- **Anzahl** (Block 1): wie viele der 5 Slots aktiv genutzt werden (1–5)
- **Threshold**: triggert automatische Aufnahme in die älteste, nicht gelockte aktive Flare
  (Round-Robin)
- **Cont. Rec** (Continuous Recording): re-triggert dauerhaft neue Aufnahmen, solange das
  Eingangssignal über dem Threshold liegt, statt auf Unterschreiten zu warten (Coastline-Original-
  Feature)
- **On/Off**: globaler Bypass/Enable des Capture-Systems
- **Manueller Trigger** (Rec-Taster/Trig): gleiche Round-Robin-Logik
- **Hold** (Block 2): Countdown bis max. 30s; jeder höhere Wert springt logisch auf infinite
  (auch Boot-Default)
- **Instant Playback Mode** (Settings, 4.1): siehe Beschreibung dort

### 4.9 Display-Konzept

SSD1309, 128×64 px. Enthält: Cycle-Anzeige (Name unten, Wert oben), Home-Dashboard mit
Flare-Status (Level/Lock/Solo), Input-Threshold-VU-Meter mit Schwellen-Markierung,
CPU/SDRAM-Meter (oben rechts, per Settings ausblendbar), "Wandering Beams" — rotierende,
sich verkürzende Strahlen um das Flare-Symbol als Visualisierung der verbleibenden Hold-Time
(kürzer/langsamer = näher am Ausklingen).

---

## 5. Entwicklungsprinzipien

1. **UI-Mechanik zuerst, komplett auf Dummy-Werten**, bevor echte DSP dazukommt.
2. **Jede Phase = ein Cursor-Prompt = ein Git-Commit.**
3. **Nach jeder Phase ein konkretes, testbares Kriterium** (siehe Tabelle Abschnitt 6).
4. **Der Cycle-Mechanismus wird einmal generisch implementiert** und für alle 15 Zeilen
   wiederverwendet.
5. **ParameterRegistry wird ab Phase 1 mitgezogen**, nicht nachträglich draufgesetzt.
6. **ARCHITECTURE.md ist Quelle der Wahrheit.** Änderungen hier zuerst, dann erst der nächste
   Prompt.

---

## 6. Phasen-Roadmap

| Phase | Fokus | Testkriterium |
|---|---|---|
| 0 | Setup & `.cursor/rules/` | Projekt compiliert, LED blinkt, Rule aktiv |
| 1 | ParameterRegistry, Cycle-Mechanismus, ADC-Mux-Polling, Display-Grundgerüst | Cycle-Taster+Dummy-Zeilen funktionieren, EMA-geglättete Mux-Werte sichtbar |
| 2 | Flare-Level-Pushes, Rec-Taster/Trig, Menü-Taster-Gesten | Lock/Solo/Level auf Dummy-Werten, Debouncing sauber |
| 3 | Capture-Engine (SDRAM-Ringbuffer, Round-Robin, Cont. Rec, Time-Block) | Reale Aufnahme/Wiedergabe, Threshold-VU-Meter, Hold-Countdown |
| 4 | Spectra-Engine (additiv) | Partials/Waveshape/Umbra-Aurora/Ensemble-Drift hörbar |
| 5 | Swarm-Engine (granular) | Size/Spread/Scan/Atmosphere hörbar |
| 6 | Engine-Blend (Block 3) | Stufenloses Crossfading Spectra↔Swarm |
| 7 | Spectral Resonator | Mix/Decay/Pitch/Quantized aktiv, Intonation aus Settings wirksam |
| 8 | Reverb & Filter Mix | ReverbSc mit Character-Macro, SVF-Filter mit Feedback-Drive, Destination-Routing |
| 9 | Pan Drift & Crossfade & Wandering Beams | Phasenversetzte Pan-LFOs, Crossfade-Slew, Display-Visualisierung |
| 10 | Mod-System | 4 Slots, Jack-Normalling, Registry-Destination, Divider/Clock |
| 11 | Multi & Settings & Kalibrierung | Dry/Wet/Macros, Settings-Untermenü komplett, CV-Kalibrierung |

---

## 7. Phasen im Detail (Cursor-Prompts)

### Phase 0 — Setup & Cursor-Rules

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root.

Richte ein libDaisy/DaisySP-Projekt für Electrosmith Daisy Seed ein (PlatformIO). Lege
.cursor/rules/architecture.mdc gemäß Abschnitt 0 an. Erstelle main.cpp mit Blink-Test
zur Verifikation des Build-/Flash-Workflows. Kein Audio-Code in dieser Phase.
```

### Phase 1 — ParameterRegistry, Cycle-Mechanismus, ADC-Mux

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkte 3, 6), 4.6, 4.7.

Implementiere:
- ParameterRegistry-Klasse (ID, Name, Min/Max/Default, Wert-Zeiger, Registrierung)
- Generische CycleRow-Klasse (Parameterliste, gebundener Index, Blättern() vs.
  WertÄndern()) mit 3 Dummy-Instanzen
- Non-blocking Polling zweier CD74HC4067-artiger Mux-Ketten (oder Direktanschluss,
  falls noch nicht final) im Main-Loop, NICHT im Audio-Callback, mit EMA-Glättung
- Cycle-Taster-Gesten gemäß 4.7 (gehalten+drehen / allein kurz / allein lang), nutze
  dieselbe Gesten-Klasse später auch für Flare-Level und Rec-Taster
- Display-Update: Cycle-Anzeige (unten Name, oben Wert), simples Dummy-Dashboard

Baue alles so, dass spätere Phasen nur noch echte CycleRow-Instanzen mit echten,
in der ParameterRegistry angemeldeten Parametern anlegen müssen.
```

### Phase 2 — Flare-Level, Rec-Taster, Menü-Gesten

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 4.2 und 4.5.

Implementiere 5 Flare-Level-Push-Potis (Turn=Dummy-Level, kurz=Dummy-Lock,
lang=Dummy-Solo) mit der Gesten-Klasse aus Phase 1. Rec-Taster und Trig-Eingang
lösen denselben Dummy-Callback aus. Zeige Lock/Solo/Level aller 5 Flares im
Dashboard.
```

### Phase 3 — Capture-Engine

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkt 2, 4), 4.1 (Block 1+2),
4.8.

Implementiere die Capture-Engine:
- 5 Ringbuffer via `DSY_SDRAM_BSS float flare_buffer[5][BUFFER_SIZE];`
- Echte CycleRow für Block 1 (Anzahl, Threshold, Cont. Rec, On/Off) und Block 2
  (Buffer, Hold, Fade In, Fade Out), ersetzt die Dummy-Zeilen
- Round-Robin-Aufnahme bei Threshold-Überschreitung ODER Rec-Taster/Trig, in die
  älteste nicht gelockte aktive Flare
- Continuous-Recording-Modus (re-triggert bei Pegelüberschreitung ohne auf
  Unterschreiten zu warten)
- Hold-Countdown bis 30s, danach Fade-Out; Werte >30s = infinite (auch Boot-Default)
- Lock schützt vor Ersetzung und Ausklingen
- 20Hz-Hochpass/20kHz-Tiefpass-Filterung des Eingangs vor dem Schreiben in den
  SDRAM-Buffer (Abschnitt 2, Punkt 4)
- Beachte den Audio-Routing-Modus (Block 11 Settings, siehe 4.1): im Default (Stereo)
  speist der gemischte In-L/In-R-Signalpfad die Buffer; im späteren Sidechain-Mode
  (Phase 11) wird stattdessen exklusiv In R aufgenommen, In L bleibt trocken und
  umgeht die Buffer komplett — baue die Signalquelle für die Buffer-Aufnahme daher
  hinter einer austauschbaren Abstraktion, nicht fest verdrahtet auf "In L + In R"
- Play/Pause aus Phase 1 steuert jetzt echte Wiedergabe (Crossfade über Fade-Zeiten)
```

### Phase 4 — Spectra-Engine

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 3+4).

Implementiere die additive Spectra-Engine: FFT-Analyse via CMSIS-DSP (DaisySP selbst hat
kein eigenes FFT-Modul; libDaisy bindet aber die native ARM-CMSIS-DSP-Bibliothek für den
Cortex-M7 ein). Konkret: `arm_rfft_fast_f32`/`arm_rfft_fast_init_f32` für die Real-FFT
(schneller/speicherärmer als komplexe FFT, da unser Audiosignal reell ist), danach
`arm_cmplx_mag_f32` zur Berechnung der Partial-Magnituden. Prüfe vor der Implementierung,
ob die im Projekt vorliegende CMSIS-DSP-Version die neuere API (separater temporärer
Buffer für F32-RFFT/CFFT) oder die ältere in-place-API verwendet, und passe den Code
entsprechend an. Audio vor der FFT mit einem Hann- oder Blackman-Harris-Fenster
multiplizieren (`arm_mult_f32`) gegen Spectral Leakage. FFT NICHT im hochpriorisierten
AudioCallback ausführen, sondern blockweise (z.B. 256-512 Samples Hop-Size) im Main-Loop
oder einem niedriger priorisierten Block-Zyklus. Resynthese über eine Bank aus 32-64
DaisySP-Oszillatoren, deren Frequenzen/Amplituden direkt aus den Partial-Magnituden
gesetzt werden (keine IFFT nötig). Echte CycleRow für Block 4: Partials, Waveshape
(Sine↔Saw↔Fold, 2% Deadzone bei bipolaren Anteilen falls zutreffend), Umbra/Aurora-Macro
(bipolar, 2% Deadzone, siehe 4.1-Detailbeschreibung), Ensemble/Drift (Slew-Limiting +
Partial-Detuning für nativen Chorus). Pitch Spectra vorerst direkt gesetzt (volle Block-3-
Integration folgt Phase 6). Registriere alles in der ParameterRegistry.
```

### Phase 5 — Swarm-Engine

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 3+5).

Implementiere die granulare Swarm-Engine auf den Flare-Ringbuffern. Echte CycleRow
für Block 5: Size (Grain-Länge), Spread (Stereo-Streuung einzelner Grains), Scan
(Scrubbing-Geschwindigkeit, 0=Freeze), Atmosphere-Macro (bipolar: Blur↔Radiation,
2% Deadzone, siehe 4.1-Detailbeschreibung inkl. BBD-Style-Slew bei Radiation).
Pitch Swarm vorerst direkt gesetzt. A/B-Wechsel mit Spectra reicht für diese Phase
(echtes Blending folgt Phase 6).
```

### Phase 6 — Engine-Blend

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 3).

Ersetze den A/B-Wechsel durch echtes Pre-Fader-Crossfading zwischen Spectra- und
Swarm-Ausgabe. Vollständige CycleRow für Block 3: Blend, Pitch Spectra, Pitch
Swarm — konsolidiere die in Phase 4/5 direkt gesetzten Pitch-Werte hier hinein.
```

### Phase 7 — Spectral Resonator

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 7 und Settings-Untermenü).

Implementiere den Spectral Resonator auf dem Swarm-Ausgang: Bank stimmbarer
Bandpass-Resonatoren. CycleRow Block 7: Mix, Decay, Pitch, Quantized (On/Off,
zwingt Tonhöhen auf die in Settings gewählte Scale). Lies den Intonation-Schalter
(Equal Temperament / Just Intonation) aus dem Settings-Untermenü (Block 11) und
wende ihn auf die Resonatorstimmung an.
```

### Phase 8 — Reverb & Filter Mix

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkt 5), 4.1 (Block 6+10).

Implementiere:
- Reverb: DaisySP ReverbSc als globalen Pre-Fader-Send. CycleRow Block 6: Mix,
  Decay, Damping, Character-Macro (bipolar, 2% Deadzone: negativ = langsame
  Chorus-Modulation auf den Reverb-Tail; positiv = Tanh-Soft-Clipping-Sättigung
  direkt im Feedback-Loop des Tanks — siehe 4.1-Detailbeschreibung)
- Filter: Zustandsvariables Multimode-Filter (SVF: LP/BP/HP). CycleRow Block 10:
  Cutoff, Resonance, Feedback (Audio-Rate-Rückführung auf Cutoff-Modulation für
  Drive), Destination (cycelt Input→Spectra→Swarm→Reverb, wählt welche Stufe
  gefiltert wird, Pre-Fader-Abgriff)
```

### Phase 9 — Pan Drift, Crossfade & Wandering Beams

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 8+9) und 4.9.

Implementiere:
- Pan-Drift: pro Flare unabhängiger LFO (Dreieck/Sine-Mischung + leichter Jitter),
  Constant-Power-Panning. CycleRow Block 8: Phase (Phasenversatz zwischen den
  LFOs der Flares, 0%=synchron, 100%=maximal versetzt), Amplitude (Auslenkung),
  Velocity (Geschwindigkeit)
- Crossfade: CycleRow Block 9: Amplitude, Velocity, mit Slew-Rate-Limiting
  (BBD-artig) beim Round-Robin-Ersetzen einer Flare
- Display: "Wandering Beams" — rotierende, sich mit ablaufender Hold-Time
  verkürzende/verlangsamende Strahlen um jedes Flare-Symbol
```

### Phase 10 — Mod-System

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkt 7), 4.3, 4.4.

Implementiere die 4 Mod-Slots: Jack-Erkennung über Schaltkontakt (nicht
Spannungs-Heuristik) — gestecktes Kabel = externer CV wird gelesen, sonst interne
Quelle (einfaches Dreieck/Sinus, Rate = Clock-Periode × Divider). CycleRow pro
Slot: Amplitude, Destination (aus ParameterRegistry, alle Blöcke 1–11 sowie
Flare-Level), Divider.
```

### Phase 11 — Multi, Settings & Kalibrierung

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 11, Settings-Untermenü).

Implementiere:
- CycleRow für Multi-Encoder: Dry/Wet global, Macro1, Macro2 (Ziele vorerst fest im
  Code, klar kommentiert — siehe Abschnitt 7 Offene Punkte), Settings
- Settings-Untermenü: CPU/SDRAM-Meter-Toggle, Instant-Playback-Mode-Toggle,
  Scale-Auswahl (C-Major/Minor/Pentatonik), Intonation-Toggle (Equal/Just),
  Auto-Mod/Normalling-Auswahl (OFF/Age/Pitch/Both — steuert, was ungesteckte
  Mod-CV-Buchsen intern liefern, sobald Jack-Erkennung "nicht gesteckt" meldet),
  Audio-Routing-Toggle (Stereo/Sidechain, siehe 4.1-Detailbeschreibung): im
  Sidechain-Mode wird die in Phase 3 vorbereitete Buffer-Signalquelle exklusiv auf
  In R umgeschaltet, In L wird direkt (trocken, unter Umgehung der Flare-Buffer) mit
  dem Spectra/Swarm/Reverb-Ausgang auf Out L/R gemischt
- Kalibrierungsroutine (Min/Max-Lernmodus) für Threshold und alle CV-Eingänge
- Review: alle Display-Texte auf 128×64px-Lesbarkeit prüfen
```

---

## 8. Offene Punkte

- **~~Umbra/Corona-Namenskollision~~ Erledigt:** Macro heißt jetzt Umbra/Aurora
- **~~"Spread" doppelt vergeben~~ Erledigt:** Block 8 heißt jetzt Phase statt Spread
- **Macro1/Macro2-Zielzuweisung:** aktuell fest im Code (Phase 11), noch keine Front-Panel-UI
  dafür entschieden
- **Multi-Encoder Push-Funktion:** ungenutzt/nicht spezifiziert
- Preset-Speicherung: architektonisch durch die ParameterRegistry vorbereitet (Flash-Persistenz),
  aber erst nach Stabilität der V1.0-Firmware geplant
