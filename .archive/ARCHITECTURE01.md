# Ambient Lush Machine — Architektur & Entwicklungsplan

> Dieses Dokument ist die **einzige Referenzquelle** für die KI-gestützte Entwicklung in Cursor.
> Lege es als `ARCHITECTURE.md` in den Projekt-Root und beginne **jeden** Cursor-Prompt mit:
> „Lies zuerst ARCHITECTURE.md im Projekt-Root, bevor du etwas implementierst.“

Inspiriert vom Plugin **Coastline** (Aqeel Aadam Sound) — Sustainer/Resynthese-Engines Tide (additiv) und Glade (granular) — aber bewusst reduziert und für Hardware/Eurorack neu gedacht.

---

## 0. Zielplattform

- **MCU/Board:** Electrosmith Daisy Seed (STM32H750, 480MHz Cortex-M7)
- **Framework:** libDaisy (Hardware-Abstraktion) + DaisySP (DSP-Bausteine)
- **Sprache:** C++ (PlatformIO oder Daisy-eigenes CMake-Toolchain-Setup)
- **Custom Carrier-PCB** nötig, um alle Pots/Taster/Buchsen/Display auf die Daisy-Seed-Pins zu routen (Standard-DIY-Eurorack-Praxis)

Falls eine andere Plattform gewünscht ist: Diesen Abschnitt anpassen, der Rest des Dokuments bleibt gültig (Architektur ist plattformunabhängig beschrieben).

---

## 1. Vollständige Hardware-Architektur

### 1.1 Globale Bedienelemente

| Element | Typ | Funktion |
|---|---|---|
| Encoder | Push/Turn | Turn = navigieren/Wert ändern; Push = bestätigen/reingehen |
| Return | Taster | Eine Ebene zurück, **ohne** Wert zu setzen |
| CAPTURE | Taster | Fokus auf Capture-Block. **Doppelklick** = manueller Rec-Trigger auf die aktuell fokussierte Voice (kein Untermenü-Toggle) |
| ENGINE | Taster | Fokus auf Engine-Block. 2. Klick = Untermenü Tide ↔ Glade |
| FX | Taster | Fokus auf FX-Block. 2. Klick = Untermenü Matrix ↔ Reverb |
| MACRO | Taster | Fokus auf globalen Macro-Screen (z.B. Dry/Wet, frei zuweisbar) |
| Threshold-Poti | Poti (global) | Direkter Hardware-Zugriff auf Capture-Threshold, kein Menü nötig |
| Global-FX-Poti | Poti (global) | Wirkt gleichzeitig auf alle 4 Voices (z.B. globaler Send/Mix) |
| Display | SSD1309 OLED, 128×64px, 2.42″ | Zeigt Menüs, Home-Dashboard, V1–V4-Liste links |

### 1.2 Pro Voice (×4, fix — keine wählbare Anzahl)

| Element | Typ | Funktion |
|---|---|---|
| Voice-Taster | Taster | Siehe Gesten-Tabelle unten |
| Level | Poti | Lautstärke der Voice |
| Mix Tide/Glade | Poti | Balance der Resynthese-Engines **für diese Voice** (unabhängig vom globalen Engine-Blend) |
| FX1 (Macro A) | Poti | Frei zuweisbar im Menü. **Default: Tide Harmonic Balance** |
| FX2 (Macro B) | Poti | Frei zuweisbar im Menü |
| Mod-Attenuator | Poti | Attenuverter für den CV-Eingang dieser Voice |
| CV-In (Buchse) | Jack | Modulationseingang, Ziel im Menü konfigurierbar |

### 1.3 I/O-Buchsen (11 gesamt)

Clock In · Mono In L · Mono In R · Mono Out L · Mono Out R · CV In V1 · CV In V2 · CV In V3 · CV In V4 · Capture-Trigger CV · Engine-Blend CV

