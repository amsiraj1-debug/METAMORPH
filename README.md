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
- Host automation for morph X/Y, influence radius, pitch, gain, transform strength and mix
- Four realtime/quality modes
- MIDI waypoint recall (notes 36-43 / C2-G2) plus MIDI CC morph control (20/21/22)
- Up to 8 saved waypoints; each waypoint stores cursor X/Y and influence radius
- Direct 1-8 waypoint buttons plus next/previous recall and a visible route in the workspace
- Target pitch-range display
- Input/output meters
- Full plugin-state serialization including target profiles and waypoints
- Windows GitHub Actions build

## DSP implementation

The live engine is an original spectral-envelope morphing processor. Imported references are analyzed into frequency-band profiles, then the current 2D cursor position blends or subtracts those profiles in real time. Version 0.3 adds a stronger 0-200% Transform Strength stage, expanded formant/body/presence/air shaping and target-dependent soft harmonic coloration so reference changes are clearly audible. The pitch shifter uses an original dual-delay granular-style implementation.

A **waypoint** is a saved light-cursor state for performance recall. Move the light to a useful blend, adjust its influence radius, press **+ WAYPOINT**, then recall that state with buttons 1-8, Previous/Next, or MIDI notes C2-G2. Waypoints do not automatically sequence themselves; use DAW automation or MIDI/shortcuts when you want timed movement.

This is **not** a binary patch, license bypass, or decompilation of proprietary DSP.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MetamorphRebuild_VST3
```

The VST3 is written to `build/MetamorphRebuild_artefacts/Release/VST3`.

GitHub Actions uploads the Windows VST3 bundle and standalone EXE as **Metamorph-CR-Windows-x64**.


## Reference Match v0.4

Transform Strength has been replaced conceptually by **Voice Match**. The 0-200% control no longer drives a saturation stage or intentionally raises output level. The processor now analyzes the live input spectrum, compares it with the active reference-voice spectrum, and applies the spectral difference. A smoothed RMS compensation stage keeps the wet path close to the dry/input loudness before Dry/Wet mixing.

The Windows regression build verifies that 200% Voice Match remains audibly different from the source, stays within the configured RMS tolerance, remains below the clipping ceiling in the smoke test, and preserves waypoint/MIDI recall behavior.


## DNnI model integration

The repository now supports selecting a `.dnni` model from the plugin UI with **LOAD DNNI MODEL**. The uploaded `model.dnni` used to validate this path is 87,529,366 bytes with SHA-256 `48fe10df60bb4d92d2a5f19f02b4d712dc070bebba9d5ea1ef2172d9f82a428c`.

The plugin validates the DNnI container, stores the model selection in plugin state, and routes the wet vocal through a dedicated DNnI backend whenever a compatible authorized runtime bridge is available. If no runtime bridge is installed, the UI states that clearly and the existing Reference Match processor remains active.

See `docs/DNNI_BACKEND.md` for the adapter ABI and runtime setup. The proprietary `.dnni` model itself is intentionally excluded from Git.


## DNnI Inspector

The repository now includes a clean-room binary inspector for unprotected `.dnni` container structure. It does **not** decrypt protected sections or bypass access controls.

Build:

```powershell
cmake --build build --config Release --target DnniInspector
```

Run:

```powershell
DnniInspector.exe model.dnni model-report.json
```

The JSON report contains the file size and SHA-256, signature/header bytes, merged block regions, entropy/printable/zero ratios, classifications for plain text, candidate FP32/FP16/INT8 weight regions, high-entropy opaque regions, and extracted printable strings. The classifier is heuristic: a candidate numeric region is not proof that a tensor has been decoded.

GitHub Actions packages the inspector as `DnniInspector.exe` beside the VST3/standalone build. The next clean-room step is to use reports from known authorized DNnI models to infer stable container tables, tensor dimensions, and operator metadata where those structures are stored in clear form.
