# Perseids — Architektur & Entwicklungsplan (Master-Referenz)

> Dieses Dokument ist die **einzige Referenzquelle (Source of Truth)** für die KI-gestützte
> Entwicklung dieses Projekts. Implementierungen dürfen niemals gegen die hier definierten
> Prinzipien, Hardware-Grenzen oder Phasen-Abfolgen verstoßen.

**Konzept:** Inspiriert vom Plugin Coastline, aber eigenständig weiterentwickelt: ein globaler
Klangkörper (zwei Resynthese-Engines **Spectra** additiv & **Swarm** granular, Reverb, Filter,
Spectral Resonator) verarbeitet bis zu 5 simultane Audio-Stimmen ("**Trails**") im
Round-Robin-Pool. Die DSP-Verarbeitung ist bewusst **global und pre-fader**, pro Trail gibt es
nur einen schlanken Mixer-Zugriff (Level/Lock/Solo) — das hält die Bedienoberfläche schlank.

---

## 0. Cursor-IDE-Setup

Lege ein Verzeichnis `.cursor/rules/` im Projekt-Root an mit mindestens einer Datei
`architecture.mdc` (moderne, empfohlene Konvention 2026 — die alte flache `.cursorrules`-Datei
funktioniert zwar noch, gilt aber als veraltet). Frontmatter-Beispiel:

```
---
description: Perseids Firmware Architecture
alwaysApply: true
---
Lies vor jedem Code-Vorschlag ARCHITECTURE.md im Projekt-Root. Beachte die Hardware-Grenzen
der Electrosmith Daisy Seed und die C++/DSP-Leitplanken in Abschnitt 2. Schreibe ausschließlich
Embedded-ARM-Cortex-M7-Code (libDaisy/DaisySP), keinen generischen Desktop-C++-Code.
```

**Projektstruktur (Standard-PlatformIO-Konvention):**

```
Projekt-Root/
├── platformio.ini          ← Board-Konfiguration (electrosmith_daisy), Build-Flags
├── src/
│   └── main.cpp            ← Einstiegspunkt, hier landet der Firmware-Code
├── include/                 ← optionale eigene Header
├── lib/                      ← optionale eigene Libraries
├── .cursor/
│   └── rules/
│       └── architecture.mdc
└── ARCHITECTURE.md
```

`main.cpp` liegt in `src/`, direkt neben `platformio.ini` im Projekt-Root — das ist keine
Daisy-Seed- oder libDaisy-Eigenheit, sondern PlatformIO erzwingt diese Struktur generell, da
der Build-Prozess den Einstiegspunkt standardmäßig dort erwartet. Das Grundgerüst (leere
`src/`, `include/`, `lib/`, `test/` plus `platformio.ini`) legt `pio project init --board
electrosmith_daisy` automatisch an — der eigentliche Firmware-Code in `main.cpp` sowie die
libDaisy/DaisySP-Anbindung (`lib_deps` in `platformio.ini`) müssen danach noch geschrieben
werden, das übernimmt Cursor gemäß Phase-0-Prompt.

**Realitäts-Check PlatformIO + libDaisy:** Die Board-ID `electrosmith_daisy` ist offiziell in
PlatformIO eingetragen, libDaisy/DaisySP selbst aber **nicht** offiziell im PlatformIO-Registry
gepflegt — sie werden über `lib_deps` als direkte GitHub-Verweise eingebunden und landen beim
Build automatisch in einem versteckten `.pio/libdeps/`-Ordner (nicht im sichtbaren `lib/`).
Im Electro-Smith-Forum gibt es wiederkehrende Berichte über Build-Probleme bei diesem Weg —
es ist also möglich, dass der allererste Build in Phase 0 nicht auf Anhieb sauber durchläuft
und an den `lib_deps`-Einträgen oder Compiler-Flags nachjustiert werden muss. Das wäre dann
kein Bedienfehler, sondern ein bekannter Reibungspunkt dieser Kombination. Läuft PlatformIO
gar nicht rund, ist der offizielle Fallback der Makefile-basierte Daisy-Toolchain
(libDaisy/DaisySP als Git-Submodule, `make` statt `pio`) — erprobter, aber ohne
PlatformIO-Komfort.

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
2. **SDRAM-Zwang für große Buffer.** Die 5 Trail-Ringbuffer müssen im externen 64 MB SDRAM
   liegen: `DSY_SDRAM_BSS float trail_buffer[5][BUFFER_SIZE];`. Im Audio-Callback niemals
   `new`, `malloc`, `std::vector` oder andere Heap-Allokationen.
3. **Zentrale ParameterRegistry ab Phase 1.** Jeder modulierbare Parameter meldet sich dort mit
   ID, Name, Min/Max/Default und einem Zeiger auf den aktuellen Wert an. Das Mod-System
   (Phase 10) und die Macro-Zuweisung (Phase 11) greifen ausschließlich über diese Registry zu,
   nie über isolierte Variablen.
4. **Pflicht-Input-Filtering.** Eingangssignal durchläuft vor dem Schreiben in den SDRAM-Buffer
   zwingend ein 20 Hz Hochpass- und 20 kHz Tiefpassfilter (schützt die Frequenzanalyse vor
   Netzbrummen/Digitalfiepen).
5. **Pre-Fader-Routing überall.** Abgriffe für Filter-Destination, Mod-Matrix und Reverb-Send
   erfolgen strikt vor den VCA-Mixer-Multiplikatoren. Ein auf 0 gemuteter Trail bleibt weiterhin
   als Modulationsquelle/Effekt-Send aktiv.
