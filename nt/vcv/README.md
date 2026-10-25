# Perseids für VCV Rack 2

Von **3F4** (3rdFloor4).

Perseids als natives Rack-Modul (22 HP). Im Modul läuft **derselbe Code wie auf dem disting NT** (`nt/perseids_nt.cpp`, unverändert), getragen von einem kleinen NT-Host (`src/nt_host.*`). Klang, Bedienung und Display sind damit wie am NT: Das Display ist das NT-Display Pixel für Pixel, bei 100 % Zoom genau ein Bildpunkt pro NT-Pixel.

## Installieren

Mit dem fertigen Paket: `3F4-Perseids-2.0.29-win-x64.vcvplugin` nach `%LOCALAPPDATA%\Rack2\plugins-win-x64\` legen und Rack neu starten. Rack entpackt es selbst. Das Modul steht dann unter **3F4 → Perseids**.

Selbst bauen (MSYS2 MinGW 64 oder Linux/macOS, Rack SDK 2.x):

```
cd nt/vcv
make RACK_DIR=<Pfad zum Rack-SDK> install
```

`make -C test` prüft den Host nativ ohne Rack (Audio, Bedienung, Menü, zuweisbare Regler, zwei Instanzen, Speichern). `python3 test/preview_panels.py <Rack>/res/ComponentLibrary` rendert beide Panels mit Racks eigenem SVG-Renderer nach `test/panel-preview*.png`.

## Bedienelemente

Alle Bedienelemente sind normale Rack-Parameter. **MIDI-Map** lernt jedes davon: im MIDI-Map-Modul auf einen freien Eintrag klicken, dann das Bedienelement am Perseids-Panel anfassen.

| Panel | NT | Perseids-Ansicht | im Menü |
|---|---|---|---|
| BLEND / SCAN / SIZE | Poti L / C / R | Blend, Scan, Size (nach Druck: Dry/Wet, Reso mix, Atmosphere), mit Catch-up | Poti L = Seite, Poti C = Parameter, Poti R = Wert (absolut) |
| PUSH neben jedem Poti | Poti-Druck | Ziel des Potis umschalten | – |
| TRAIL (Encoder L) | Encoder L | Trail wählen, hinter Trail 5 die Mod-Ansicht | Parameter (läuft über die Seitengrenzen) |
| LEVEL (Encoder R) | Encoder R | Level des Trails, in der Mod-Ansicht der Amount | Wert ±1 (gedrückt halten: ±10) |
| ◀ / ▶ an den Encodern | – | ein Schritt je Druck, gehalten mit Wiederholung | dasselbe |
| PUSH SOLO / PUSH LOCK | Encoder-Druck | Solo / Lock des Trails | L: nächste Seite, R: bestätigen (z. B. *Reset all mods*) |
| REC | Taste 3 | Aufnahme auslösen | – |
| HOLD INF | Taste 4 | Hold unendlich an/aus | – |
| MENU | (Menü des NT) | öffnet die Parameterseiten | schließt sie wieder |

**MIDI-Tipps**
- **Potis** auf absolute Regler (Fader/Knobs) legen. Perseids holt den Wert ab, bevor es übernimmt; bis dahin ist der Wert in der Fußzeile grau.
- **Encoder**: Ein absoluter CC über den ganzen Weg ergibt 24 Rasten. Bei Endlos-Encodern im Absolut-Modus geht es über den Umbruch ohne Sprung weiter.
- **Pads und Tasten**: auf ◀ / ▶, PUSH, REC, HOLD INF, MENU (Note oder CC).

## Panel: Performance oder Large

Rechtsklick auf das Modul → **Panel**:

- **Performance (22 HP):** das Spielpanel, wie oben.
- **Large (22 + 12 HP):** Rechts kommt ein Feld mit **12 zuweisbaren Reglern** dazu. Module rechts daneben rücken zur Seite. Die Wahl wird mit dem Patch gespeichert.

**Zuweisen:** Rechtsklick auf einen der kleinen Regler → **Assign this knob to** → Seite → Parameter. Ein Häkchen zeigt das aktuelle Ziel. **Nothing** gibt den Regler frei, **Default** stellt die Werksbelegung wieder her (für alle auf einmal: Rechtsklick aufs Modul → *Assignable knobs: factory targets*). Routing-Parameter (Ein-/Ausgänge, Output mode) und Auslöser (*Clear trails*, *Reset all mods*) stehen nicht zur Wahl.

**Werksbelegung**, eine Reihe je Bereich, alles, was nicht schon auf einem Poti, Encoder oder Taster liegt:

| Reihe | links | Mitte | rechts |
|---|---|---|---|
| Spectra | Partials | Waveshape | Umbra/Aurora |
| Pitch | Ensemble | Pitch Spectra | Pitch Swarm |
| Swarm | Spread | Scatter | Direction |
| Reso / Zeit | Reso decay | Reso pitch | Hold |

**So verhalten sich die Regler:**
- Drehen setzt den Parameter über seinen ganzen Bereich (min … max, gerundet).
- Ändert sich der Wert anders, über das Menü am Display, ein Preset oder das Laden eines Patches, springt der Regler mit.
- Der Tooltip zeigt Name und Wert wie am NT, z. B. „Hold: 15 s“ oder „Direction: Rev“.
- Unter dem Regler steht der Name des Ziels.
- Die Ziele werden nach Namen mit dem Patch gespeichert. Die Regler arbeiten auch, wenn das kleine Panel gewählt ist; man sieht sie dann nur nicht.

**MIDI-Map mit Hardware-Encodern:** Jeder zuweisbare Regler ist ein eigener Rack-Parameter. In MIDI-Map einen Eintrag anklicken, dann den kleinen Regler anfassen. MIDI-Map arbeitet absolut: Ein CC 0…127 fährt den ganzen Bereich des Ziels ab.
- **Endlos-Encoder** am Controller dafür auf **absolut** stellen (0…127). Relative Encoder-Modi versteht MIDI-Map nicht, der Wert springt dann zwischen den Enden. Wer relative Encoder braucht, nimmt ein Mapping-Modul, das sie kann (z. B. Stoermelder MIDI-CAT).
- Weil die Regler ihrem Ziel folgen, gibt es keinen Sprung, wenn du die Belegung wechselst: Der Rack-Regler steht sofort auf dem neuen Wert. Beim ersten Bewegen des Hardware-Reglers springt der Wert allerdings auf dessen Stellung. MIDI-Map hat dafür keinen Pickup.

## Buchsen

| Panel | NT-Eingang | ab Werk |
|---|---|---|
| IN L, IN R | Input 1, 2 | Audio *In L* / *In R* |
| CV 1 … CV 10 | Input 3 … 12 | *Clock in* = CV 1, alle anderen frei (*Reset in* z. B. auf CV 2) |
| OUT L, OUT R | Output 1, 2 | *Out L* / *Out R* |

Die CV-Eingänge sind frei zuweisbar, über dieselben Routing-Parameter wie am NT. Die Parameter stehen auf den Seiten *Routing* (*Rec trig in*), *Clock* (*Clock in*, *Reset in*), *Resonator* (*Reso V/Oct in*) und auf den Seiten *Mod 1 … Mod 12* (*Mod n in*). Das Menü nennt die Eingänge wie die Buchsen: *In L*, *In R*, *CV 1* … *CV 10*. Damit liegen alle 12 NT-Eingänge auf dem Panel. Ein vom NT übernommenes Preset mit „Input 3“ landet auf CV 1, „Input 12“ auf CV 10.

## Speichern, Kontextmenü

- Alle Parameter werden mit dem Patch gespeichert, nach Namen (wie im NT-Preset). Die aufgenommenen Trails werden nicht gespeichert.
- Rechtsklick → **Trail length** 5/10/15/20/30 s. Das baut den Algorithmus neu: Die Werte bleiben, die Trails sind danach leer.
- **Initialize** setzt alle Perseids-Parameter auf Werkseinstellung.

## Aufbau

- `src/nt_host.*`: der NT-Host. Er verwaltet Speicher, Parameter, 64 Busse (12 Eingänge, 8 Ausgänge, 44 Aux), die Bedienung (`customUi`), das Parametermenü und das Display. Alles läuft im Audio-Thread von Rack, das Panel liest nur eine Kopie des Bildschirms. Der Host ist portables C++ ohne Rack-Header.
- `src/nt_api.cpp`, `src/nt_globals.cpp`: die NT-API, wie das Plug-in sie sieht. Die Zeichenfunktionen sind die von nt_emu, siehe `../render/nt_raster.h`. Mehrere Module teilen keinen Zustand.
- `src/Perseids.cpp`: das Rack-Modul, Panel-Widget, Endlos-Encoder und Display.
- `panel_def.py` erzeugt `res/Perseids.svg`, `res/Perseids-Large.svg` und `src/panel_layout.hpp`. Positionen nur dort ändern. Farben, Schriften, Bauteilgrößen und Abstände kommen aus dem 3F4-Panel-Kit `style/panel_3f4.py`, die Regeln dazu in `style/3F4_PANEL_STYLE.md`.
- `src/fonts/`: die NT-Schriften aus nt_emu (MIT, © Neal Sanche). Tom Thumb ist MIT, Selawik steht unter OFL, pixelmix ist freeware von Andrew Tyler.
- `tools/mklibrack.py`: Import-Bibliothek für libRack.dll, damit die Windows-DLL ohne Rack SDK gebaut werden kann (Cross-Build unter Linux).

## Panel-Farben (3F4-Stil)

| Rolle | Farbe |
|---|---|
| Panel | `#1b1c1f` |
| Flächen hinter Tastern | `#232428` |
| Display-Rahmen | `#060607` |
| Beschriftung, Titel, Pfeile | `#8fb8ff` (hellblau) |
| Nebentexte (PUSH …, INF) | `#7388aa` |
| Streifen unten (aus dem Logo), 3 mm, volle Breite | `#000041` |
| Trennlinien | `#34353a`, `#4a4b50` |
| Ringe IN L / IN R | `#d4314f` / `#e8b52c` |
| Ringe OUT L / OUT R | `#8a4fd1` / `#e8b52c` |
| Logo: oberer Balken und „3F4“ | `#dcdcdc` |
| Logo: dunkler / grauer Balkenteil (für dunkles Panel aufgehellt) | `#3b3c41` / `#5b5c67` |
| Logo-Original: Balken hell / dunkel / grau, Streifen | `#dddddd` / `#292929` / `#4d4e57`, `#000041` |