**CV-Spread-Modus (umschaltbar, global):** Wird nur V1-CV-In gepatcht, verteilt das Modul das Signal automatisch mit fester Zeitverzögerung auf V2–V4 (V2 = t, V3 = 2t, V4 = 3t). Liegt Clock an, wird t als Clock-Subdivision definiert (z.B. 1/16), sonst als freie ms-Zeit.

### 1.4 Gesten-Tabelle Voice-Taster

| Geste | Aktion |
|---|---|
| Kurz drücken | Fokus **Transposition**: Turn wählt OFF / Wert1 / Wert2 / Wert3 (mit sanftem Glide, falls Voice aktiv klingt) |
| Lang drücken (Schwelle konfigurierbar, Start: 600ms) | Fokus **Oktave**: Turn verschiebt Oktave rauf/runter |
| Doppelklick | **Play/Pause** (Crossfade, Dauer = Fade-In/Fade-Out-Zeit aus Capture-Einstellungen) |
| Fokus halten + Return gedrückt halten | **Clear**: Sustainer dieser Voice sofort löschen |

### 1.5 Capture-Modell (Sustainer-Pool, analog Coastline)

- Globaler Threshold triggert automatisch eine neue Aufnahme in die **älteste, nicht gelockte** Voice (Round-Robin-Ersetzung, wie im Original)
- Manueller Trigger (CAPTURE-Taster Doppelklick) nimmt **in die aktuell fokussierte Voice** auf
- **Hold Time** (global, Menüparameter im CAPTURE-Screen): Zeit bis automatisches Ausklingen; 0/∞ möglich
- **Lock** (pro Voice, Menüpunkt im CAPTURE-Screen): schützt vor Ersetzung und Hold-Time-Ausklingen — ideal für Dauer-Drone
- **Solo** (pro Voice, Menüpunkt im CAPTURE-Screen): nur zum Anhören/Debuggen

### 1.6 Navigations-Zustandsmaschine (gilt für alle Menü-Taster)

```
Home/Übersicht → [Taster kurz] → Block-Fokus (Parameterliste)
Block-Fokus → [Turn] → Parameter wählen
Block-Fokus → [Push] → Wert-Edit → [Turn] → Wert ändern → [Push] → zurück zur Parameterliste
Jede Ebene → [Return] → eine Ebene zurück, kein Wert gesetzt
```

### 1.7 Engine-Parameter (Menü, nicht auf Pots — außer Mix Tide/Glade pro Voice)

- **Tide:** Partials, Wave Shaping, Chorus, Modus (Ebb/Flow), Ebb: Offset + Harmonics, Flow: Chroma + Shift, Pitch, **Harmonic Balance** (= Standardziel für FX1)
- **Glade:** Size, Spread, Blur, Churn, Erode, Pitch
- **Global Engine-Blend:** Tide↔Glade, per CV modulierbar (Engine-Blend-CV-Jack)

### 1.8 FX-Parameter (Menü)

- **Matrix (vereinfacht, Presets statt freier NxN-Matrix):** 3–5 feste Routing-Presets (z.B. „Tide→Input Subtle“, „Glade→Tide Distortion“)
- **Reverb:** Size/Decay, Mix, Character (Chorus↔LoFi↔Chaos-Blend)

---

## 2. Entwicklungsprinzipien

1. **Von unten nach oben, aber UI zuerst testen:** Erst die komplette Bedienlogik mit Dummy-Parametern (keine echte DSP) validieren, danach Block für Block echte Audio-Verarbeitung einbauen.
2. **Jede Phase = ein Cursor-Prompt = ein Git-Commit.** Nichts zusammenfassen, auch wenn es verlockend ist.
3. **Nach jeder Phase ein konkretes, hörbares/sichtbares Testkriterium** (siehe „Fertig, wenn“ je Phase) — kein „fühlt sich fertig an“.
4. **Stubs vor echter DSP:** z.B. Capture-Engine erst als simples Loop-Record/Playback (Ringbuffer), Tide/Glade-Resynthese kommt später oben drauf.
5. **ARCHITECTURE.md ist Quelle der Wahrheit.** Wenn sich während der Umsetzung etwas ändert, dieses Dokument aktualisieren, bevor der nächste Prompt geschrieben wird.

