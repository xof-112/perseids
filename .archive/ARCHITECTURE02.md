# Flaresum — Architektur & Entwicklungsplan

> Dieses Dokument ist die **einzige Referenzquelle** für die KI-gestützte Entwicklung in Cursor.
> Lege es als `ARCHITECTURE.md` in den Projekt-Root und beginne **jeden** Cursor-Prompt mit:
> „Lies zuerst ARCHITECTURE.md im Projekt-Root, bevor du etwas implementierst.“

Inspiriert vom Plugin **Coastline** (Aqeel Aadam Sound), aber eigenständig weiterentwickelt: ein globaler
Klangkörper (zwei Resynthese-Engines, Reverb, Filter, Spectral Resonator) verarbeitet bis zu 5 gleichzeitig
gehaltene Audio-Schnappschüsse ("Flares"). Pro Flare gibt es nur einen schlanken Mixer-Zugriff
(Level/Lock/Solo) — der Rest ist bewusst global, nicht pro Flare, um die Bedienoberfläche schlank zu halten.

**Namensgebung:** Modul = *Flaresum*. Ehemalige "Voices" = **Flares**.

---

## 0. Zielplattform

- **MCU/Board:** Electrosmith Daisy Seed (STM32H750, 480 MHz Cortex-M7)
- **Framework:** libDaisy (Hardware-Abstraktion) + DaisySP (DSP-Bausteine)
- **Sprache:** C++ (PlatformIO oder Daisy-eigenes CMake-Toolchain-Setup)
- **Custom Carrier-PCB** nötig für die Anbindung aller Potis/Encoder/Taster/Buchsen/Display

Falls eine andere Plattform gewünscht ist: nur diesen Abschnitt anpassen, der Rest bleibt gültig.

---

## 1. Vollständige Hardware-Architektur

### 1.1 Bedienelemente — Übersicht

| Gruppe | Anzahl | Typ | Funktion |
|---|---|---|---|
| Block-Potis (1–10) | 10 | Poti | Je Zugriff auf 2–4 Unterparameter via Cycle-Taster (siehe 1.6) |
| Flare-Level | 5 | Poti mit Push | Turn = Level dieser Flare; kurz = Lock; lang = Solo |
| Mod-Slots | 4 | Poti | Amplitude; Destination/Divider via Cycle-Taster; Quelle intern oder CV-In |
| Multi | 1 | Encoder | Dry/Wet global, Macro1, Macro2 — via Cycle-Taster wie die Block-Potis |
| Cycle-Taster | 1 | Taster (neben Display) | Siehe 1.6 und 1.7 |
| Rec-Taster | 1 | Taster (parallel zur Trig-Buchse) | Manueller Aufnahme-Trigger |

**Gesamt: 19 Potis + 1 Encoder + 2 Taster.** Kein Bauteil hat eine Doppelrolle außer dem Cycle-Taster
(bewusst, siehe 1.7) und den Flare-Level-Pushes (Lock/Solo).

### 1.2 Die 11 Funktionsblöcke (global, nicht pro Flare)

| # | Block | Unterparameter (Cycle-Liste, erster Eintrag = Default) |
|---|---|---|
| 1 | **Flares** | Anzahl (1–5), Threshold, On/Off |
| 2 | **Time** | Buffer, Hold, Fade In/Out |
| 3 | **Engines** | Blend (Resynth↔Grain), Pitch Resynth, Pitch Grain |
| 4 | **Resynth Parameter** | Anzahl (Partials), Shape/Fold, Chorus |
| 5 | **Grain Parameter** | Anzahl, Position, Velocity, Size |
| 6 | **Reverb** | Mix, Size, Decay |
| 7 | **Spectral Resonator** (wirkt auf Grain-Ausgang) | Mix, Quantized, Decay, Pitch |
| 8 | **Pan Drift** | Spread, Amplitude, Velocity |
| 9 | **Crossfade über 5 Flares** | Amplitude, Velocity |
| 10 | **Filter Mix** | Destination, HP, LP |
| 11 | **Multi** (Encoder statt Poti) | Dry/Wet global, Macro1, Macro2 |

