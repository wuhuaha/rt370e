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
- current-chunk size: `512` samples
- rolling context: `64` samples
- actual model input tensor: `64 + 512 = 576` samples
- recurrent state shape: `2 x batch x 128`
- output: speech probability per chunk

## Project Adaptation

- `river_voice_preproc` currently emits `256` samples every `16 ms`.
- `river_voice_detector_silero` therefore stages the model boundary as:
  - `2 x 256-sample frames -> 1 x 512-sample logical VAD window`
  - backend-managed `64-sample` rolling context
  - backend-managed `576-sample` real model input tensor
  - backend-managed recurrent state

## Source Hygiene

- This vendored ONNX file is the immutable project-side baseline.
- Host-side conversion tooling must never write back into this path.
- Always stage a temporary copy before conversion, for example:
  ```bash
  cd /root/ameba-river
  source /root/ameba-river/.venv-silero-convert/bin/activate
  python tools/silero_vad/stage_conversion_source.py \
    --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
    --output /tmp/silero_vad_16k_op15.stage.onnx
  ```

## Next Step

- stop treating `512` as the real model input size; the real ONNX model input is `576`
- evaluate a reconstruction path from the official model structure if direct `onnx2tf` remains unstable
- add the exact host-side export / conversion command
- measure tensor arena, heap pressure, flash growth, and per-window latency before considering compression