---

## 3. Phasen-Roadmap

| Phase | Inhalt |
|---|---|
| 0 | Projekt-Setup, Blink-Test |
| 1 | UI-Skelett: Encoder/Taster/Display, Navigations-Zustandsmaschine, Dummy-Parameter |
| 2 | Voice-Gesten (Transposition/Oktave/Play-Pause/Clear-Fokus) auf Dummy-Werten |
| 3 | Capture-Engine (Ringbuffer-Loop, Threshold, Hold Time, Lock/Solo, manueller Trigger) |
| 4 | Tide-Engine (additive Resynthese, inkl. Harmonic Balance) |
| 5 | Glade-Engine (granular) |
| 6 | Mix Tide/Glade pro Voice + globaler Engine-Blend (+ CV) |
| 7 | FX1/FX2-Zuweisungssystem (Macro-Framework), Default FX1 = Tide Harmonic Balance |
| 8 | Reverb + Matrix-Presets |
| 9 | CV-Eingänge pro Voice, Attenuverter, Capture-Trigger-CV, Clock + Spread-Modus |
| 10 | Globaler Macro-Screen, Threshold/Global-FX-Potis, Feinschliff & Kalibrierung |

---

## 4. Phasen im Detail

### Phase 0 — Projekt-Setup

**Fertig, wenn:** Projekt compiliert und flasht, eine LED blinkt im definierten Rhythmus.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root.

Richte ein neues libDaisy/DaisySP-Firmware-Projekt für Electrosmith Daisy Seed ein
(PlatformIO-Setup, Standard-Ordnerstruktur: src/, lib/, ARCHITECTURE.md bleibt im Root).
Erstelle main.cpp mit einer einfachen Blink-Test-Routine auf der Onboard-LED,
um den Build- und Flash-Workflow zu verifizieren. Kein Audio-Code in dieser Phase.
Kommentiere im Code, welche Phase (laut ARCHITECTURE.md Abschnitt 3) das ist.
```

---

### Phase 1 — UI-Skelett & Navigations-Zustandsmaschine

**Fertig, wenn:** Am Display sind Home-Screen, Block-Fokus und Wert-Edit sichtbar und per echtem Encoder/Taster durchklickbar — mit reinen Platzhalter-Werten (keine echte DSP dahinter).

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.1, 1.4 und 1.6.

Implementiere das UI-Grundgerüst:
- Treiber-Setup für SSD1309 128x64 OLED über libDaisy/U8g2-kompatible Anbindung
- Encoder-Handling (Turn/Push) und Return-Taster gemäß der Navigations-Zustandsmaschine
  aus Abschnitt 1.6 (Home -> Block-Fokus -> Wert-Edit -> Return)
- Die 4 Menü-Taster (CAPTURE/ENGINE/FX/MACRO) lösen Fokuswechsel aus, noch OHNE echte
  Parameterlisten - nutze für jeden Block eine Dummy-Parameterliste mit 3 Platzhalter-
  Werten (float, Name als String), um die Navigation zu testen
- Home-Screen zeigt ein simples Dashboard mit den 4 Blocknamen + Platzhalter für V1-V4
- Noch keine Audio-Verarbeitung, kein Voice-Taster-Handling (kommt in Phase 2)

Strukturiere den Code so, dass die Navigations-Zustandsmaschine als eigene Klasse/Modul
existiert (z.B. UiStateMachine), unabhängig von den späteren echten Parametern -
spätere Phasen sollen nur noch echte Parameter-Structs einhängen müssen, nicht die
Zustandsmaschine selbst ändern.
```

---