Blöcke 1–10 sitzen auf je einem Poti. Block 11 sitzt auf dem Multi-Encoder. Alle 11 Blöcke nutzen
denselben Cycle-Mechanismus (1.6).

### 1.3 Flare-Level (×5)

Jede der 5 Flares hat einen eigenen Push-Poti:

- **Turn:** Lautstärke dieser Flare
- **Kurz drücken:** Lock (schützt vor Ersetzung durch neue Aufnahmen und vor Hold-Time-Ausklingen)
- **Lang drücken:** Solo (nur diese Flare hörbar, zum Anhören/Debuggen)

### 1.4 Mod-Slots (×4)

Jeder Slot hat einen Poti mit Cycle-Liste **Amplitude → Destination → Divider**:

- **Amplitude:** Tiefe/Attenuverter der Modulation
- **Destination:** Zielparameter (beliebiger Parameter aus einem der 11 Blöcke oder Flare-Level)
- **Divider:** Clock-Unterteilung, relevant wenn die Quelle intern/clock-abgeleitet ist

**Quelle intern oder CV-In:** Vorschlag (zu bestätigen, siehe Abschnitt 6): automatische Umschaltung per
Jack-Erkennung — ist an der zugehörigen Mod-CV-Buchse ein Kabel gesteckt, wird das externe Signal
verwendet; sonst generiert der Slot intern eine einfache clock-synchronisierte Modulationsquelle
(Rate = Divider × Clock-Periode).

### 1.5 I/O-Buchsen

| Eingänge | Ausgänge |
|---|---|
| Mono In L | Mono Out L |
| Mono In R | Mono Out R |
| Clock | |
| Trig (neuer Flare, parallel zum Rec-Taster) | |
| Mod CV 1–4 | |

**8 Eingänge + 2 Ausgänge = 10 Buchsen gesamt.**

### 1.6 Universeller Cycle-Mechanismus (gilt für alle 15 Zeilen: 10 Block-Potis + 4 Mod-Potis + 1 Multi-Encoder)

| Zustand | Aktion |
|---|---|
| Regler drehen, Cycle-Taster **nicht** gehalten | Ändert den Wert des zuletzt gebundenen Parameters dieser Zeile (Start: erster Eintrag der Liste = Default) |
| Cycle-Taster **gehalten** + Regler drehen | Blättert durch die Parameterliste dieser Zeile — Display unten: Listenposition/Name, Display oben: aktueller Wert des angezeigten Parameters. **Keine** Wertänderung während des Blätterns |
| Cycle-Taster **loslassen** | Der Regler ist ab jetzt an den zuletzt angezeigten Parameter gebunden, bis erneut geblättert wird |

Jede der 15 Zeilen merkt sich ihre Bindung unabhängig von den anderen.

### 1.7 Cycle-Taster — Zusatzfunktionen (allein gedrückt, ohne Reglerbewegung)

| Geste | Aktion |
|---|---|
| Kurz, allein | **Play/Pause** (global, alle Flares) |
| Lang, allein | **Reset** (alle Flares sofort löschen) |
| Gehalten + Regler drehen | Cycle-Modus (siehe 1.6) |

Die drei Bedeutungen sind in der Firmware eindeutig unterscheidbar: entscheidend ist, ob während des
Drückens eine Reglerbewegung registriert wurde (→ Cycle-Modus) oder nicht (→ Play/Pause bzw. Reset je
nach Druckdauer).

### 1.8 Rec-Taster

Momentan-Taster, elektrisch parallel zur Trig-Buchse (z.B. per Diode verknüpft) — identisches Signal,
löst unabhängig vom Threshold eine neue Aufnahme aus. Zielauswahl: siehe 1.9 (gleiche Round-Robin-Logik
wie beim automatischen Threshold-Trigger).

### 1.9 Capture-Modell (Flare-Pool)

