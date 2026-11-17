# LZ Defeedback

Rückkopplungsunterdrückung als Plugin (VST3, AU, Standalone) für Live-Beschallung: Mikrofon, Monitor, PA.

LZ Defeedback findet Rückkopplungen und setzt schmale Kerbfilter (Notches) genau auf ihre Frequenz. Das Signal läuft dabei **ohne zusätzliche Latenz** durch – solange nichts koppelt, ist der Ausgang bitgleich mit dem Eingang.

## Download

Fertige Dateien für macOS (Universal) und Windows (x64) hängen am jeweiligen [Release](https://github.com/larszu/lz-defeedback/releases/latest): VST3, AU (nur Mac) und Standalone. Installation: [docs/INSTALL.md](docs/INSTALL.md).

## Systemvoraussetzungen

| | Mindestens | Empfohlen |
|---|---|---|
| Betriebssystem | macOS 11 oder Windows 10, jeweils 64 Bit | aktuelles macOS bzw. Windows 11 |
| Prozessor | Apple Silicon oder Intel/AMD x64 mit SSE2 (Windows auf ARM wird nicht gebaut) | ab Apple M1 bzw. Intel Core i5 / AMD Ryzen 5 der letzten Jahre |
| Arbeitsspeicher | 4 GB | 8 GB |
| Host | DAW oder Live-Host mit VST3 oder AU – oder die Standalone-App | Live-Host mit festem Puffer (z. B. Gig Performer, Reaper, Ableton Live) |
| Kanäle | Mono oder Stereo, Ein- gleich Ausgang | – |
| Abtastrate | 44,1–192 kHz | 48 kHz |

Der Rechenaufwand ist gering: je Kanal 24 Biquads im Signalweg und eine FFT mit 4096 Punkten alle 512 Samples. Ein Wert für die CPU-Last an echter Hardware ist noch nicht gemessen.

### Hardware-Empfehlung für den Live-Einsatz

- **Audio-Interface mit stabilem Treiber:** Core Audio (Mac) bzw. ASIO (Windows; nicht den generischen Windows-Treiber). Puffer 64–128 Samples bei 48 kHz halten die Wandler- und Pufferlatenz unter etwa 5 ms – das Plugin selbst fügt keine hinzu.
- **Einschleifen:** als Insert auf dem Mikrofonkanal oder auf dem Monitor-/PA-Weg, hinter EQ und Dynamik. Je Monitorweg eine eigene Instanz.
- **Digitalpult mit Rechneranbindung** (z. B. über USB-, Dante- oder MADI-Karte) erlaubt das Einschleifen in einzelne Wege ohne zusätzliche Wandlung.
- **Zum Einmessen:** Messmikrofon und ein RTA zum Gegenprüfen der gefundenen Frequenzen sind hilfreich, aber nicht nötig.
- **Stromsparmodus aus**, WLAN und Bluetooth während der Veranstaltung möglichst aus – Aussetzer im Audiotreiber sind im Live-Betrieb gefährlicher als die Rückkopplung selbst.

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

Release: Tag `v*` pushen – `.github/workflows/release.yml` baut auf macOS (Universal) und Windows, führt die Tests und `auval` aus und hängt die ZIPs ans Release.

## Tests

`tests/sim_test.cpp` simuliert eine geschlossene Schleife: Quelle → Mikrofon → Plugin → Lautsprecher mit Sättigung → Raum mit Resonanzen und 7 ms Laufzeit → Mikrofon. Geprüft wird bei 11,025 / 48 / 96 / 192 kHz:

- Ohne Plugin schaukelt sich die Schleife bis in die Begrenzung auf; mit Plugin bleibt sie auf Quellpegel.
- Reaktion nach 0,25–0,4 s, auch bei langsam wachsender Rückkopplung und laufender Musik.
- Kein Filter bei Rauschen; bitgleicher Ausgang im Ruhezustand.
- Kaum Fehlauslösungen bei Musik und lang gehaltenen Tönen: höchstens ein Filter (−9 dB) bei Gesang mit Vibrato, sonst keiner.

Grenze: Ein lang gehaltener, obertonarmer Sinuston ohne Vibrato ist spektral nicht von Rückkopplung zu unterscheiden.

## Weiter

Offene Aufgaben und Plan (Praxistest, Signierung, eigenes neuronales Netz): [docs/ROADMAP.md](docs/ROADMAP.md)

## Lizenz

Eigener Code: © Lars Zumpe Medienproduktion. JUCE 8 steht unter AGPLv3 oder einer kommerziellen JUCE-Lizenz – vor einer Weitergabe der Binärdateien die passende Lizenz klären.
