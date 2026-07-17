# Perseids — KiCad-Schaltplan (Bestehende Hardware + Phase 3)

Referenz für den Schaltplan-Aufbau in KiCad 10. Zwei Abschnitte: was bereits existiert
(Phase 1–2, digitale Steuerungs-Hardware) und was für Phase 3 neu dazukommt (analoge
Audio-I/O-Konditionierung). Pin-Namen entsprechen `ARCHITECTURE.md`, Abschnitt 4.5a.

---

## Teil 1 — Bestehende Steuerungs-Hardware (Phase 1–2)

### 1.1 Stückliste

| Ref | Anzahl | Bauteil | Anmerkungen |
|---|---|---|---|
| U2, U3 | 2 | CD74HC4067 | 16-Kanal-Analog-Multiplexer — **U2 = Mux A, U3 = Mux B** |
| ENC1–ENC5 | 5 | EC11-Drehencoder mit Push | Trail-Level ×5 |
| SW1 | 1 | Taster | Cycle-Taster |
| SW2 | 1 | Taster | Rec-Taster |
| J1 | 1 | 3,5-mm-Mono-Klinke | Trig-Eingang |
| POT1–POT4 | 4 (von 10 geplant) | Poti, Wert offen (typ. 10kΩ linear) | Block-Potis — **aktuell nur 4 auf dem Breadboard verbaut** (2 an Mux A/U2, 2 an Mux B/U3), Gesamtdesign sieht 10 vor; Wert noch **unbestätigt**. **Für Phase 3 werden nur 2 der 4 tatsächlich gebraucht** — Block-1-Poti und Block-2-Poti (siehe 4.6: ein Poti pro Block, nicht pro Unterparameter). POT3/POT4 bleiben vorerst ungenutzt. |
| — | — | *Keine externen Pull-up-Widerstände* | STM32-interne Pull-ups im Code bestätigt (`GPIO::Pull::PULLUP`) — keine externen 10k-Widerstände auf CLK/DT/SW/Taster-Leitungen ergänzen, außer du willst bewusst zusätzliche Sicherheitsmarge |

### 1.2 Netz-/Verbindungstabelle

| Signal | Treiber (Ausgang) | Empfänger (Eingang) | Richtung |
|---|---|---|---|
| Mux-Select S0 | Daisy Seed D0 | U2 Pin S0 + U3 Pin S0 (verbunden) | Daisy → Mux |
| Mux-Select S1 | Daisy Seed D1 | U2 Pin S1 + U3 Pin S1 (verbunden) | Daisy → Mux |
| Mux-Select S2 | Daisy Seed D2 | U2 Pin S2 + U3 Pin S2 (verbunden) | Daisy → Mux |
| Mux-Select S3 | Daisy Seed D3 | U2 Pin S3 + U3 Pin S3 (verbunden) | Daisy → Mux |
| Mux-A-Ausgang | U2 Common (SIG) | Daisy Seed D15 (A0) | Mux → Daisy |
| Mux-B-Ausgang | U3 Common (SIG) | Daisy Seed D16 (A1) | Mux → Daisy |
| Cycle-Taster | SW1 (Schaltkontakt) | Daisy Seed D5 | Taster → Daisy |
| Rec-Taster | SW2 (Schaltkontakt) | Daisy Seed D12 | Taster → Daisy |
| Trig-Eingang | J1 (externes Gerät) | Daisy Seed D13 | Extern → Daisy |
| Trail-1-Encoder CLK | ENC1 CLK (Schaltkontakt) | Daisy Seed D4 | Encoder → Daisy |
| Trail-1-Encoder DT | ENC1 DT (Schaltkontakt) | Daisy Seed D21 | Encoder → Daisy |
| Trail-1-Push | ENC1 SW (Schaltkontakt) | Daisy Seed D14 | Encoder → Daisy |
| Trail-2-Encoder CLK | ENC2 CLK | Daisy Seed D22 | Encoder → Daisy |
| Trail-2-Encoder DT | ENC2 DT | Daisy Seed D23 | Encoder → Daisy |
| Trail-2-Push | ENC2 SW | Daisy Seed D17 | Encoder → Daisy |
| Trail-3-Encoder CLK | ENC3 CLK | Daisy Seed D24 | Encoder → Daisy |
| Trail-3-Encoder DT | ENC3 DT | Daisy Seed D25 | Encoder → Daisy |
| Trail-3-Push | ENC3 SW | Daisy Seed D18 | Encoder → Daisy |
| Trail-4-Encoder CLK | ENC4 CLK | Daisy Seed D26 | Encoder → Daisy |
| Trail-4-Encoder DT | ENC4 DT | Daisy Seed D27 | Encoder → Daisy |
| Trail-4-Push | ENC4 SW | Daisy Seed D19 | Encoder → Daisy |
| Trail-5-Encoder CLK | ENC5 CLK | Daisy Seed D28 | Encoder → Daisy |
| Trail-5-Encoder DT | ENC5 DT | Daisy Seed D29 | Encoder → Daisy |
| Trail-5-Push | ENC5 SW | Daisy Seed D20 | Encoder → Daisy |
| OLED CS | Daisy Seed D7 | Display-Modul | Daisy → OLED |
| OLED SCK | Daisy Seed D8 | Display-Modul | Daisy → OLED |
| OLED DC | Daisy Seed D9 | Display-Modul | Daisy → OLED |
| OLED MOSI | Daisy Seed D10 | Display-Modul | Daisy → OLED |
| OLED RST | Daisy Seed D11 | Display-Modul | Daisy → OLED |
| 4× Block-Poti-Schleifer (aktuell verbaut) | POT1–POT4 (Schleifer) | Mux A/U2 Kanal 0–1 + Mux B/U3 Kanal 0–1 (je 2 pro Mux) | Poti → Mux |
| — Phase-3-Zuordnung | POT1 → Block 1 (Count/Threshold/Cont.Rec/On-Off); POT2 → Block 2 (Buffer/Hold/Fade In/Fade Out) | POT3, POT4 diese Phase ungenutzt | — |