- **Anzahl** (Block 1) legt fest, wie viele der 5 möglichen Flare-Slots aktiv genutzt werden (1–5)
- **Threshold** (Block 1) triggert automatisch eine neue Aufnahme in die **älteste, nicht gelockte**
  aktive Flare (Round-Robin-Ersetzung)
- **On/Off** (Block 1) = globaler Bypass/Enable des gesamten Capture-Systems
- **Manueller Trigger** (Rec-Taster/Trig-Buchse) nutzt dieselbe Round-Robin-Logik wie der automatische
  Threshold-Trigger — es gibt keinen "fokussierten" Flare-Begriff mehr in dieser Architektur
- **Buffer, Hold, Fade In/Out** (Block 2 „Time") sind globale Werte für Aufnahmelänge, Ausklingzeit und
  Ein-/Ausblendzeit — Letztere wird auch für Play/Pause (1.7) verwendet

### 1.10 Display-Konzept

SSD1309 OLED, 128×64px, 2,42″. Beim Blättern (1.6) zeigt die untere Zeile Listenposition/Parametername,
die obere Zeile den aktuellen Wert. Im Ruhezustand: Dashboard-Ansicht mit Status aller aktiven Flares
(Level/Lock/Solo-Zustand) — genaues Layout ist Teil von Phase 1.

---

## 2. Entwicklungsprinzipien

1. **UI-Mechanik zuerst, komplett auf Dummy-Werten**, bevor echte DSP dazukommt — der Cycle-Mechanismus
   (1.6) ist das Herzstück der Bedienung und muss sich für sich allein richtig anfühlen.
2. **Jede Phase = ein Cursor-Prompt = ein Git-Commit.**
3. **Nach jeder Phase ein konkretes, hörbares/sichtbares Testkriterium.**
4. **Der Cycle-Mechanismus wird EINMAL generisch implementiert** (eine Klasse/Modul, das eine
   Parameterliste + gebundenen Index verwaltet) und danach für alle 15 Zeilen wiederverwendet — nicht
   pro Block neu schreiben.
5. **ARCHITECTURE.md ist Quelle der Wahrheit.** Änderungen hier zuerst, dann erst den nächsten Prompt
   schreiben.

---

## 3. Phasen-Roadmap

| Phase | Inhalt |
|---|---|
| 0 | Projekt-Setup, Blink-Test |
| 1 | Generischer Cycle-Mechanismus + Display + Cycle-Taster-Gesten (Play/Pause/Reset), alles auf Dummy-Parametern |
| 2 | Flare-Level-Push-Potis (Lock/Solo) + Rec-Taster/Trig-Callback, weiterhin Dummy |
| 3 | Capture-Engine: Flares-Pool (Anzahl/Threshold/On-Off), Ringbuffer, Round-Robin, Time-Block real angebunden |
| 4 | Resynth-Engine (additiv) inkl. Resynth-Parameter-Block und Pitch Resynth |
| 5 | Grain-Engine (granular) inkl. Grain-Parameter-Block und Pitch Grain |
| 6 | Engine-Blend (Resynth↔Grain, global) |
| 7 | Spectral Resonator (auf Grain-Ausgang) |
| 8 | Reverb + Filter Mix |
| 9 | Pan Drift + Crossfade-über-5-Flares (automatische Bewegung im Flare-Pool) |
| 10 | Mod-System: 4 Slots, CV-Jack-Erkennung, Clock/Divider-Anbindung |
| 11 | Multi-Block (Dry/Wet, Macro1/2) + Kalibrierung & Feinschliff |

---

## 4. Phasen im Detail

### Phase 0 — Projekt-Setup

**Fertig, wenn:** Projekt compiliert/flasht, LED blinkt.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root.

Richte ein neues libDaisy/DaisySP-Firmware-Projekt für Electrosmith Daisy Seed ein
(PlatformIO, Standard-Ordnerstruktur, ARCHITECTURE.md bleibt im Root). Erstelle
main.cpp mit einem Blink-Test auf der Onboard-LED zur Verifikation des Build-/
Flash-Workflows. Kein Audio-Code in dieser Phase.
```

---

### Phase 1 — Cycle-Mechanismus, Display, Cycle-Taster-Gesten

**Fertig, wenn:** Mindestens 3 Dummy-Zeilen (unterschiedliche Parameteranzahl) lassen sich per Cycle-
Taster+Regler durchblättern und binden; Play/Pause und Reset (Taster allein, kurz/lang) lösen sichtbare
Dummy-Callbacks aus; Display zeigt beim Blättern unten Position/Name, oben Wert.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.6, 1.7 und 1.10.

Implementiere den universellen Cycle-Mechanismus als eigenständige, wiederverwendbare
Klasse (z.B. CycleRow), die verwaltet:
- eine Liste benannter Parameter (Name, Wertebereich, aktueller Wert, Setter/Getter)
- einen "gebundenen Index" (Start: 0 = erster Eintrag)
- Methoden für: WertÄndern(delta) [wirkt auf gebundenen Parameter], Blättern(delta)
  [verändert nur den gebundenen Index, keine Wertänderung]

Instanziiere davon 3 Dummy-CycleRow-Objekte mit je 2-4 Platzhalter-Parametern (float,
Name als String), simuliere Regler-Input erstmal über einen zusätzlichen Test-Encoder
oder Taster-Kombination, falls noch keine echten Potis am Board hängen.

Implementiere den Cycle-Taster (Abschnitt 1.7) mit klarer Unterscheidung:
- gehalten + Reglerbewegung während des Haltens erkannt -> CycleRow::Blättern() auf
  der aktuell aktiven Zeile
- allein gedrückt (keine Reglerbewegung während des Drückens), kurz -> Play/Pause-
  Dummy-Callback (z.B. Toggle eines globalen Bool + Debug-Ausgabe)
- allein gedrückt, lang (Schwelle konfigurierbar, Start 600ms) -> Reset-Dummy-Callback

Implementiere das Display-Update gemäß Abschnitt 1.10: beim Blättern unten Name/
Position, oben Wert des angezeigten Parameters; außerhalb des Blätterns ein simples
Platzhalter-Dashboard.

Baue die Architektur so, dass spätere Phasen nur noch echte CycleRow-Instanzen mit
echten Parametern anlegen müssen (siehe Abschnitt 1.2), ohne den Mechanismus selbst
zu ändern.
```

---

### Phase 2 — Flare-Level & Rec-Taster

**Fertig, wenn:** 5 Dummy-Flare-Level reagieren auf Turn/kurz/lang; Rec-Taster und Trig-Eingang lösen
denselben Dummy-Callback aus.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.3, 1.5 und 1.8.

Implementiere:
- 5 Flare-Level-Push-Potis: Turn ändert einen Dummy-Level-Float pro Flare; kurz
  drücken togglet einen Dummy-Lock-Bool; lang drücken togglet einen Dummy-Solo-Bool
  (nutze für die Druckdauer-Erkennung dieselbe Gesten-Logik-Klasse wie beim
  Cycle-Taster aus Phase 1, nicht neu implementieren)
- Rec-Taster UND Trig-CV-Eingang lösen denselben Callback aus (Dummy: Debug-Ausgabe
  "Manual Trigger"), da beide elektrisch/logisch dasselbe Signal repräsentieren
  (Abschnitt 1.8)

Zeige den Lock/Solo/Level-Zustand aller 5 Flares im Dashboard (Abschnitt 1.10) an.
```

---

### Phase 3 — Capture-Engine (Flare-Pool)

**Fertig, wenn:** Reales Audiosignal wird bei Threshold-Überschreitung in einen Ringbuffer aufgenommen
und läuft in Loop; Anzahl/On-Off/Round-Robin/Lock funktionieren; Buffer/Hold/Fade In-Out sind echt
angebunden; Play/Pause aus Phase 1 steuert jetzt echtes Audio.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Blöcke 1+2)
und 1.9.

Implementiere die Capture-Engine (z.B. FlarePool) als eigenständiges Modul:

- Ringbuffer pro Flare-Slot (max. 5), Länge = Buffer-Parameter (Block 2 „Time")
- Echte CycleRow-Instanz für Block 1 "Flares" (Anzahl 1-5, Threshold, On/Off) und
  Block 2 "Time" (Buffer, Hold, Fade In/Out), ersetze die Dummy-Zeilen aus Phase 1
  für diese beiden Blöcke
- Threshold-Poti liest Eingangspegel; bei Überschreiten UND On/Off=on wird in die
  älteste, nicht gelockte, aktive (gemäß Anzahl) Flare aufgenommen (Round-Robin,
  Abschnitt 1.9)
- Rec-Taster/Trig-Callback aus Phase 2 löst jetzt echte Aufnahme aus, gleiche
  Round-Robin-Logik
- Hold Time: Flare fadet nach Ablauf automatisch aus (Fade-Out-Zeit aus Block 2)
- Lock (Phase 2) schützt vor Round-Robin-Ersetzung UND vor Hold-Time-Ausklingen
- Play/Pause (Phase 1) crossfaded jetzt echte Wiedergabe aller aktiven, nicht
  gelockten(?) Flares gleichzeitig - klär in einem Kommentar im Code, ob gelockte
  Flares von Play/Pause ausgenommen sein sollen, und triff eine begründete
  Annahme, falls das hier noch nicht eindeutig festgelegt ist
- Reset (Phase 1) löscht alle Flare-Ringbuffer sofort

Achte auf Interrupt-sichere Übergabe zwischen Audio-Callback und UI-Thread wie in
libDaisy üblich.
```

---

### Phase 4 — Resynth-Engine

**Fertig, wenn:** Eine aufgenommene Flare wird additiv resynthetisiert hörbar; Anzahl/Shape-Fold/Chorus/
Pitch Resynth sind einstellbar und hörbar wirksam.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 3+4).

