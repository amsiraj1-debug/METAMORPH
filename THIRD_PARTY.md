# Third-party components

- JUCE 8.0.12, commit `29396c22c93392d6738e021b83196283d6e4d850`, AGPLv3/commercial dual license. This project uses the AGPLv3 option.
- RVC-Project/Retrieval-based-Voice-Conversion-WebUI, commit `81eed5e8f68b6bed1789f682fe78cdd324495afc`, MIT. The required source subset and its original `rvc/LICENSE` are included. RMVPE incorporates upstream Apache-2.0 work; upstream notices remain in source.
- Inference integration informed by the user's installed VocalMorph distribution, copyright 2026 VocalMorph contributors, AGPL-3.0-only. No native VocalMorph DSP library is required.
- Shared RMVPE and HuBERT/ContentVec weights come from https://huggingface.co/lj1995/VoiceConversionWebUI (repository license MIT). Exact immutable revisions and binary checksums are in `tools/package_windows.py`, and packaging preserves a weights notice. No target voice checkpoint is included.
- CPython 3.12.10: PSF license; its embedded distribution retains LICENSE.txt. PyTorch: BSD-style. Transformers: Apache-2.0. NumPy/SciPy: BSD. librosa: ISC. SoundFile: BSD. Dependency license metadata is preserved in the packaged runtime.
- VST3 technology by Steinberg; JUCE retains its SDK notices.

The public rebuild does not contain the original protected VST3, its PACE/iLok code, proprietary factory models, or extracted branding/artwork. Local recovered resources retain their original rights and are not relicensed by this project.