**Freie Pins (noch nicht belegt):** D6, D30, D31, D32.

**Aktueller Breadboard-Stand (Stand dieser Session):** Nur 4 der 10 geplanten Block-Potis sind
verbaut — 2 an Mux A/U2 (Kanal 0–1), 2 an Mux B/U3 (Kanal 0–1). Der Fokus liegt aktuell auf
Phase 3 (Capture Engine) — die Mod-Slot-Potis (Phase 10) werden noch nicht gebraucht und sind
bewusst aus diesem Plan ausgeklammert; die kommen wieder rein, sobald diese Phase tatsächlich
ansteht.

**Anmerkung zu den Mux-SIG-Pins:** Der CD74HC4067 selbst ist als Bauteil physisch ein
bidirektionaler Analog-Schalter (SIG kann von beiden Seiten getrieben werden), wird in diesem
Design aber nur einseitig genutzt — Poti-Schleifer rein, ADC-Pin raus. Es wird nie ein Signal
zurückgeschrieben. **In diesem gesamten Steuerungs-Hardware-Abschnitt ist nichts
bidirektional** — jede Leitung hier ist ein Einwegsignal, entweder "Daisy treibt" (Select-
Leitungen, OLED-SPI) oder "externes Bauteil treibt, Daisy liest" (Potis über Mux, Encoder,
Taster, Trig).

---

## Teil 2 — Phase 3: Audio-I/O-Pegelanpassung (neu)

### 2.1 Stückliste

| Ref | Anzahl | Bauteil | Anmerkungen |
|---|---|---|---|
| U1 | 1 | TL074 | Quad-JFET-Op-Amp — alle 4 Sektionen genutzt (In L, In R, Out L, Out R) |
| J2, J3 | 2 | 3,5-mm-Mono-Klinke | Audio In L, Audio In R |
| J4, J5 | 2 | 3,5-mm-Mono-Klinke | Audio Out L, Audio Out R |
| R1, R2 | 2 | 100kΩ | Eingangsdämpfung, In L / In R |
| R3, R4 | 2 | 33kΩ | Eingangsdämpfung Feedback, In L / In R |
| R5, R6 | 2 | 10kΩ | Ausgangsverstärkung Eingang, Out L / Out R |
| R7, R8 | 2 | 33kΩ | Ausgangsverstärkung Feedback, Out L / Out R |
| R9, R10 | 2 | 1kΩ | Ausgangs-Schutzwiderstand, Out L / Out R |
| C1, C2 | 2 | 10µF Elko | DC-Block, In L / In R |
| C3, C4 | 2 | 10µF Elko | DC-Block, Out L / Out R |
| C5, C6 | 2 | 100nF Keramik | Versorgungs-Entkopplung, V+ / V− nahe U1 |

### 2.2 Netz-/Verbindungstabelle

