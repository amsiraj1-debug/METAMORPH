# Metamorph Rebuild

A new Windows VST3 for offline voice conversion with local RVC `.pth` models. Record a vocal track, choose a voice, transform it, replay it in the DAW and blend the result. No account or iLok is used by this implementation.

This is an independent reconstruction, not recovered Antares source code, an official Antares release, or a verified identical implementation of Metamorph. Analysis recovered embedded images and fonts from a user-supplied binary, but its main executable code remains encrypted. The public project uses new interface code and the open-source RVC inference architecture. The recovered resource gallery is a separate local deliverable; proprietary factory voice weights are not bundled.

## Install

Open this repository's **Actions → Build Windows VST3**, select a successful run, and download **Metamorph-Rebuild-Windows-x64-VST3**. Extract it, then copy the entire `Metamorph Rebuild.vst3` folder to `C:\Program Files\Common Files\VST3`. Keep its `Contents/Resources` folder intact: it includes the offline runtime and the shared RMVPE/HuBERT support models. Rescan plugins in your DAW.

## Use

1. Insert **Metamorph Rebuild** on a vocal track.
2. Click **Choose voice .pth**, or drop your RVC inference checkpoint on the interface.
3. Click **Record**, play the vocal section in the DAW, then stop recording. Alternatively, import a WAV/AIFF/FLAC at the desired DAW start position.
4. Set pitch in semitones and click **Transform**. Conversion runs in a separate process; the DAW continues passing audio while it runs.
5. Replay the recorded section in the DAW, or click **Preview**. Adjust **Dry / Wet** and export a WAV to keep the result.

The first conversion can take a minute or more while models initialize. This is a record-and-render processor, not low-latency live voice conversion. Preview requires the DAW to keep calling the plugin's audio callback.

Supported models: RVC v1/v2 F0-enabled inference checkpoints, 32/40/48 kHz, first speaker. This does not support arbitrary PyTorch architectures, training checkpoints, encrypted Antares `.bf` voices, or retrieval `.index` files. Checkpoints are loaded with `weights_only=True`. Audio is processed as mono, up to three minutes at 8–192 kHz. Output is centered on stereo tracks. Model path, mix, pitch and runtime selection are saved in the project state; recordings/results must be exported before closing the session.

## Build

Requires CMake 3.24+, Visual Studio 2022 C++ tools and Python 3.12 on Windows.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python tools/package_windows.py --build build
```

JUCE is pinned to 8.0.12. The RVC subset is pinned to commit `81eed5e8f68b6bed1789f682fe78cdd324495afc`. Build packaging fetches pinned shared support weights and verifies their SHA-256 checksums. No target voice model is uploaded or distributed by this workflow.

Development overrides: `METAMORPH_RESOURCES` selects the folder containing `worker/`, `rvc/`, `runtime/` and `assets/`. `METAMORPH_PYTHON` selects an existing Python executable; `METAMORPH_ASSETS` selects existing support models; `METAMORPH_CACHE_DIR` redirects temporary conversion sessions.

`tools/extract_resources.py INPUT.vst3 OUTPUT_FOLDER` extracts validated PNG/SVG/font resources and a browsable gallery. Install `pefile` to additionally dump PE sections. Extracted machine-code sections are not buildable source. The extractor never executes the binary.

## Validation

The workflow checks dry passthrough, recording length, WAV import/export, reset, parameter-state recall and editor creation. It then exercises the bundled RVC worker with a synthetic random-weight model to verify its complete inference path; this is a compatibility test, not a voice-quality assessment. A separate local test uses the user's RVC checkpoint without publishing it.

Original integration and UI code: AGPL-3.0-only. See `THIRD_PARTY.md` for dependency and weight provenance.
