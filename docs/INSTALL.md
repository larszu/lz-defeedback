# LZ Defeedback – Installation

## macOS (Universal: Apple Silicon und Intel, ab macOS 11)

| Datei | Ziel |
|---|---|
| `LZ Defeedback.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| `LZ Defeedback.component` (AU) | `~/Library/Audio/Plug-Ins/Components/` |
| `LZ Defeedback.app` (Standalone) | `/Programme` |

Die Dateien sind nicht notarisiert. Nach dem Kopieren einmal im Terminal freigeben:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"LZ Defeedback.vst3" \
  ~/Library/Audio/Plug-Ins/Components/"LZ Defeedback.component" /Applications/"LZ Defeedback.app"
```

Danach die DAW neu starten bzw. den Plugin-Scan neu anstoßen.

## Windows (10/11, 64 Bit)

| Datei | Ziel |
|---|---|
| `LZ Defeedback.vst3` (Ordner) | `C:\Program Files\Common Files\VST3\` |
| `LZ Defeedback.exe` (Standalone) | beliebig |

Die Datei ist nicht signiert; SmartScreen fragt beim ersten Start nach („Weitere Informationen“ → „Trotzdem ausführen“).

## Standalone einrichten

„Settings…“ → Audiogerät wählen (Windows: ASIO-Treiber des Interfaces), Eingang freigeben, Puffer 64–128 Samples. Der Ausgang geht auf Monitor oder PA.