6. **Non-blocking ADC-Mux-Polling im Main-Loop**, niemals im Audio-Callback. Eingelesene Werte
   per Exponential Moving Average (EMA) glätten, um Poti-Jitter zu unterdrücken.
   **Hinweis:** Die Daisy Seed hat nur 12 native ADC-Pins (bestätigt via Electrosmith-
   Dokumentation) — bei unserer Kanalzahl (19 Potis — die 4 Mod-Amplitude-Potis darunter fungieren als
   bipolare Attenuverter, siehe 4.3 — plus CV-Eingänge) sind externe
   Mux-ICs (z.B. CD74HC4067, 16-Kanal) auf der Carrier-PCB zwingend nötig, nicht optional.
   Sitzen NICHT auf der Daisy Seed selbst.
7. **Jack-Erkennung über Hardware-Normalling**, keine Spannungs-Heuristik. Buchsen mit
   Schaltkontakt: gesteckt = Kontakt öffnet = externer CV wird gelesen; ungesteckt = Kontakt
   geschlossen = interne Quelle aktiv (siehe Abschnitt 4.10, Auto-Mod).
8. **4%-Mitten-Deadzone für ALLE bipolaren Parameter (zwingend, keine Ausnahmen):**
   Waveshape, Umbra/Aurora-Macro, Atmosphere-Macro, Character-Macro, Multi-Macros, die
   4 Mod-Amplituden (Attenuverter, siehe 4.3) und Crossfade-Velocity (Block 9). ADC-Werte
   zwischen 0.48 und 0.52 (= 4 % des Regelwegs, ±2 % um die Mitte) werden hart auf exakt 0.0
   (Mitte) gezwungen, um Center-Detent-Toleranzen und ADC-Jitter auszugleichen.

---

## 3. Bedienelemente — Übersicht

| Gruppe | Anzahl | Typ | Funktion |
|---|---|---|---|
| Block-Potis (1–10) | 10 | Poti | Je Zugriff auf 2–4 Unterparameter via Cycle-Taster (siehe 4.6) |
| Trail-Level | 5 | Poti mit Push | Turn = Level dieses Trails; kurz = Lock; lang = Solo |
| Mod-Slots | 4 | Poti | Amplitude (bipolarer Attenuverter); Destination/Divider via Cycle-Taster; Quelle intern oder CV-In |
| Multi | 1 | Encoder | Dry/Wet, Macro1, Macro2, Settings — via Cycle-Taster wie die Block-Potis |
| Cycle-Taster | 1 | Taster (neben Display) | Siehe 4.6 und 4.7 |
| Rec-Taster | 1 | Taster (parallel zur Trig-Buchse) | Manueller Aufnahme-Trigger |

**Gesamt: 19 Potis + 1 Encoder + 2 Taster.**

---

## 4. Vollständige Architektur & UI-Mechanik

### 4.1 Die 11 Funktionsblöcke (global, nicht pro Trail)

| # | Block | Cycle-Liste (erster Eintrag = Default) |
|---|---|---|
| 1 | **Trails** | Anzahl (1–5), Threshold, Cont. Rec, On/Off |
| 2 | **Time** | Buffer (= Ringbuffer-Länge/max. Aufnahmedauer pro Trail), Hold (bis 30s, darüber = infinite/Default), Fade In, Fade Out |
| 3 | **Engines** | Blend (Spectra↔Swarm), Pitch Spectra, Pitch Swarm |
| 4 | **Spectra Parameter** | Partials, Waveshape (Sine↔Saw↔Fold), Umbra/Aurora-Macro, Ensemble/Drift |
| 5 | **Swarm Parameter** | Size, Spread, Scan, Atmosphere-Macro (Blur↔Radiation) |
| 6 | **Reverb** | Mix, Decay, Damping, Character-Macro (Chorus↔Friction) |
| 7 | **Spectral Resonator** (wirkt auf Swarm-Ausgang) | Mix, Decay, Pitch, Quantized (On/Off, Skala aus Settings) |
| 8 | **Pan Drift** | Phase, Amplitude, Velocity |
| 9 | **Crossfade über 5 Trails** | Amplitude, Velocity |
| 10 | **Filter Mix** | Cutoff, Resonance, Feedback (Drive), Destination |
| 11 | **Multi** (Encoder) | Dry/Wet global, Macro1, Macro2, Settings |

**Block-8-Detail (Phase):** Steuert den Phasenversatz der pro Trail unabhängigen Pan-Drift-LFOs
zueinander (0% = alle Trails wandern synchron, 100% = maximal gegeneinander versetzt) —
verhindert, dass mehrere Trails im exakt gleichen Panorama-Takt wandern.

**Block-9-Detail (Crossfade über 5 Trails):** Eine Amplituden-Welle (Lautstärke-Fokus) wandert
kontinuierlich vorwärts/rückwärts durch die aktiven Trails und blendet benachbarte Trails
stufenlos gegeneinander über — ergibt einen sich mehr oder weniger langsam verschiebenden
Fokus bzw. eine zusätzliche innere Bewegung im Klangbild. **Amplitude** = Tiefe der Welle
(0 % = keine Wirkung, alle Trails gleichberechtigt; 100 % = nur der fokussierte Trail voll
hörbar). Nicht-fokussierte Trails werden also im Normalfall nur ABGESCHWÄCHT, nicht
entfernt — erst nahe 100 % Amplitude nähern sie sich der Stille. **Velocity** =
Wander-Geschwindigkeit, bipolar (Vorzeichen = Richtung vorwärts/rückwärts, 4 % Deadzone,
Mitte = Fokus eingefroren). Die Welle wirkt multiplikativ
auf derselben VCA-Stufe wie Trail-Level, also NACH den Pre-Fader-Abgriffen — Regel 2.5 bleibt
unberührt, Mod-Matrix und Sends sehen die Welle nicht. Die Welle läuft nur über die laut
Block 1 aktiven Trails. Solo übersteuert die Welle (Solo-Trail bleibt voll hörbar);
Lock schützt nur vor Round-Robin-Ersetzung, nicht vor der Welle.

