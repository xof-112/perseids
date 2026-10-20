# 3F4 Panel-Stil (VCV Rack)

Hausstil der **3F4**-Module (3rdFloor4), zuerst umgesetzt bei Perseids. Dieses Blatt sammelt alle Regeln und Maße. `panel_3f4.py` im selben Ordner setzt sie als Code um: Ein Modul ruft die Funktionen auf, statt Maße abzutippen, und sieht dann automatisch gleich aus.

Alle Maße in **mm**. Positionen sind **Mittelpunkte**, so wie `createParamCentered` sie erwartet. Schriftgrößen sind **Versalhöhen** (Höhe eines Großbuchstabens).

## 1. Farben

| Rolle | Farbe | Konstante |
|---|---|---|
| Panel | `#1b1c1f` | `BG` |
| Scheibe hinter kleinen Push-Tastern | `#232428` | `BG2` |
| Display-Rahmen | `#060607` | `BEZEL` |
| Beschriftung, Titel, Pfeile | `#8fb8ff` (hellblau) | `INK` |
| Nebentexte (PUSH …, Unterzeilen) | `#7388aa` | `DIM` |
| Streifen unten (aus dem Logo) | `#000041` | `NAVY` |
| Trennlinie über dem Buchsenfeld | `#34353a` | `RULE` |
| senkrechte Trenner zwischen Buchsengruppen | `#4a4b50` | `SEP` |
| Ring IN L / IN R | `#d4314f` / `#e8b52c` | `RING_IN_L` / `RING_IN_R` |
| Ring OUT L / OUT R | `#8a4fd1` / `#e8b52c` | `RING_OUT_L` / `RING_OUT_R` |
| Logo: oberer Balken und „3F4“ | `#dcdcdc` | `LOGO_LIGHT` |
| Logo: dunkler / grauer Balkenteil | `#3b3c41` / `#5b5c67` | `LOGO_DARK` / `LOGO_GREY` |

Logo im Original, auf weißem Grund: Balken hell `#dddddd`, dunkel `#292929`, grau `#4d4e57`, Streifen `#000041`. Für das dunkle Panel sind die dunklen Teile aufgehellt.

## 2. Schrift

- **Beschriftung:** DejaVu Sans Condensed Bold, nur Großbuchstaben.
- **Titel:** DejaVu Sans Bold, Laufweite +0,9 mm.
- **Logo „3F4“:** Liberation Sans Regular.
- Text wird als **Pfad** ins SVG geschrieben, weil Rack keine Schriften rendert. Die Fonts braucht man also nur beim Erzeugen, nicht in Rack.

| Text | Versalhöhe | Farbe |
|---|---|---|
| Titel (Modulname) | 4,0 | `INK` |
| Potis, Encoder | 2,4 | `INK` |
| Taster | 2,0 | `INK` |
| Buchsen | 1,7 | `INK` |
| Unterzeilen, PUSH-Hinweise | 1,5 | `DIM` |

**Wortwahl:** Spielersprache. Beschriftet wird, was das Element tut (BLEND, TRAIL, REC), nicht „POT L“. Eine Zweitfunktion steht darunter, etwa `PUSH ⇄ DRY/WET`.

## 3. Bauteile (Rack componentlibrary)

| Element | Bauteil | Ø |
|---|---|---|
| Hauptpoti | `RoundBigBlackKnob` | 15,2 |
| Encoder (endlos) | `RoundLargeBlackKnob` | 12,2 |
| Haupttaster (REC, MENU …) | `VCVButton` | 6,1 |
| Push- und ◀/▶-Taster | `TL1105` | 5,2 |
| Buchse | `PJ301MPort` | 8,0 |
| Schraube | `ScrewBlack` | 5,1 |

## 4. Abstände Element ↔ Beschriftung

Die Werte sind vom Mittelpunkt aus gemessen, + heißt nach unten. „Grundlinie“ ist die Schriftgrundlinie.

| Element | Beschriftung | Weiteres |
|---|---|---|
| Hauptpoti | Grundlinie +12,4, Unterzeile +16,0 | Push-Taster rechts unten bei (+9,6 / +6,0), Scheibe `BG2` mit r 3,4 dahinter |
| Encoder | Grundlinie +9,6 | ◀/▶ bei ±11,5 auf gleicher Höhe, Pfeil-Dreiecke 4,6 darüber (Kantenlänge 2), Push 13,8 unter dem Encoder, „PUSH …“ 4,8 unter dem Push |
| Haupttaster | Grundlinie +7,0, Unterzeile +10,0 | Taster nebeneinander im Raster 11 |
| Buchse | Grundlinie −5,8 (über der Buchse) | Audio-Buchsen mit Farbring r 4,9 |

