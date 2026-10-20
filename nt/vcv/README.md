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

`make -C test` prüft den Host nativ ohne Rack (Audio, Bedienung, Menü, zwei Instanzen, Speichern).

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

## Buchsen

| Panel | NT-Eingang | ab Werk |
|---|---|---|
| IN L, IN R | Input 1, 2 | Audio *In L* / *In R* |
| CV 3 … CV 12 | Input 3 … 12 | *Clock in* = Input 3, alle anderen frei |
| OUT L, OUT R | Output 1, 2 | *Out L* / *Out R* |

Die CV-Eingänge sind frei zuweisbar, über dieselben Routing-Parameter wie am NT. Die Parameter stehen auf der Seite *Routing* (*Rec trig in*, *Clock in*, *Reso V/Oct in*) und auf den Seiten *Mod 1 … Mod 12* (*Mod n in*). Dort Input 3 … 12 wählen, dann wirkt die Buchse CV 3 … CV 12. Damit sind alle 12 NT-Eingänge auf dem Panel.

## Speichern, Kontextmenü

- Alle Parameter werden mit dem Patch gespeichert, nach Namen (wie im NT-Preset). Die aufgenommenen Trails werden nicht gespeichert.
- Rechtsklick → **Trail length** 5/10/15/20/30 s. Das baut den Algorithmus neu: Die Werte bleiben, die Trails sind danach leer.
- **Initialize** setzt alle Perseids-Parameter auf Werkseinstellung.

## Aufbau

- `src/nt_host.*`: der NT-Host. Er verwaltet Speicher, Parameter, 64 Busse (12 Eingänge, 8 Ausgänge, 44 Aux), die Bedienung (`customUi`), das Parametermenü und das Display. Alles läuft im Audio-Thread von Rack, das Panel liest nur eine Kopie des Bildschirms. Der Host ist portables C++ ohne Rack-Header.
- `src/nt_api.cpp`, `src/nt_globals.cpp`: die NT-API, wie das Plug-in sie sieht. Die Zeichenfunktionen sind die von nt_emu, siehe `../render/nt_raster.h`. Mehrere Module teilen keinen Zustand.
- `src/Perseids.cpp`: das Rack-Modul, Panel-Widget, Endlos-Encoder und Display.
- `panel_def.py` erzeugt `res/Perseids.svg` und `src/panel_layout.hpp`. Positionen nur dort ändern.
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