**Block-4-Detail (Umbra/Aurora-Macro, bipolar, 4% Deadzone):** 0% = neutrale 1:1-Resynthese.
Negative Werte (Umbra) schneiden fundamentale Frequenzen weg, bringen leise
Ambient-Rauschanteile nach vorn (transparent/luftig). Positive Werte (Aurora) legen ein
Formant-/Chroma-Filter über die Partials für harmonische Vokalbetonung (Note-Tracking).

**Block-4-Detail (Ensemble/Drift):** Slew-Limiting auf das FFT-Tracking plus leichte
Gegeneinander-Verstimmung gerader/ungerader Partials — erzeugt einen organischen Chorus
nativ in der Oszillatorbank, ohne externe Delay-Lines.

**Block-5-Detail (Atmosphere-Macro, bipolar, 4% Deadzone):** 0% = saubere Grains mit
Hann-Window. Negative Werte (Blur) glätten die Grain-Hüllkurven extrem für kantenlose
Ambient-Wolken. Positive Werte (Radiation) reduzieren die Sample-Rate (Lofi) und glätten
Änderungen über einen BBD-Style-Slew-Limiter (Tape-Warble).

**Block-6-Detail (Character-Macro, bipolar, 4% Deadzone):** 0% = unbehandelte Hallfahne.
Negative Werte (Chorus) legen eine langsame Modulation auf den Reverb-Tail für einen breiten,
üppig-schimmernden ("lushen") Hallcharakter — wirkt, anders als Ensemble/Drift in Spectra, auch
auf Swarm-Anteile und trockenes Signal, da es am gemeinsamen Reverb-Send sitzt. Positive Werte
(Friction) legen eine nicht-lineare Sättigung (Tanh-Soft-Clipping) direkt in den Feedback-Loop
des Reverb-Tanks — bei hohen Werten dichte Overdrive-Wall. Chorus und Friction sind bewusst
exklusiv (ein Regler, zwei Richtungen), nicht gleichzeitig kombinierbar.

**Block-10-Detail (Destination):** Wählt, auf welche Signalstufe der Filter wirkt — cycelbar
durch **Input → Spectra → Swarm → Reverb**.

**Block-11-Settings-Untermenü** (eigener Cycle-Einstieg über "Settings" in der Multi-Cycle-Liste):
1. CPU/SDRAM-Meter (On/Off, Anzeige im Display)
2. Instant Playback Mode (On/Off) — ON: Resynthese startet sofort, Analyse verfeinert sich live
   während der Buffer vollläuft (reaktiv wie ein Reverb/Resonator). OFF: wartet auf vollen
   Buffer, dann einmalige Analyse (verhält sich wie ein Delay/Looper)
3. Scale (C-Major, Minor, Pentatonik — erweiterbar)
4. Intonation (Equal Temperament ↔ Just Intonation, für Block 7 Quantized)
5. Auto-Mod/Normalling (siehe 4.10)
6. **Audio Routing** (Stereo ↔ Sidechain) — siehe Detailbeschreibung unten

**Block-11-Detail (Audio Routing):**
- **Stereo (Default):** In L und In R arbeiten als normales Stereo-Paar (bzw. Mono-Split).
  Beide Eingänge werden gemischt in die 5 Trail-Ringbuffer aufgenommen und ganz normal
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

### 4.2 Trail-Level (×5)

- **Turn:** Lautstärke dieses Trails
- **Kurz drücken:** Lock (schützt vor Round-Robin-Ersetzung und Hold-Time-Ausklingen)
- **Lang drücken:** Solo

### 4.3 Mod-Slots (×4)

Cycle-Liste **Amplitude → Destination → Divider**. **Amplitude ist bipolar (Attenuverter):**
Mitte = 0 (keine Modulation, 4% Deadzone), nach rechts steigende positive Modulationstiefe,
nach links invertierte Modulation — jeweils angewandt auf die interne Quelle bzw. bei
gestecktem Kabel auf das externe CV-Signal. Destination referenziert einen beliebigen
Parameter aus der ParameterRegistry (Block 1–11 oder Trail-Level). Divider = Clock-Unterteilung
für den Fall interner Quelle. Interne Quellen: siehe Auto-Mod (4.10).

### 4.4 I/O-Buchsen

| Eingänge | Ausgänge |
|---|---|
| Mono In L | Mono Out L |
| Mono In R | Mono Out R |
| Clock | |
| Trig (neuer Trail, parallel zum Rec-Taster) | |
| Mod CV 1–4 (mit Schaltkontakt/Normalling) | |

**8 Eingänge + 2 Ausgänge = 10 Buchsen.**

### 4.5 Rec-Taster

Momentan-Taster, elektrisch parallel zur Trig-Buchse — identisches Signal, löst unabhängig vom
Threshold eine neue Aufnahme aus (gleiche Round-Robin-Logik wie automatischer Trigger).

### 4.6 Universeller Cycle-Mechanismus (10 Block-Potis + 4 Mod-Potis + 1 Multi-Encoder)