Implementiere die Resynth-Engine (additive Resynthese, analog "Tide" im Coastline-
Vorbild) als eigenständiges DSP-Modul, das auf den Flare-Ringbuffern aus Phase 3
arbeitet:

- Frequenzanalyse (FFT via DaisySP oder eigene Implementierung), Extraktion der
  stärksten Partials
- Additive Resynthese über eine Oszillatorbank
- Echte CycleRow für Block 4 "Resynth Parameter": Anzahl (Partials), Shape/Fold
  (0% = Sinus, morpht Richtung Sägezahn/Wavefolding), Chorus (Stereo-Chorus)
- Pitch Resynth als Teil von Block 3 "Engines" (siehe Phase 6 für den Rest von
  Block 3) - für diese Phase reicht ein direkter Pitch-Parameter für die
  Resynth-Engine

Binde die Resynth-Ausgabe vorerst direkt auf den Audio-Ausgang.
```

---

### Phase 5 — Grain-Engine

**Fertig, wenn:** Dieselbe Flare ist über Grain statt Resynth hörbar; Anzahl/Position/Velocity/Size und
Pitch Grain wirken hörbar.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 3+5).

Implementiere die Grain-Engine (granular, analog "Glade") als eigenständiges Modul:

- Grain-Player mit semi-zufälliger Positionswanderung im Flare-Ringbuffer
- Transienten-/Klick-Reduktion
- Echte CycleRow für Block 5 "Grain Parameter": Anzahl (Grains), Position
  (Wo im Buffer gestartet wird), Velocity (Wandergeschwindigkeit), Size
  (Grain-Länge)
- Pitch Grain als Teil von Block 3 "Engines" (direkter Pitch-Parameter für diese
  Phase, vollständige Block-3-Integration folgt in Phase 6)

Für diese Phase reicht ein einfacher A/B-Wechsel zwischen Resynth und Grain pro
Flare (echtes Blending kommt in Phase 6).
```