### Phase 2 — Voice-Gesten

**Fertig, wenn:** Die 4 Voice-Taster lösen im Display sichtbar die korrekten Fokus-Modi aus (Transposition/Oktave/Play-Pause/Clear) — weiterhin mit Dummy-Werten, kein echtes Audio.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 und 1.4.

Erweitere die UiStateMachine aus Phase 1 um die 4 Voice-Taster (V1-V4) mit exakt
diesen Gesten (siehe Abschnitt 1.4):
- Kurz drücken: Fokus Transposition dieser Voice, Turn wählt zyklisch
  OFF / Wert1 / Wert2 / Wert3 (erstmal als Dummy-Enum, echte Pitch-Logik folgt später)
- Lang drücken (konfigurierbare Schwelle, Startwert 600ms): Fokus Oktave, Turn
  verändert einen Dummy-Oktave-Int-Wert (-2..+2)
- Doppelklick (Zeitfenster konfigurierbar, Startwert 300ms): togglet einen
  Dummy-Play/Pause-Zustand pro Voice
- Fokus halten + Return gedrückt halten (Schwelle konfigurierbar): löst einen
  Dummy-"Clear"-Callback für diese Voice aus

Implementiere die Geste-Erkennung (kurz/lang/doppelklick/kombiniert-mit-Return)
als generisches, wiederverwendbares Modul (z.B. ButtonGesture-Klasse), nicht als
Spezialfall nur für Voice-Taster - das CAPTURE-Taster-Doppelklick aus Abschnitt 1.1
soll später dieselbe Klasse nutzen können.

Zeige den aktuellen Fokus-Zustand jeder Voice zusätzlich im Display an
(z.B. kleine V1-V4-Liste am linken Rand, wie in Abschnitt 1.1 Display-Zeile
beschrieben).
```

---

### Phase 3 — Capture-Engine

**Fertig, wenn:** Ein Audiosignal am Mono-In wird bei Überschreiten des Threshold-Potis in einen Ringbuffer aufgenommen und läuft in Loop ab; Hold Time, Lock, Solo und manueller Trigger funktionieren; Play/Pause aus Phase 2 steuert jetzt echtes Audio statt Dummy-Zustand.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.5.

Implementiere die Capture-Engine als eigenständiges Modul (z.B. CaptureEngine),
zunächst OHNE Tide/Glade-Resynthese - nur simples Loop-Record/Playback pro Voice:

- Ringbuffer pro Voice (Länge konfigurierbar, Startwert 2 Sekunden bei Audio-Samplerate
  der Daisy Seed)
- Threshold-Poti (Abschnitt 1.1) liest den Eingangspegel; bei Überschreiten wird in die
  älteste, nicht gelockte Voice aufgenommen (Round-Robin gemäß Abschnitt 1.5)
- Hold Time als globaler Parameter im CAPTURE-Menü (Fokus über CAPTURE-Taster,
  siehe Phase 1 Navigations-Zustandsmaschine) - nach Ablauf faded die Voice automatisch aus
- Lock und Solo als Menüpunkte pro Voice im CAPTURE-Screen
- Manueller Trigger: CAPTURE-Taster-Doppelklick (nutze die ButtonGesture-Klasse aus
  Phase 2) nimmt in die aktuell fokussierte Voice auf, unabhängig vom Threshold
- Verknüpfe den Play/Pause-Dummy-Zustand aus Phase 2 jetzt mit echtem Audio:
  Play/Pause crossfaded die Wiedergabe dieser Voice (Fade-Zeit als Parameter,
  Startwert 200ms - noch nicht an Tide/Glade-Fade-Settings gekoppelt, das folgt später)
- Clear-Callback aus Phase 2 leert jetzt den echten Ringbuffer der Voice

Achte auf saubere Interrupt-sichere Übergabe zwischen Audio-Callback (Ringbuffer-
Schreiben/Lesen) und dem UI-Thread (Parameter-Änderungen), wie in libDaisy üblich.
```

