# Mantis Vex Q

A parametric EQ VST3 plugin built with JUCE 8. 24 bands, dynamic EQ, linear phase mode, oversampling, and M/S monitoring.

![Mantis Vex Q](Screenshot%202026-06-23%20153704.png)

---

## Features

**EQ**
- 24 independent bands
- Filter types: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, All Pass, Tilt Shelf
- Slopes: 12 / 24 / 36 / 48 dB/oct (Butterworth cascade)
- Per-band channel routing: Stereo, Left, Right, Mid, Side

**Dynamic EQ**
- Per-band dynamic mode with threshold, attack, release, and ratio controls
- External sidechain input support

**Processing**
- Linear phase mode (4096-point FIR convolution, ~46ms PDC at 44.1kHz)
- Oversampling: 1x / 2x / 4x / 8x
- Auto-gain with A-weighting (IEC 61672) — gain compensation weighted by ear sensitivity
- Output gain with smoothing

**Monitoring**
- Real-time spectrum analyzer (pre/post EQ)
- Monitor solo and delta mode
- M/S monitoring (stereo / mid / side)

**Workflow**
- Click EQ display to add bands, drag nodes for freq/gain, scroll for Q
- A/B comparison slots
- Undo/redo (Ctrl+Z / Ctrl+Y)
- Band copy/paste (Ctrl+C / Ctrl+V)
- Keyboard shortcuts: E (enable), B (bypass), D (dynamic), S (solo)
- MIDI CC learn — right-click any knob to assign

---

## Installation

1. Download `Mantis-Vex-Q-v1.0.0-win64.zip` from [Releases](../../releases)
2. Extract `Mantis Vex Q.vst3`
3. Copy it to your VST3 folder — typically `C:\Program Files\Common Files\VST3\`
4. Rescan plugins in your DAW

**Requirements:** Windows 10/11 x64, VST3 host

---

## Building from source

**Dependencies**
- [JUCE 8](https://juce.com/) at `D:/Juce/JUCE` (or update the path in `CMakeLists.txt`)
- Visual Studio 2022
- CMake 3.22+

```
git clone https://github.com/mantisvex/mantisvex-Q.git
cd mantisvex-Q
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

The built plugin will be at `build/MantisVexQ_artefacts/Release/VST3/Mantis Vex Q.vst3`.

---

## License

GNU General Public License v3 — see [LICENSE](LICENSE).