| Zustand | Aktion |
|---|---|
| Regler drehen, Cycle-Taster **nicht** gehalten | Ändert den Wert des zuletzt gebundenen Parameters (Start: erster Listeneintrag = Default) — mit Pickup/Catch (siehe unten), kein direkter Sprung |
| Cycle-Taster **gehalten** + Regler drehen | Blättert durch die Parameterliste — Display unten: Name/Position, oben: aktueller Wert. Keine Wertänderung |
| Cycle-Taster **loslassen** | Regler ab jetzt an zuletzt angezeigten Parameter gebunden |

**Pickup/Catch beim Regler-Rebinding:** Sobald ein Poti per Cycle-Taster neu an einen Parameter
gebunden wird, steht er fast nie an derselben physischen Position wie der gespeicherte Wert.
Damit das erste Drehen danach nicht zu einem Sprung im Wert führt, gilt grundsätzlich
**Pickup/Catch statt Jump**: Der gespeicherte Wert ändert sich erst, sobald die physische
Poti-Position durch Drehen den gespeicherten Wert "durchfährt" — erst ab diesem Moment folgt
der Wert 1:1 der Poti-Bewegung. Bis dahin bleibt der gespeicherte Wert unverändert stehen, auch
wenn am Poti gedreht wird. Gilt überall dort, wo eine Diskrepanz zwischen Poti-Position und
gespeichertem Wert entstehen kann: Cycle-Rebinding (hier), spätere Preset-Recall (siehe
Abschnitt 8) und perspektivisch beim Rebinden der Mod-Slot-Amplitude. Gilt **nicht** für den
Multi-Encoder — als Endlos-Encoder ohne feste physische Position kann dort keine Diskrepanz
entstehen, Pickup ist nicht nötig.

**Display-Kopplung (siehe 4.11):** Während der Catch-up-Phase erscheint eine durchgezogene
waagerechte Linie, die die physische Ist-Position des Potis zeigt, während der Balken selbst
weiter den gespeicherten, noch nicht übernommenen Wert zeigt. Sobald beide zusammenfallen,
"rastet" die Linie in den Balken ein, und der Poti übernimmt ab da die direkte Kontrolle. Diese
Linie ist bewusst anders gestaltet als die Punkte, die 4.11 für den modulierten Ist-Wert
beschreibt — beide operieren auf derselben Balkenhöhe, bedeuten aber unterschiedliche Dinge
(siehe Abgrenzung dort).

### 4.7 Cycle-Taster — Zusatzfunktionen (allein gedrückt, ohne Reglerbewegung)

| Geste | Aktion |
|---|---|
| Kurz, allein | **Play/Pause** (global, alle Trails) |
| Lang, allein | **Reset-Abfrage:** Display zeigt „Alle Trails löschen?" — ein erneuter Kurzdruck innerhalb 3 s bestätigt und löscht alle Trails; Timeout oder Reglerbewegung bricht ab. Während der Abfrage gilt Kurzdruck als Bestätigung, NICHT als Play/Pause |
| Gehalten + Regler drehen | Cycle-Modus (4.6) |

**Multi-Encoder-Push (eigener Taster am Encoder, Block 11):** Nach demselben Muster wie
Trail-Level-Push (4.2, kurz=Lock/lang=Solo) und Cycle-Taster (kurz/lang unterschiedlich belegt):
- **Kurz:** Schaltet einen Schritt weiter durch die eigene Cycle-Liste des Multi-Blocks
  (Dry/Wet → Macro1 → Macro2 → Settings → zurück zu Dry/Wet) — schnellerer Direktzugriff
  als der übliche Weg über "Cycle-Taster halten + Encoder drehen" (4.6), der weiterhin
  parallel funktioniert.
- **Lang:** **Return zum Home-Dashboard** — global, unabhängig davon, in welchem Block/Menü
  man sich gerade befindet (nicht nur bei Multi selbst gebunden). Einzige explizite
  Rückkehr-Geste im gesamten Bedienkonzept.

### 4.7a Rückkehr zum Home-Dashboard

Zwei Wege führen zurück zum Home-Dashboard (4.9), die sich ergänzen statt zu konkurrieren:

1. **Explizit:** Multi-Encoder-Push lang (siehe oben) — sofortige Rückkehr, unabhängig vom
   aktuellen Kontext.
2. **Automatisch per Inaktivitäts-Timeout:** Wurde **kein** Poti gedreht und **kein** Taster
   gedrückt, springt das Display nach **7 Sekunden** automatisch zum Home-Dashboard zurück —
   unabhängig davon, ob man gerade in einer Cycle-Anzeige, einem Settings-Untermenü oder einer
   Segmented-Auswahl steht. Die zuletzt gebundenen Poti-Zuordnungen bleiben davon unberührt;
   nur die Anzeige wechselt, keine Regler werden entkoppelt. 7 s ist ein Startwert (Zielkorridor
   5–10 s, siehe Kalibrierungshinweis in 4.11) — lang genug, um einen Wert in Ruhe abzulesen,
   kurz genug, um nicht unnötig auf einer Cycle-Anzeige "gefangen" zu bleiben. An echter
   Hardware im praktischen Gebrauch feinjustieren.

### 4.8 Capture-Modell (Trail-Pool)

- **Anzahl** (Block 1): wie viele der 5 Slots aktiv genutzt werden (1–5)
- **Buffer** (Block 2): Länge des SDRAM-Ringbuffers pro Trail = maximale Aufnahmedauer
  einer einzelnen Aufnahme
