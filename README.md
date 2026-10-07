# Metamorph CR

A clean-room, real-time Windows vocal timbre morphing VST3/standalone application built with JUCE.

This project recreates the **workflow and functional ideas** of a reference-voice morphing plugin with original source code and original visuals. It does not contain Dreamtonics source code, branding, activation logic, licensed factory voices, or copied UI assets.

## Current feature set

- Real-time VST3 and standalone builds
- Import clean WAV/AIFF/FLAC voice references
- Drag-and-drop reference import
- Automatic reference analysis into a compact spectral/timbre profile
- Multiple target voices on a 2D morphing canvas
- Light/influence-radius style blending and shadow subtraction
- Generated target voices from reusable hex-style voice codes
- 40 procedural starter profiles, generated locally from code
- Pre-gain, output gain and dry/wet controls
- Real-time pitch shifting
- Host automation for morph X/Y, influence radius, pitch, gain and mix
- Four realtime/quality modes
- MIDI waypoint recall (notes 36-43) plus MIDI CC morph control (20/21/22)
- Up to 8 saved waypoints
- Target pitch-range display
- Input/output meters
- Full plugin-state serialization including target profiles and waypoints
- Windows GitHub Actions build

## DSP implementation

The live engine is an original spectral-envelope morphing processor. Imported references are analyzed into frequency-band profiles, then the current 2D cursor position blends or subtracts those profiles in real time. The pitch shifter uses an original dual-delay granular-style implementation.

This is **not** a binary patch, license bypass, or decompilation of proprietary DSP.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MetamorphRebuild_VST3
```

The VST3 is written to `build/MetamorphRebuild_artefacts/Release/VST3`.

GitHub Actions uploads the Windows VST3 bundle and standalone EXE as **Metamorph-CR-Windows-x64**.
