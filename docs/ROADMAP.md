# Roadmap & offene Aufgaben

Stand: 11.10.2026 – Version 0.1.0 (Regelbasierte Erkennung, nur simuliert geprüft)

## Erledigt

- DSP-Kern ohne JUCE: Notch-Bank (24 Biquads, 0 ms Latenz), Erkennung auf dem Ausgang, Modi Live/Einmessen
- JUCE-Plugin (VST3, AU, Standalone) mit Spektrum und Filteranzeige, feste Filter im Projekt gespeichert
- `tests/sim_test.cpp`: geschlossene Rückkopplungsschleife bei 11,025 / 48 / 96 / 192 kHz, Musik- und Gesangs-Fehlalarmtests, läuft sauber unter ASan/UBSan
- `auval` ohne Warnung bestanden

## 1. Test an echter Anlage (höchste Priorität)

Bisher ist nichts an Mikrofon und PA geprüft.

- [ ] Standalone: Eingang unter „Settings…“ freigeben, Mikrofon → Plugin → Monitor/PA
- [ ] Einmessen: Anlage langsam lauter, prüfen, ob die Filter auf den richtigen Frequenzen landen (Vergleich mit RTA)
- [ ] Live: Sprache, Gesang, Instrumente – Fehlauslösungen notieren (Frequenz, Quelle)
- [ ] Reaktionszeit und Klangverfärbung bei Max. Tiefe −24 dB und Breite 1/10 okt beurteilen
- [ ] Aufnahmen (Mikrofonsignal, 48 kHz WAV) von echten Rückkopplungen **und** von unkritischem Material sichern → Trainings- und Testdaten für Abschnitt 3

## 2. Windows-Build

- [ ] GitHub-Actions-Workflow für Windows (VST3 + Standalone, MSVC 2022) und macOS (Universal Binary arm64/x86_64)
- Achtung: Repo ist privat, Actions-Minuten kosten Geld. Workflow nur bei v-Tags laufen lassen, nicht bei jedem Push.
- [ ] Release per v-Tag, Installer/ZIP an den Release hängen
- [ ] macOS: Signieren und Notarisieren (Developer-ID nötig)

## 3. Eigenes neuronales Netz (Klassifikator)

**Grundsatz:** komplett eigene Entwicklung. Nichts aus Alpha Labs Defeedback verwenden – keine Gewichte, keine Architektur, kein dekompilierter Code. Das Original wurde nur statisch untersucht, um die Frage „echte KI oder Fake“ zu klären.

Ziel: Das Netz ersetzt die handgesetzten Schwellen in `FeedbackDetector` (PNPR, PAPR, Steigung, Obertontest) durch eine gelernte Entscheidung „Rückkopplung ja/nein“ je Spitzenkandidat. Die Notch-Bank bleibt im Signalweg → weiterhin 0 ms Latenz.

Schritte:

1. **Datengenerator** (`training/generate.py` oder C++-Tool auf Basis von `sim_test.cpp`)
   - Quellen: Rauschen, synthetische Sprache, Musik (Akkorde, gehaltene Töne mit/ohne Vibrato), später echte Aufnahmen aus Abschnitt 1
   - Raum: zufällige Moden (Frequenz, Güte, Schleifenverstärkung 0,8–1,6), Laufzeit 2–30 ms, Sättigung
   - Label je Kandidat und Frame: liegt eine instabile Schleifenmode auf dieser Frequenz?
2. **Merkmale je Kandidat** (pro Frame, Verlauf über ~24 Frames): Pegel, PNPR, PAPR, Frequenzabweichung, Pegelverlauf, Pegel bei 1/2·f, 2·f, 3·f …
3. **Modell:** klein halten (z. B. MLP oder 1D-Conv über den Verlauf, < 20k Parameter), damit es pro Frame im Audio-Thread rechenbar ist
4. **Training:** PyTorch im vorhandenen `.venv` (torch nachinstallieren), Validierung auf zurückgehaltenen Räumen/Quellen
5. **Export:** Gewichte als generierter C++-Header (`src/dsp/model_weights.h`), Inferenz handgeschrieben, keine Laufzeitbibliothek
6. **Integration:** Schalter „Erkennung: Regeln / Netz“, beide Wege bleiben testbar
7. **Prüfung:** bestehende Schleifentests müssen grün bleiben; neuer Vergleichstest Regeln vs. Netz auf ungesehenen Daten (Trefferquote, Fehlalarme, Reaktionszeit)

## 4. Verbesserungen am Regel-Detektor

- [ ] Gedächtnis für freigegebene Filter: kehrt Rückkopplung auf einer kürzlich freigegebenen Frequenz zurück, gleich mit der vorherigen Tiefe starten statt −9 dB
- [ ] Bekannte Grenze: lang gehaltener, obertonarmer Ton ohne Vibrato wird als Rückkopplung erkannt (Kandidat für das Netz)
- [ ] Optional: Frequenzverschiebung (wenige Hz) als zusätzliche Stabilitätsreserve, abschaltbar
- [ ] Mehrkanal-Variante (je Kanal eigene Erkennung) für Monitorwege

## 5. Oberfläche

- [ ] Public Sans als eingebettete Schrift (Marken-Typo, lokal ausgeliefert)
- [ ] Filterliste mit Einzel-Löschen/-Fixieren per Klick auf die Marke im Spektrum
- [ ] Hell-Modus prüfen (lzm-web: Werkzeug-Modus)
- [ ] Tooltips aktivieren (TooltipWindow fehlt noch)

## 6. Lizenz

- JUCE 8: AGPLv3 oder kommerzielle Lizenz. Vor einer Weitergabe oder einem Verkauf der Binärdateien klären.