- **Threshold**: triggert automatische Aufnahme in den ältesten, nicht gelockten aktiven Trail
  (Round-Robin)
- **Cont. Rec** (Continuous Recording): re-triggert dauerhaft neue Aufnahmen, solange das
  Eingangssignal über dem Threshold liegt, statt auf Unterschreiten zu warten
- **On/Off**: globaler Bypass/Enable des Capture-Systems
- **Manueller Trigger** (Rec-Taster/Trig): gleiche Round-Robin-Logik
- **Hold** (Block 2): Countdown bis max. 30s; jeder höhere Wert springt logisch auf infinite
  (auch Boot-Default)
- **Instant Playback Mode** (Settings, 4.1): siehe Beschreibung dort

### 4.9 Display-Konzept

SSD1309, 128×64 px. Enthält: Cycle-Anzeige (Name unten, Wert oben), Home-Dashboard mit
Trail-Status (Level/Lock/Solo), Input-Threshold-VU-Meter mit Schwellen-Markierung,
CPU/SDRAM-Meter (oben rechts, per Settings ausblendbar), "Wandering Beams" — rotierende,
sich verkürzende Strahlen um das Trail-Symbol als Visualisierung der verbleibenden Hold-Time
(kürzer/langsamer = näher am Ausklingen), Reset-Bestätigungsdialog („Alle Trails
löschen?", siehe 4.7).

### 4.10 Auto-Mod / Normalling (interne Quellen der Mod-CV-Buchsen)

Meldet die Jack-Erkennung (Abschnitt 2, Punkt 7) eine Mod-CV-Buchse als „nicht gesteckt",
liefert der betreffende Slot eine interne Quelle. Welche, bestimmt die Settings-Auswahl
**OFF / Age / Pitch / Both** (Block 11):

- **OFF:** einfacher interner LFO (Dreieck/Sinus-Mischung), Rate = Clock-Periode × Divider
  (Standardverhalten, Phase 10).
- **Age (Alter der Aufnahme):** lineare Hüllkurve über die Lebensdauer eines Trails — startet
  bei 0 % im Moment der Aufnahme und steigt auf 100 %, je näher der Trail seinem Ende
  (Hold-Time-Fade-Out) kommt. Musikalischer Nutzen (Modulation über Zeit): Sounds verändern
  sich autonom, während sie altern — z. B. Filter langsam öffnen, die Granular-Cloud immer
  kleiner zerhacken oder den Reverb-Send aufdrehen, kurz bevor eine Klangwolke stirbt und im
  Round-Robin ersetzt wird.
- **Pitch (automatische Tonhöhen-Erkennung):** die FFT-Analyse kennt die Grundfrequenz des
  aufgenommenen Materials ohnehin; sie wird in einen kontinuierlichen Steuerwert übersetzt
  (tiefe Töne = niedriger Wert, hohe Töne = hoher Wert). Musikalischer Nutzen (Modulation
  über Tonhöhe): klassisches Key-Tracking — z. B. Filter-Cutoff bei hohen Lead-Tönen weiter
  öffnen als bei tiefen Bass-Drones, oder bei tiefen Tönen den Reverb-Send reduzieren, damit
  der Mix untenrum nicht vermatscht.
- **Both:** beide Modulationen greifen gleichzeitig — der interne Slot-Wert ist das
  arithmetische Mittel aus Age-Hüllkurve und Pitch-Tracking-Wert. Bewusst additiv statt
  multiplikativ: Beide Einflüsse bleiben über den gesamten Lebenszyklus gleichmäßig und
  subtil hörbar, statt sich gegenseitig zu verstärken oder auszulöschen.

Merkregel: **Age = Modulation über Zeit** (wie lange lebt die Stimme schon?),
**Pitch = Modulation über Note** (wie hoch ist die Stimme?).

**Quell-Trail:** Bei bis zu 5 simultanen Trails liefert der **jüngste nicht-gelockte
aktive Trail** den Age-/Pitch-Wert. Fallback: Sind alle aktiven Trails gelockt, liefert
der jüngste aktive Trail (unabhängig von Lock). Existiert kein aktiver Trail, ist der
Wert 0 — ein reiner Ruhezustand ohne praktische Relevanz, da es dann ohnehin nichts
Hörbares gibt, das moduliert werden müsste. Regel 2.5 gilt trotzdem: Ein auf Level 0
stehender, aber weiterhin aktiver Trail bleibt als Age-/Pitch-Modulationsquelle nutzbar.

### 4.11 Display-Designsystem: Cycle-Parameter-Darstellung

Generisches Anzeige-Vokabular für **jeden** Parameter in **jedem** Block — Blöcke referenzieren
nur noch, welcher der vier Typen für welchen Parameter gilt, statt das Aussehen jedes Mal neu
zu beschreiben. Gilt gleichermaßen für Block-Potis (1–10), Mod-Slots (4) und den Multi-Encoder.

**Gemeinsamer Bildschirmaufbau (128×64 SSD1309), von oben nach unten:**
1. Kopfzeile: Block-/Kontextname links, Position "n/m" rechts (z. B. "2/4")
2. Eine durchgehende horizontale **Decken-Linie** über die volle Breite — gemeinsame
   100 %-Referenz für alle Parameter des aktuellen Blocks gleichzeitig, damit ihre
   Balkenhöhen direkt gegeneinander vergleichbar sind (nicht pro Balken einzeln)
3. Parameterfläche: bis zu 5 Spalten gleicher Breite (bei Blöcken mit ≤5 Cycle-Einträgen;
   Mod-Slots/Multi haben weniger)
4. Segmentierte, eingerahmte Zeile mit allen Parameter-Kürzeln (3–4 Zeichen), aktiver
   Eintrag voll invertiert (weiße Fläche, schwarzer Text)

**Aktiver Parameter — zwei senkrechte Linien statt geschlossenem Rahmen:** Die Spalte des
gerade gebundenen Parameters bekommt links und rechts je eine senkrechte Linie, beginnend
nahtlos an der Decken-Linie, ohne Lücke durchlaufend bis in die Segmented-Zeile darunter.
Kein Linienabschluss oben/unten nötig — die Decken-Linie und die Box selbst übernehmen diese
Funktion bereits. Der Zahlenwert des aktiven Parameters steht freistehend zwischen Kopfzeile
und Decken-Linie, mittig über seiner Spalte.

**Vier Darstellungstypen** (welcher Typ gilt, steht pro Parameter im jeweiligen Block):

1. **Unipolar (0–100 %)** — z. B. Buffer, Mix, Cutoff, Umbra/Aurora-Betrag ohne Vorzeichen.
   Balken wächst von der Grundlinie (0 %, an der Segmented-Box) nach oben zur Decken-Linie
   (100 %). Aktiv: breiter (14px), Zahlenwert sichtbar. Inaktiv: schmal (6px), kein Zahlenwert
   — Balkenbreite ist die einzige verbleibende Unterscheidung zu aktiv/inaktiv (keine
   Graustufen, da Display monochrom).

2. **Bipolar (±100 %, mit Mittelwert)** — z. B. Umbra/Aurora-Macro, Atmosphere-Macro,
   Character-Macro, Crossfade-/Pan-Drift-Velocity. Balken wächst vom Zentrum der Spalte aus
   nach oben (positiv) oder unten (negativ); Decken-Linie oben = +100 %, Segmented-Box unten
   = −100 %. Gestrichelte Mittellinie als Nullreferenz: **volle Spaltenbreite beim aktiven
   Parameter**, **halbe Spaltenbreite bei inaktiven bipolaren Parametern** (reicht als
   Hinweis "ist bipolar", ohne mit dem Balken zu konkurrieren).

3. **Toggle (2 Zustände, z. B. On/Off, Cont. Rec, Quantized, Instant Playback)** — kein
   Balken. Beide Zustände bleiben nebeneinander sichtbar (links/rechts, passend zur
   Drehrichtung des Potis/Encoders), der aktuelle invertiert dargestellt. Alle übrigen
   Parameter der Spaltenreihe bleiben dabei normal sichtbar — der Toggle nimmt nur seine
   eigene Spalte ein, nie die volle Bildschirmbreite.

4. **Zählwert (feste Einheit ohne %-Bezug, z. B. Partials, kleine Sekundenwerte wie Fade
   In/Out)** — zwei Unterfälle:
   - Wertebereich groß genug, um von einer Balkendarstellung zu profitieren (z. B. Partials,
     4–64): identischer Mechanismus wie unipolar, aber das Label zeigt die echte Zahl statt
     Prozent, und die Decken-Linie bekommt eine kleine Zusatzangabe des Maximums oben rechts
     neben der Positionsanzeige (z. B. "1/4 · max 64").
   - Wertebereich klein/schnell erfassbar (z. B. Fade In/Out, Anzahl der Trails): **kein
     Balken**, nur die Zahl selbst — aktiv groß und mittig zwischen den beiden seitlichen
     Linien, inaktiv klein am unteren Spaltenrand.

**Enums mit ≥3 benannten Optionen** (Destination, Scale, Intonation, Audio Routing, Auto-Mod)
fallen NICHT unter dieses Vier-Typen-System — sie bekommen einen eigenen horizontalen
Segmented-Control-Bildschirm (siehe Auto-Mod-Beispiel, 4.10), da hier kein Balken sinnvoll ist.

**Modulierter Ist-Wert (wenn ein Mod-Slot auf diesen Parameter zeigt):** Der Balken selbst
bleibt unverändert der Registry-Basiswert (vom Poti gebunden, ruhig stehend). Zusätzlich
erscheint links und rechts je eine kurze **Punktlinie aus 2 Punkten** auf der Höhe des
momentanen modulierten Ist-Werts — sie wandert live mit der Mod-Quelle rauf und runter. Die
Punkte sitzen bewusst weiter außen als die Catch-up-Linie (4.6) je reicht: Punkt 1 auf Höhe
der Catch-up-Reichweite, Punkt 2 noch etwas weiter außen. Bei bipolaren Parametern gilt
dasselbe relativ zum Zentrum statt zur Grundlinie. Erscheint nur, wenn aktuell ein Mod-Slot
(4.3) auf den gerade aktiven Parameter geroutet ist — sonst bleibt nur der normale Balken
sichtbar.

**Abgrenzung zur Catch-up-Anzeige:** Punktlinie (Modulation) und durchgezogene Linie
(Catch-up, siehe 4.6) sind bewusst unterschiedlich weit und unterschiedlich dicht gestaltet,
obwohl beide auf derselben Balkenhöhe operieren auftreten können — Punkte = "läuft automatisch
mit einer Mod-Quelle", Linie = "wartet auf Übernahme durch den Poti". Fallen beide auf dieselbe
Höhe, verschmelzen sie bewusst zu einem einzigen optischen Eindruck: eine Linie mit
durchgezogener Mitte (Catch-up) und gepunkteten Spitzen, die darüber hinausragen (Modulation)
— liest sich als "hier ist mehr los als nur Catch-up", nicht als Verwechslung. Beide Zustände
können also gleichzeitig sichtbar sein, ohne dass eine Priorisierung nötig wäre.

**Kalibrierungshinweis:** Alle oben beschriebenen Feinheiten (Linienbreiten, Zusatzlabels wie
"max 64", gestrichelte Mittellinien) sind am Bildschirm bislang nur simuliert. Bei
128×64 Pixeln und ~5–6px Zeichenhöhe kann Feindetail auf dem echten SSD1309 unleserlich
werden — vor Phase 4 (erster Block mit allen vier Darstellungstypen gleichzeitig: Spectra-
Parameter) an echter Hardware gegenprüfen und bei Bedarf vergröbern.

**Noch offen (Abschnitt 8):** Poti-/Encoder-Drehrichtung (im Uhrzeigersinn = welcher Zustand
bei Toggles, welche Richtung bei bipolaren Werten) hängt von der finalen
Hardware-Verdrahtung ab und ist noch nicht festgelegt.

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
| 2 | Trail-Level-Pushes, Rec-Taster/Trig, Menü-Taster-Gesten | Lock/Solo/Level auf Dummy-Werten, Debouncing sauber |
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
  dieselbe Gesten-Klasse später auch für Trail-Level und Rec-Taster
- Display-Update: Cycle-Anzeige (unten Name, oben Wert), simples Dummy-Dashboard
- Implementiere das komplette Display-Designsystem aus 4.11 (Decken-Linie, seitliche
  Linien statt Rahmen, Segmented-Zeile, invertierte Auswahl) bereits jetzt anhand der
  3 Dummy-Instanzen — z. B. D1 unipolar mit Balken, D2 Toggle, D3 bipolar mit
  gestrichelter Mittellinie. So lässt sich die komplette Anzeige-Logik an echter
  Hardware testen, bevor in Phase 3+ die ersten echten Parameter angemeldet werden.

Baue alles so, dass spätere Phasen nur noch echte CycleRow-Instanzen mit echten,
in der ParameterRegistry angemeldeten Parametern anlegen müssen — das Display-
Designsystem selbst bleibt dabei unverändert wiederverwendbar.
```

### Phase 2 — Trail-Level, Rec-Taster, Menü-Gesten

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 4.2 und 4.5.

Implementiere 5 Trail-Level-Push-Potis (Turn=Dummy-Level, kurz=Dummy-Lock,
lang=Dummy-Solo) mit der Gesten-Klasse aus Phase 1. Rec-Taster und Trig-Eingang
lösen denselben Dummy-Callback aus. Zeige Lock/Solo/Level aller 5 Trails im
Dashboard.
```

### Phase 3 — Capture-Engine

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkt 2, 4), 4.1 (Block 1+2),
4.8.

