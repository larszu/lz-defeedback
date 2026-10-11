# LZ Defeedback

Rückkopplungsunterdrückung als Plugin (VST3, AU, Standalone) für Live-Beschallung: Mikrofon, Monitor, PA.

LZ Defeedback findet Rückkopplungen und setzt schmale Kerbfilter (Notches) genau auf ihre Frequenz. Das Signal läuft dabei **ohne zusätzliche Latenz** durch – solange nichts koppelt, ist der Ausgang bitgleich mit dem Eingang.

## Funktionsweise

- **Signalweg:** bis zu 24 schmale Glockenfilter mit Absenkung (Biquads, doppelte Genauigkeit). Inaktive Filter werden übersprungen.
- **Erkennung:** läuft parallel auf dem Ausgangssignal (FFT, ca. 85 ms Fenster, 10,7 ms Takt bei 48 kHz). Eine Spitze gilt nur als Rückkopplung, wenn sie
  - schmalbandig ist (Abstand zu den Nachbarbändern),
  - deutlich über dem Gesamtpegel liegt,
  - über mehrere Takte am Stück besteht und dabei nicht abklingt,
  - in der Frequenz stabil ist (Vibrato fällt heraus),
  - kein Teil einer Obertonreihe ist – sonst muss sie über ~260 ms gleichmäßig wachsen. Stimmen und Instrumente haben einen kräftigen zweiten Teilton, Rückkopplung an einem sättigenden Verstärker nur ungerade.
- **Reaktion:** neues Filter mit −9 dB; besteht die Spitze weiter, wird es in 3-dB-Schritten bis zur maximalen Tiefe vertieft. Weil die Erkennung hinter den Filtern misst, wird genau so tief abgesenkt wie nötig.
- **Live-Modus:** Filter bleiben nach dem letzten Treffer für die Haltezeit stehen und kehren dann langsam auf 0 dB zurück.
- **Einmessen:** neue Filter bleiben fest stehen. „Alle fixieren“ übernimmt die aktuellen Live-Filter als feste. Feste Filter werden mit dem Projekt gespeichert.

## Bedienung

| Regler | Bedeutung |
|---|---|
| Empfindlichkeit | 0 % vorsichtig (~320 ms bis zur Reaktion), 100 % schnell (~64 ms) |
| Max. Tiefe | tiefste Absenkung pro Filter |
| Breite | Filterbreite in Oktaven (Standard 1/10) |
| Filter | Anzahl nutzbarer Filter |
| Halten / Rückkehr | Live-Filter: Wartezeit nach dem letzten Treffer / Zeit für 12 dB Rückkehr |

Typischer Ablauf: Modus **Einmessen**, Anlage bei offenen Mikrofonen langsam lauter machen, bis die ersten Filter stehen. Dann **Live** – die übrigen Filter fangen im Betrieb auftretende Rückkopplungen ab.

## Bauen

Voraussetzungen: CMake ≥ 3.22, Ninja, Xcode Command Line Tools (macOS) bzw. Visual Studio 2022 (Windows).

```bash
git clone --recurse-submodules <repo>
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Ergebnisse liegen in `build/LZDefeedback_artefacts/Release/` (VST3, AU, Standalone).

## Tests

`tests/sim_test.cpp` simuliert eine geschlossene Schleife: Quelle → Mikrofon → Plugin → Lautsprecher mit Sättigung → Raum mit Resonanzen und 7 ms Laufzeit → Mikrofon. Geprüft wird bei 11,025 / 48 / 96 / 192 kHz:

- Ohne Plugin schaukelt sich die Schleife bis in die Begrenzung auf; mit Plugin bleibt sie auf Quellpegel.
- Reaktion nach 0,25–0,4 s, auch bei langsam wachsender Rückkopplung und laufender Musik.
- Kein Filter bei Rauschen; bitgleicher Ausgang im Ruhezustand.
- Kaum Fehlauslösungen bei Musik und lang gehaltenen Tönen: höchstens ein Filter (−9 dB) bei Gesang mit Vibrato, sonst keiner.

Grenze: Ein lang gehaltener, obertonarmer Sinuston ohne Vibrato ist spektral nicht von Rückkopplung zu unterscheiden.

## Weiter

Offene Aufgaben und Plan (Praxistest, Windows-Build, eigenes neuronales Netz): [docs/ROADMAP.md](docs/ROADMAP.md)

## Lizenz

Eigener Code: © Lars Zumpe Medienproduktion. JUCE 8 steht unter AGPLv3 oder einer kommerziellen JUCE-Lizenz – vor einer Weitergabe der Binärdateien die passende Lizenz klären.
