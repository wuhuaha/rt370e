# Silero VAD Upstream Metadata

- repository: `https://github.com/snakers4/silero-vad`
- pinned commit: `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`
- vendored date: `2026-03-11`
- license file: `LICENSE.silero_vad`

## Chosen Source Artifact

- file: `silero_vad_16k_op15.onnx`
- sha256: `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`
- size: `1289603` bytes

## Why This File

- `ameba-river` is fixed to `16 kHz`, so the 16k-only official artifact is a better conversion source than the generic dual-rate ONNX.
- It is materially smaller than `silero_vad.onnx`.
- It is still an official upstream file, so future regeneration does not depend on third-party forks.
- `op15` is selected as the initial conversion source because it is a simpler compatibility baseline than the newer `op18` branch for embedded conversion tooling.

## Official Streaming Contract

These rules are derived from upstream `src/silero_vad/utils_vad.py` at the pinned commit:

- sample rate: `16000`
- model input chunk: `512` samples
- rolling context: `64` samples
- recurrent state shape: `2 x batch x 128`
- output: speech probability per chunk

## Project Adaptation

- `river_voice_preproc` currently emits `256` samples every `16 ms`.
- `river_voice_detector_silero` therefore stages the model boundary as:
  - `2 x 256-sample frames -> 1 x 512-sample Silero window`
  - backend-managed `64-sample` context
  - backend-managed recurrent state

## Next Step

- import the real model into the detector runtime
- add the exact host-side export / conversion command
- measure tensor arena, heap pressure, flash growth, and per-window latency before considering compression