---

### Phase 6 — Engine-Blend (Block 3 „Engines")

**Fertig, wenn:** Block 3 "Engines" ist vollständig (Blend, Pitch Resynth, Pitch Grain über eine
gemeinsame CycleRow), Blend crossfaded stufenlos zwischen Resynth und Grain.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 3).

Ersetze den A/B-Wechsel aus Phase 5 durch echtes Crossfading zwischen Resynth- und
Grain-Ausgabe, gesteuert durch eine vollständige CycleRow für Block 3 "Engines":
Blend, Pitch Resynth, Pitch Grain. Konsolidiere die in Phase 4/5 provisorisch
direkt gesetzten Pitch-Parameter jetzt in diese eine CycleRow.
```

---

### Phase 7 — Spectral Resonator

**Fertig, wenn:** Der Grain-Ausgang läuft hörbar durch den Spectral Resonator, Mix/Quantized/Decay/Pitch
wirken.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 7).

Implementiere den Spectral Resonator als DSP-Modul, das ausschließlich auf dem
Grain-Engine-Ausgang arbeitet (nicht auf Resynth oder Input):
- Mix (Anteil des resonierten Signals)
- Quantized (Beschränkung der Resonanzfrequenzen auf musikalische Intervalle/
  Skalen - Umfang der Quantisierungslogik selbst entscheiden und kommentieren)
- Decay (Ausklingzeit der Resonanz)
- Pitch (Grundtonhöhe der Resonanz)

Echte CycleRow für Block 7.
```

