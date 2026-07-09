Iterativer Hardware-Aufbau für Flaresum
Der größte Vorteil der Daisy Seed Architektur ist, dass der digitale Teil (Prozessor, Display, Potis) mit 3.3V arbeitet. Diese Spannung erzeugt das Daisy Seed völlig problemlos aus dem USB-Kabel, das du ohnehin zum Programmieren (Flashen) angeschlossen hast.
Die gefährlichen ±12V aus dem Eurorack brauchst du nur für die analogen Operationsverstärker (TL074), um die lauten Eurorack-Signale zu bändigen.
Daher bauen wir das Breadboard in drei sicheren, isolierten Schritten auf:
Schritt 1: Der "Schreibtisch-Modus" (Nur USB-Strom)
Passend zu ARCHITECTURE.md: Phase 0, 1 und 2
In diesem Schritt bleibt das Eurorack-Flachbandkabel komplett in der Schublade. Das Daisy Seed wird nur per USB mit deinem Rechner verbunden.
Was du aufbaust:
1. Daisy Seed auf das Breadboard stecken.
2. Block 5 (OLED Display): Verbinde es mit dem 3v3 Digital Pin (38) und den SPI-Pins.
3. Block 4 (Multiplexer & UI): Baue die beiden MUX-Chips auf (ebenfalls an 3v3 Pin 38). Schließe erst einmal nur 2-3 Potis und den Cycle-Taster an, um das Prinzip zu testen.
Was du programmieren kannst:
Du kannst das komplette Menü-System, den Cycle-Mechanismus, das ADC-Polling und das Display-Dashboard (Phase 1 & 2) völlig risikofrei in der Cursor IDE entwickeln und testen.
Schritt 2: Der "Audio-Modus" (Eurorack-Strom zwingend nötig)
Passend zu ARCHITECTURE.md: Phase 3 bis 9
Jetzt geht es an die Klangerzeugung (Flares, Granular, Reverb). Sobald du echte Eurorack-Signale in das Modul schicken willst, brauchen wir die Operationsverstärker (TL074) zur Pegelabsenkung. Der TL074 funktioniert nicht mit USB-Strom, er braucht zwingend die ±12V!
Was du aufbaust:
1. Block 1 (Eurorack Power): Flachbandkabel ans Breadboard. WICHTIG: Verbinde die Masse (GND) des Euroracks unbedingt mit der Masse (DGND/AGND) des Daisy Seeds!
2. Daisy Power-Switch: Verbinde nun die +12V Schiene mit dem VIN Pin (39) des Daisy Seeds. (Das Daisy holt sich seinen Strom nun aus dem Rack; das USB-Kabel dient ab jetzt nur noch der Datenübertragung für den Code).
3. Block 2 (Audio I/O): Baue den ersten TL074-Chip mit seinen 100k/33k Widerständen und Kondensatoren auf, um das Audio In/Out zu verkabeln.
Was du programmieren kannst:
Du kannst jetzt Audiosignale sicher in den SDRAM aufnehmen und die gesamte DSP-Kette (Spectra, Swarm, Reverb) testen und abhören.
Schritt 3: Der "Modulations-Modus"
Passend zu ARCHITECTURE.md: Phase 10 und 11
Das Modul generiert jetzt Sound und lässt sich per Display/Potis bedienen. Zum Schluss öffnen wir es für externe Steuerspannungen aus deinem Rack.
Was du aufbaust:
1. Block 3 (CV Inputs): Baue den zweiten TL074 auf und schließe die Thonkiconn-Buchsen (inkl. Normalling-Verdrahtung) an.
2. Die Lebensretter (BAT43): Setze die Schottky-Dioden exakt wie im Schaltplan ein. Sie sind ab jetzt deine Lebensversicherung, falls du aus Versehen +10V in einen CV-Eingang schickst.
Was du programmieren kannst:
Die Mod-Matrix, Hardware-Normalling (LFO vs. ext. CV) und die Kalibrierung. Dein Modul ist physisch und im Code komplett!