| Signal | Treiber (Ausgang) | Empfänger (Eingang) | Richtung |
|---|---|---|---|
| Audio In L (extern) | J2 (Eurorack-Gerät) | C1 → R1 → U1 Pin 2 (−In A) | Extern → Op-Amp |
| Audio In L (konditioniert) | U1 Pin 1 (Out A) | Daisy Seed Audio In L (Codec) | Op-Amp → Daisy |
| — Feedback (Stufe In L) | U1 Pin 1 (Out A) → R3 (33k) → U1 Pin 2 | intern in der Op-Amp-Stufe | entfällt (Feedback-Schleife) |
| — Bias (Stufe In L) | GND | U1 Pin 3 (+In A) | GND → Op-Amp |
| Audio In R (extern) | J3 (Eurorack-Gerät) | (10µF) → (100k) → U1 Pin 6 (−In B) | Extern → Op-Amp |
| Audio In R (konditioniert) | U1 Pin 7 (Out B) | Daisy Seed Audio In R (Codec) | Op-Amp → Daisy |
| — Feedback (Stufe In R) | U1 Pin 7 → (33k) → U1 Pin 6 | intern in der Op-Amp-Stufe | entfällt (Feedback-Schleife) |
| — Bias (Stufe In R) | GND | U1 Pin 5 (+In B) | GND → Op-Amp |
| Daisy Audio Out L | Daisy Seed Audio Out L (Codec) | R5 (10k) → U1 Pin 9 (−In C) | Daisy → Op-Amp |
| Audio Out L (extern) | U1 Pin 8 (Out C) → R9 (1k) → C3 (10µF) | J4 → Eurorack-Gerät | Op-Amp → extern |
| — Feedback (Stufe Out L) | U1 Pin 8 → R7 (33k) → U1 Pin 9 | intern in der Op-Amp-Stufe | entfällt (Feedback-Schleife) |
| — Bias (Stufe Out L) | GND | U1 Pin 10 (+In C) | GND → Op-Amp |
| Daisy Audio Out R | Daisy Seed Audio Out R (Codec) | (10k) → U1 Pin 13 (−In D) | Daisy → Op-Amp |
| Audio Out R (extern) | U1 Pin 14 (Out D) → (1k) → (10µF) | J5 → Eurorack-Gerät | Op-Amp → extern |
| — Feedback (Stufe Out R) | U1 Pin 14 → (33k) → U1 Pin 13 | intern in der Op-Amp-Stufe | entfällt (Feedback-Schleife) |
| — Bias (Stufe Out R) | GND | U1 Pin 12 (+In D) | GND → Op-Amp |
| Versorgung V+ | Eurorack +12V-Schiene | U1 Pin 4, entkoppelt mit C5 (100nF) gegen GND | Schiene → IC (Versorgungseingang) |
| Versorgung V− | Eurorack −12V-Schiene | U1 Pin 11, entkoppelt mit C6 (100nF) gegen GND | Schiene → IC (Versorgungseingang) |

**Anmerkung zur Versorgung:** Das ist die erste Baugruppe im Projekt, die die ±12V-Eurorack-
Schienen braucht, nicht nur die 3,3V/5V des Daisy Seed selbst — vor dem Bestücken dieser Stufe
mit einplanen.

**Anmerkung zur Richtung:** Auch in diesem Abschnitt ist jedes Signal strikt einseitig — Audio
läuft durch jede Op-Amp-Stufe nur in eine Richtung (rein→raus), nie zurück. Die "Feedback"-
Zeilen sind kein Signalfluss im I/O-Sinn, sondern nur die Widerstandsschleife, die die
Verstärkung des Op-Amps festlegt — diese Schleife ist intern in jeder Stufe, kein separates
externes Signal. **Auch hier ist nichts bidirektional.** Die vier Op-Amp-Stufen sind komplett
unabhängig voneinander (In L, In R, Out L, Out R nutzen je eine eigene Sektion des
gemeinsamen TL074, nur die ±12V-Versorgungspins werden tatsächlich von allen vieren geteilt).

---

## Offene Punkte vor dem finalen KiCad-Schaltplan

1. **Poti-Widerstandswert** (Block-Potis, Mod-Potis) — nirgends in der ARCHITECTURE.md
   festgelegt. Vor der Bestellung bestätigen.
2. **Buchsen-Footprint** — 3,5mm vs. 6,35mm, Panel- vs. Platinenmontage, noch nicht
   entschieden.
3. **Mux-Gehäuseform** (THT DIP-24 vs. SMD SOIC-24) — beeinflusst die Footprint-Wahl in KiCad.