---

### Phase 8 — Reverb & Filter Mix

**Fertig, wenn:** Reverb (Mix/Size/Decay) ist hörbar; Filter Mix (Destination/HP/LP) filtert wahlweise
Input/Resynth/Grain/Reverb.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 6+10).

Implementiere:
- Reverb-Modul (DaisySP-Baustein oder Erweiterung) mit CycleRow für Block 6:
  Mix, Size, Decay
- Filter-Modul mit CycleRow für Block 10 "Filter Mix": Destination (wählt, auf
  welches Signal der Filter angewendet wird - Input/Resynth/Grain/Reverb, per
  Cycle durchschaltbar als eigener "Wert" dieses Parameters, nicht als Sub-Liste),
  HP (Highpass-Cutoff), LP (Lowpass-Cutoff)
```

---

### Phase 9 — Pan Drift & Crossfade über 5 Flares

**Fertig, wenn:** Aktive Flares wandern hörbar/sichtbar automatisch im Panorama; das Ein-/Ausblenden
beim Ersetzen einer Flare ist über Amplitude/Velocity einstellbar.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 8+9).

Implementiere:
- Pan-Drift-Modul: automatische, kontinuierliche Panorama-Bewegung für jede aktive
  Flare, CycleRow Block 8: Spread (Breite des Wanderbereichs), Amplitude (Intensität),
  Velocity (Geschwindigkeit). Jede Flare sollte eine unterschiedliche Phasenlage
  der Bewegung bekommen, damit sie nicht synchron wandern (sinnvolle Verteilung
  selbst wählen, z.B. gleichmäßig über 360° verteilt je nach Anzahl aktiver Flares)
- Crossfade-Modul für Block 9: Amplitude/Velocity steuern, wie stark/schnell eine
  Flare beim Ersetzen durch eine neue Aufnahme (Round-Robin, Abschnitt 1.9)
  ein-/ausblendet - Verhältnis zu den Fade-In/Out-Werten aus Block 2 "Time" im
  Code kommentieren (vermutlich: Block 2 = Basis-Fade-Zeit, Block 9 = zusätzliche
  Modulation von Stärke/Geschwindigkeit dieses Übergangs)
```

---

### Phase 10 — Mod-System

**Fertig, wenn:** Alle 4 Mod-Slots wirken auf ihr gewähltes Destination-Ziel; ist eine Mod-CV-Buchse
gepatcht, wird das externe Signal genutzt, sonst eine interne clock-synchrone Quelle mit Rate = Divider.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.4 und Abschnitt 6
(Offene Punkte zum Mod-System).