Implementiere die Capture-Engine:
- 5 Ringbuffer via `DSY_SDRAM_BSS float trail_buffer[5][BUFFER_SIZE];`
- Echte CycleRow für Block 1 (Anzahl, Threshold, Cont. Rec, On/Off) und Block 2
  (Buffer, Hold, Fade In, Fade Out), ersetzt die Dummy-Zeilen
- Round-Robin-Aufnahme bei Threshold-Überschreitung ODER Rec-Taster/Trig, in den
  ältesten nicht gelockten aktiven Trail
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
(Sine↔Saw↔Fold, bipolar um die Mittelstellung, 4% Deadzone zwingend gemäß Abschnitt 2
Punkt 8), Umbra/Aurora-Macro
(bipolar, 4% Deadzone, siehe 4.1-Detailbeschreibung), Ensemble/Drift (Slew-Limiting +
Partial-Detuning für nativen Chorus). Pitch Spectra vorerst direkt gesetzt (volle Block-3-
Integration folgt Phase 6). Registriere alles in der ParameterRegistry.
```

### Phase 5 — Swarm-Engine

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 3+5).

Implementiere die granulare Swarm-Engine auf den Trail-Ringbuffern. Echte CycleRow
für Block 5: Size (Grain-Länge), Spread (Stereo-Streuung einzelner Grains), Scan
(Scrubbing-Geschwindigkeit, 0=Freeze), Atmosphere-Macro (bipolar: Blur↔Radiation,
4% Deadzone, siehe 4.1-Detailbeschreibung inkl. BBD-Style-Slew bei Radiation).
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
  Decay, Damping, Character-Macro (bipolar, 4% Deadzone: negativ = langsame
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
- Pan-Drift: pro Trail unabhängiger LFO (Dreieck/Sine-Mischung + leichter Jitter),
  Constant-Power-Panning. CycleRow Block 8: Phase (Phasenversatz zwischen den
  LFOs der Trails, 0%=synchron, 100%=maximal versetzt), Amplitude (Auslenkung),
  Velocity (Geschwindigkeit)
- Crossfade: wandernde Amplituden-Welle über die aktiven Trails gemäß 4.1
  (Block-9-Detail), multiplikativ auf der VCA-Stufe NACH den Pre-Fader-Abgriffen.
  CycleRow Block 9: Amplitude (Wellentiefe), Velocity (bipolar: Vorzeichen =
  Wander-Richtung, 4% Deadzone, Mitte = Fokus-Freeze); zusätzlich
  Slew-Rate-Limiting (BBD-artig) beim Round-Robin-Ersetzen eines Trails
- Display: "Wandering Beams" — rotierende, sich mit ablaufender Hold-Time
  verkürzende/verlangsamende Strahlen um jedes Trail-Symbol
```