---

### Phase 4 — Tide-Engine (additive Resynthese)

**Fertig, wenn:** Eine aufgenommene Voice wird additiv resynthetisiert hörbar (Sinus-Partials), Partials/Wave-Shaping/Chorus/Modus/Pitch/Harmonic-Balance sind im ENGINE-Menü einstellbar und hörbar wirksam.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.7.

Implementiere die Tide-Engine als eigenständiges DSP-Modul (z.B. TideEngine),
das auf dem Ringbuffer-Inhalt einer Voice aus Phase 3 arbeitet:

- Frequenzanalyse des aufgenommenen Loops (z.B. FFT via DaisySP oder eigene
  Implementierung), Extraktion der stärksten Partials
- Additive Resynthese über eine Oszillatorbank (Sinus als Default)
- Parameter im ENGINE-Menü (Fokus über ENGINE-Taster, 2. Klick wechselt
  perspektivisch zu Glade - das folgt in Phase 5, für jetzt reicht die
  Tide-Unteransicht):
  - Partials (Anzahl der Oszillatoren, nach Signifikanz sortiert)
  - Wave Shaping (0% = Sinus, morpht Richtung Sägezahn und Wavefolding)
  - Chorus (einfacher Stereo-Chorus auf den Tide-Ausgang)
  - Modus Ebb/Flow als Auswahl:
    - Ebb: Offset (verschiebt welche Partials gehört werden) + Harmonics
      (harmonisch/inharmonisch-Balance)
    - Flow: Chroma (Formant-artige Balance) + Shift (relativ zur Grundfrequenz
      vs. absolute Frequenzen)
  - Pitch (Transposition des gesamten Tide-Ausgangs)
  - **Harmonic Balance**: fasse Chroma (Flow) bzw. Harmonics (Ebb) als einen
    gemeinsam benannten, modusabhängigen Zielparameter zusammen - DIES ist der
    Parameter, der ab Phase 7 als Default auf FX1 liegt. Exponiere ihn daher
    bereits jetzt über eine stabile, öffentliche Schnittstelle (z.B.
    TideEngine::SetHarmonicBalance(float)), unabhängig vom aktuell aktiven
    Ebb/Flow-Modus.

Binde die Tide-Ausgabe vorerst direkt auf den Audio-Ausgang der jeweiligen Voice.
```

---

### Phase 5 — Glade-Engine (granular)

**Fertig, wenn:** Dieselbe Voice ist wahlweise über Glade statt Tide hörbar, alle 5 Glade-Parameter wirken hörbar.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.7.

Implementiere die Glade-Engine (z.B. GladeEngine) als granularen Synthesizer,
der ebenfalls auf dem Ringbuffer-Inhalt einer Voice aus Phase 3 arbeitet:

- Grain-Player mit semi-zufälliger Positionswanderung im Loop
- Transienten-/Klick-Reduktion beim Grain-Start/-Ende
- Parameter im ENGINE-Menü, Glade-Unteransicht (2. Klick auf ENGINE-Taster
  wechselt zwischen Tide- und Glade-Unteransicht - implementiere diesen
  Umschalt-Mechanismus jetzt, da beide Engines nun existieren):
  - Size (Grain-Länge)
  - Spread (Stereo-Zufallsstreuung der Grains)
  - Blur (Lautstärke-Konturglättung pro Grain)
  - Churn (Wandergeschwindigkeit der Grain-Position im Loop)
  - Erode (Sample-Rate-Reduktion der Wiedergabe)
  - Pitch (Transposition des gesamten Glade-Ausgangs)

Binde die Glade-Ausgabe ebenfalls auf den Audio-Ausgang der Voice - für diese
Phase reicht ein einfacher A/B-Wechsel zwischen Tide und Glade pro Voice als
Zwischenschritt (das echte Blending kommt in Phase 6).
```