Implementiere die 4 Mod-Slots:
- Jack-Erkennung pro Mod-CV-Buchse (Standard-Eurorack-Technik, z.B. Schaltkontakt
  im Klinkenstecker oder Spannungspegel-Heuristik - Ansatz im Code kommentieren)
- Ist eine Buchse gepatcht: externes CV-Signal wird gelesen und mit Amplitude
  skaliert
- Ist keine Buchse gepatcht: interne Modulationsquelle generiert (Vorschlag:
  einfaches Dreieck/Sinus, Rate = Clock-Periode * Divider - eigene, einfachere
  Lösung ist ok, im Code kurz begründen)
- CycleRow pro Slot: Amplitude, Destination (Ziel aus der Registry aller
  modulierbaren Parameter - analog dem Macro-Framework-Gedanken, hier aber neu
  für Flaresum aufbauen), Divider
- Destination-Registry: alle Parameter aus Block 1-11 UND Flare-Level (Level pro
  Flare) sollen wählbar sein
```

---

### Phase 11 — Multi-Block & Feinschliff

**Fertig, wenn:** Multi-Encoder steuert Dry/Wet global sowie Macro1/Macro2 (deren Ziele vorerst fest im
Code hinterlegt, siehe Abschnitt 6); Kalibrierung für Threshold und CV-Eingänge ist vorhanden.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 (Block 11)
und Abschnitt 6.

Implementiere:
- CycleRow für den Multi-Encoder (Block 11): Dry/Wet global, Macro1, Macro2 -
  nutze für Macro1/Macro2 vorerst zwei feste, im Code klar kommentierte
  Default-Ziele aus der Destination-Registry (Phase 10), da die endgültige
  Zuweisungs-UI für Macros noch nicht final entschieden ist (siehe Abschnitt 6) -
  baue die Anbindung aber so, dass ein späterer Wechsel zu einer freien
  Zuweisung nur die Zielauswahl betrifft, nicht die CycleRow-Mechanik selbst
- Kalibrierungsroutine (Min/Max-Lernmodus) für Threshold-Poti und alle CV-Eingänge
- Review-Durchgang: alle Display-Texte auf Kürze/Lesbarkeit bei 128x64px prüfen
```

---

## 5. Wichtige Änderungen gegenüber früheren Entwürfen (zur Erinnerung)

- Kein fester Voice-Count mehr — Flares sind 1–5, einstellbar
- Kein "Fokus"-Konzept pro Flare mehr (kein Voice-Taster mit Transposition/Oktave-Gesten) — Flares haben
  nur noch Level/Lock/Solo
- Engines, Filter, Reverb, Pan Drift etc. sind **global**, nicht mehr pro Flare — deutlich weniger
  Bauteile als in früheren Entwürfen
- Transposition/Oktave-Funktionen aus früheren Entwürfen sind in Flaresum **nicht mehr enthalten**
- CV-Modulation läuft jetzt über die generischen Mod-Slots (Block 1.4), nicht mehr über feste
  Per-Flare-CV-Eingänge

## 6. Offene Punkte (bewusst nicht in Phase 0–11 final entschieden)

- **Mod-Slot-Quelle intern/extern:** Jack-Erkennungsmechanismus ist ein Vorschlag, technisch zu
  verifizieren (welche Erkennungsmethode das gewählte Buchsen-Modell tatsächlich unterstützt)
- **Macro1/Macro2-Zielzuweisung:** aktuell fest im Code (Phase 11), noch keine Front-Panel-UI dafür
  entschieden
- **Multi-Encoder Push-Funktion:** ungenutzt/nicht spezifiziert — könnte später für Macro-Zuweisung
  reserviert werden
- **Filter-Destination-Mechanik:** genaues DSP-Routing (paralleles Filtern mehrerer Signalpfade vs.
  exklusive Auswahl) noch nicht im Detail festgelegt
- Sidechain-Input-Konzept aus dem Coastline-Original: nicht Teil dieser Architektur
- Preset-Speicherung/-Verwaltung: über reine Parameter-Persistenz hinaus noch nicht geplant