### Phase 10 — Mod-System

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere Abschnitt 2 (Punkt 7), 4.3, 4.4.

Implementiere die 4 Mod-Slots: Jack-Erkennung über Schaltkontakt (nicht
Spannungs-Heuristik) — gestecktes Kabel = externer CV wird gelesen, sonst interne
Quelle gemäß Auto-Mod-Setting (4.10): OFF = einfaches Dreieck/Sinus mit Rate =
Clock-Periode × Divider; Age/Pitch/Both siehe 4.10 (Age-Hüllkurve und
Pitch-Tracking-Wert werden in Phase 11 an das Settings-Menü angebunden, die
Quellen-Abstraktion aber schon hier vorsehen). CycleRow pro Slot: Amplitude
(bipolarer Attenuverter, 4% Deadzone, siehe 4.3), Destination (aus
ParameterRegistry, alle Blöcke 1–11 sowie Trail-Level), Divider.
```

### Phase 11 — Multi, Settings & Kalibrierung

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md, insbesondere 4.1 (Block 11, Settings-Untermenü).

Implementiere:
- CycleRow für Multi-Encoder: Dry/Wet global, Macro1, Macro2 (Ziele vorerst fest im
  Code, klar kommentiert — siehe Abschnitt 8 Offene Punkte), Settings
- Multi-Encoder-Push (kurz/lang, siehe 4.7a): kurz = Schritt durch die Multi-CycleRow,
  lang = globale Rückkehr zum Home-Dashboard
- Inaktivitäts-Timeout (7s, siehe 4.7a): Display-State-Machine springt bei fehlender
  Poti-/Taster-Aktivität automatisch zum Home-Dashboard zurück, unabhängig vom Kontext
- Settings-Untermenü: CPU/SDRAM-Meter-Toggle, Instant-Playback-Mode-Toggle,
  Scale-Auswahl (C-Major/Minor/Pentatonik), Intonation-Toggle (Equal/Just),
  Auto-Mod/Normalling-Auswahl (OFF/Age/Pitch/Both, siehe 4.10 — steuert, was
  ungesteckte Mod-CV-Buchsen intern liefern, sobald die Jack-Erkennung
  "nicht gesteckt" meldet),
  Audio-Routing-Toggle (Stereo/Sidechain, siehe 4.1-Detailbeschreibung): im
  Sidechain-Mode wird die in Phase 3 vorbereitete Buffer-Signalquelle exklusiv auf
  In R umgeschaltet, In L wird direkt (trocken, unter Umgehung der Trail-Buffer) mit
  dem Spectra/Swarm/Reverb-Ausgang auf Out L/R gemischt
- Kalibrierungsroutine (Min/Max-Lernmodus) für Threshold und alle CV-Eingänge
- Review: alle Display-Texte auf 128×64px-Lesbarkeit prüfen
```