## 5. Raster und Ränder

- **Höhe** 128,5, **Breite** HP × 5,08. Perseids hat 22 HP = 111,76.
- **Kopf:** Titel mittig, Grundlinie 8,6. Das Logo ist 3,2 hoch, steht unten bündig auf der Titel-Grundlinie, rechte Kante 12,5 vom Panelrand.
- **Schrauben:** nur oben, bei (7,5 / 3,0) und (Breite − 7,5 / 3,0). Unten sitzen Buchsen, also keine Schrauben.
- **Display** (NT-Stil, 256 × 64): 86,7 breit, 21,7 hoch, mittig, Oberkante bei 13,0. Rahmen 2,0 rundum, Ecken r 1,5. Bei 86,7 mm ergibt sich in Rack bei 100 % Zoom genau ein Bildpunkt pro Display-Pixel.
- **Streifen unten:** 3,0 hoch, volle Breite, `NAVY`.
- **Buchsenfeld:**
  - Spalten im Raster 14, die erste 14 vom linken Rand (bei 22 HP 7 Spalten).
  - Reihen im Abstand 13, die unterste bei y = 120,0, damit der Ring 4,9 den Streifen nicht berührt. Mit 13 bleiben gut 1,5 mm zwischen einer Buchse und der Beschriftung der Reihe darunter.
  - Darüber eine Trennlinie (`RULE`, 0,3) bei 98,6 von x 6 bis Breite − 6, mit etwa 1 mm Luft zur Schrift darüber und darunter.
  - Gruppen (IN | CV | OUT) trennen senkrechte Linien `SEP`, mittig zwischen den Spalten, von 6,5 über bis 4,5 unter der Reihenmitte.
- **Reihenfolge der Buchsen:** In der unteren Reihe links IN L, IN R mit roten/gelben Ringen, rechts OUT L, OUT R mit violetten/gelben Ringen, CV dazwischen und darüber. CV-Buchsen zählen ab **1**.
- **Freiraum:** Zwischen Beschriftung und dem nächsten Element mindestens etwa 1 mm. Linien laufen nie durch Text.

## 6. Bedienung, Regeln für den Code

- **Ein Ort für Positionen:** `panel_def.py` erzeugt das Panel-SVG und den C++-Header mit denselben Positionen. Koordinaten nie von Hand im C++ eintragen.
- **MIDI-Map-fähig:** Jedes Bedienelement ist ein eigener Rack-Parameter.
  - Encoder sind endlos (24 Schritte pro Umdrehung bzw. pro CC-Hub) und haben zusätzlich ◀/▶-Taster für Pads.
  - Push-Funktionen haben eigene Taster.
- **Display:** in der Licht-Ebene zeichnen (`drawLayer` 1), damit es leuchtet, wenn Rack abgedunkelt ist.
- **Marke:** in `plugin.json` `"brand": "3F4"`, `"author": "3rdFloor4"`, Slug `3F4-<Modul>`.

## 7. Für ein neues Modul (z. B. Collagé)

```python
import sys; sys.path.insert(0, "style")      # panel_3f4.py mitnehmen
import panel_3f4 as p

pn = p.Panel(hp=22, title="COLLAGÉ")
pn.display(y=13.0)                            # nur bei Modulen mit NT-Display
pn.big_knob(20, 50, "DENSITY", sub="PUSH ⇄ …", push=True)
enc = pn.encoder(22, 80, "STEP", steps=True, push="…")
pn.button(pn.CX, 80, "MENU")
cols = pn.jack_columns(7)
pn.rule(98.6)
pn.jack(cols[0], p.JACK_LAST_ROW_Y, "IN L", ring=p.RING_IN_L)
pn.save("res/Collage.svg", "test/panel_parts.txt")
```

Danach den Positions-Header für C++ schreiben, wie in Perseids' `panel_def.py`, und mit `test/preview` (Rack-eigener SVG-Renderer) ein Vorschaubild erzeugen und ansehen.

Referenz: Perseids, `nt/vcv/` im Repo `xof-112/perseids` (`panel_def.py`, `res/Perseids.svg`, `test/panel-preview.png`).