---

### Phase 6 — Mix Tide/Glade & globaler Engine-Blend

**Fertig, wenn:** Das "Mix Tide/Glade"-Poti pro Voice blendet stufenlos zwischen beiden Engines; ein globaler Blend-Parameter wirkt zusätzlich auf alle Voices, per CV modulierbar.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2, 1.3 und 1.7.

Ersetze den A/B-Wechsel aus Phase 5 durch echtes Crossfading zwischen
TideEngine- und GladeEngine-Ausgabe pro Voice:

- "Mix Tide/Glade"-Poti pro Voice (Abschnitt 1.2) steuert das lokale Blend-
  Verhältnis dieser Voice
- Zusätzlich ein globaler Engine-Blend-Parameter (Menüparameter im ENGINE-Screen),
  der multiplikativ/additiv mit dem lokalen Mix-Poti zusammenwirkt (definiere die
  genaue Verrechnung nachvollziehbar in einem Kommentar, z.B. global als Offset
  auf den lokalen Wert)
- Lies den Engine-Blend-CV-Eingang (Abschnitt 1.3) noch als Rohwert ein (0-5V o.ä.)
  und moduliere den globalen Blend-Parameter damit - die eigentliche CV-Kalibrierung
  und Attenuverter-Anbindung folgt strukturiert erst in Phase 9, hier reicht ein
  einfacher linearer Mapping-Test
```

---

### Phase 7 — FX1/FX2-Macro-Framework

**Fertig, wenn:** FX1 und FX2 pro Voice sind im Menü frei auf beliebige Engine-/Capture-Parameter dieser Voice zuweisbar; beim ersten Boot ist FX1 auf allen Voices per Default auf Tide Harmonic Balance gesetzt.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 und
Abschnitt 1.7 (Harmonic Balance).

Implementiere ein generisches Macro-Zuweisungs-Framework:

- Eine zentrale Registry aller modulierbaren Ziel-Parameter pro Voice (Tide-,
  Glade- und Capture-Parameter aus den vorherigen Phasen), jedes Ziel mit
  stabilem Namen/ID, Wertebereich und Setter-Funktion
- FX1 (Macro A) und FX2 (Macro B) pro Voice sind Potis, deren Zielparameter im
  Menü (Fokus über FX-Taster oder direkt im Voice-Kontext, je nachdem was sich
  beim Testen intuitiver anfühlt - Vorschlag machen und kurz begründen) aus der
  Registry gewählt werden kann
- **Default-Zuweisung beim ersten Boot / Werksreset:** FX1 = Tide Harmonic
  Balance, für alle 4 Voices. FX2 hat noch keinen sinnvollen Default - wähle
  einen (z.B. Glade Churn) und kommentiere das explizit als vorläufig
- Persistiere die Zuweisung im Flash (QSPI der Daisy Seed), damit sie nach
  Neustart erhalten bleibt, aber auf einen Werksreset zurücksetzbar ist
```

---

### Phase 8 — Reverb & Matrix-Presets

**Fertig, wenn:** Reverb (Size/Decay, Mix, Character) ist über den FX-Taster hörbar einstellbar; mindestens 3 Matrix-Presets sind wählbar und hörbar unterschiedlich.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.8.

Implementiere:
- Ein Reverb-Modul (DaisySP bietet Bausteine dafür, ggf. erweitern) mit Size/Decay,
  Mix und einem "Character"-Parameter, der zwischen Chorus/LoFi/Chaos-Modulation
  der Reverb-Tail blendet (siehe Coastline-Vorbild in Abschnitt 1.8 - kurze,
  langsame Modulation für Chorus, Downsampling für LoFi, Zufallsschwankung für
  Chaos)