---

## 8. Offene Punkte

- **~~Umbra/Corona-Namenskollision~~ Erledigt:** Macro heißt jetzt Umbra/Aurora (inkl. Abschnitt 2, Punkt 8)
- **~~"Spread" doppelt vergeben~~ Erledigt:** Block 8 heißt jetzt Phase statt Spread
- **~~Deadzone uneinheitlich benannt~~ Erledigt:** einheitlich 4%-Deadzone (0.48–0.52), zwingend für alle bipolaren Parameter
- **~~Reset-Geste riskant (lang gedrückt = sofortiges Löschen)~~ Erledigt:** Bestätigungsdialog im Display, siehe 4.7
- **~~Auto-Mod-Quell-Trail~~ Erledigt:** jüngster nicht-gelockter aktiver Trail (Fallback siehe 4.10)
- **~~Crossfade-Welle vs. Lock/Solo~~ Erledigt:** Solo übersteuert die Welle, Lock schützt nicht vor ihr (4.1, Block-9-Detail)
- **~~Both-Kombinationsformel~~ Erledigt:** arithmetisches Mittel aus Age und Pitch (4.10), bewusst subtil statt aufdringlich
- **Macro1/Macro2-Zielzuweisung:** aktuell fest im Code (Phase 11), noch keine Front-Panel-UI
  dafür entschieden
- **~~Multi-Encoder Push-Funktion~~ Erledigt:** kurz = Schritt durch Multi-Cycle-Liste,
  lang = globale Rückkehr zum Home-Dashboard (siehe 4.7a)
- **Poti-/Encoder-Drehrichtung:** im Uhrzeigersinn = welcher Zustand bei Toggles
  (links/rechts, siehe 4.11), welche Richtung bei bipolaren Werten — abhängig von
  finaler Hardware-Verdrahtung, noch nicht festgelegt
- **Macro1/Macro2 im Display-Designsystem:** sobald Zielzuweisung (siehe oben) steht,
  muss festgelegt werden, welcher der 4 Darstellungstypen aus 4.11 greift, je nachdem
  was zugewiesen ist
- **Modulations-Mechanik auf den Registry-Basiswert:** additiv direkt auf den
  Wertzeiger (mit Clamping an Min/Max), oder separater Modulations-Offset, der erst im
  Audio-Callback mit dem Basiswert verrechnet wird? Betrifft auch, ob das Display (siehe
  4.11, modulierter Ist-Wert) den tatsächlichen Registry-Wert oder einen getrennten
  Ist-Wert anzeigt
- Preset-Speicherung: architektonisch durch die ParameterRegistry vorbereitet (Flash-Persistenz),
  aber erst nach Stabilität der V1.0-Firmware geplant — Pickup/Catch (4.6) greift dann auch
  beim Preset-Recall, sobald Presets existieren