- 3-5 feste Matrix-Presets als einfache, hartcodierte Routing-Konfigurationen
  (z.B. Preset 1 = "Tide moduliert Input leicht", Preset 2 = "Glade moduliert
  Tide stark" usw.) - keine freie NxN-Matrix, wie in Abschnitt 1.8 festgelegt
- FX-Taster (2. Klick) wechselt zwischen Matrix-Presets-Auswahl und Reverb-Menü
```

---

### Phase 9 — CV-Eingänge, Attenuverter, Clock & Spread-Modus

**Fertig, wenn:** Jede Voice reagiert auf ihren CV-Eingang (skaliert durch den Mod-Attenuator), Capture-Trigger-CV löst manuelle Aufnahmen aus, und der Spread-Modus verteilt ein einzelnes CV-Signal zeitversetzt auf V2-V4 (clock-synchron, wenn Clock anliegt).

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.2 und 1.3.

Implementiere:
- Kalibrierte CV-Eingangslesung für alle 4 Voice-CV-Jacks, skaliert über das
  jeweilige Mod-Attenuator-Poti dieser Voice, geroutet auf das im Macro-Framework
  (Phase 7) konfigurierte Ziel dieser Voice
- Capture-Trigger-CV-Jack löst denselben manuellen Aufnahme-Trigger aus wie der
  CAPTURE-Taster-Doppelklick (Phase 3) - jeder CV-Puls über einer Schwelle triggert
  eine Aufnahme in die aktuell fokussierte Voice
- Clock-Eingang: Erkennung von Clock-Pulsen, Berechnung der aktuellen
  Clock-Periode
- Spread-Modus (globaler Umschalter im Settings- oder MACRO-Menü): wenn aktiv,
  wird NUR der V1-CV-Eingang gelesen; V2/V3/V4 erhalten dasselbe Signal über
  einen Delay-Puffer, verzögert um 1x/2x/3x einer Basis-Verzögerung. Ist Clock
  vorhanden, ist die Basis-Verzögerung eine konfigurierbare Clock-Subdivision
  (Default 1/16); ohne Clock eine freie ms-Zeit (Default 50ms)
```

---

### Phase 10 — Globaler Macro-Screen & Feinschliff

**Fertig, wenn:** MACRO-Taster öffnet einen Screen mit einem frei zuweisbaren globalen Parameter (Default: Dry/Wet), Threshold- und Global-FX-Potis sind sauber kalibriert, alle Menüs haben sinnvolle Beschriftungen.

```
Prompt für Cursor:

Lies zuerst ARCHITECTURE.md im Projekt-Root, insbesondere Abschnitt 1.1.

Implementiere:
- MACRO-Taster öffnet einen Screen mit einem global zuweisbaren Parameter
  (nutze die Registry-Idee aus Phase 7, erweitert um globale statt
  Voice-spezifische Ziele), Default-Zuweisung: globaler Dry/Wet-Mix
- Global-FX-Poti (Abschnitt 1.1) ist mit demselben Zuweisungsmechanismus
  verbunden und wirkt gleichzeitig auf alle 4 Voices
- Kalibrierungsroutine für Threshold-Poti und alle CV-Eingänge (einfacher
  Min/Max-Lernmodus, im Settings-Menü aufrufbar)
- Review-Durchgang: alle Menü-Texte im Display auf Kürze/Lesbarkeit bei
  128x64px prüfen und ggf. abkürzen (siehe frühere Panel-Diskussion:
  4-5-stellige Kürzel wo nötig)
```

---

## 5. Offene Punkte für spätere Iterationen (bewusst nicht in Phase 0-10)

- Feinabstimmung der genauen Matrix-Preset-Klangcharaktere (erst hörend entscheiden)
- Sidechain-Input-Konzept aus dem Coastline-Original (aktuell nicht Teil der Hardware-Architektur)
- Pitch-Quantisierung / Just-Intonation-Optionen aus dem Original (aktuell nicht übernommen)
- Preset-Speicherung/-Verwaltung für das gesamte Modul (über reine Parameter-Persistenz aus Phase 7/10 hinaus)
