# Silero VAD Porting Record

## Purpose
This document is the single reproducibility record for migrating `Silero VAD` into `ameba-river`.

It must be updated during every migration step so the port can be rebuilt later without relying on memory.

## Scope
- target board: `RTL8730E`
- product direction: `ASR-first`
- detector boundary:
  - input: enhanced mono PCM from `river_voice_preproc`
  - output: stable `VAD` events for later `KWS/ASR`
- backend policy:
  - first bring-up can use a third-party model
  - final architecture must allow replacement with a self-developed model

## Repro Checklist
1. Upstream source
   - repository: `https://github.com/snakers4/silero-vad`
   - tag / commit: `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`
   - local archive checksum:
     - `silero_vad_16k_op15.onnx`: `7ed98ddbad84ccac4cd0aeb3099049280713df825c610a8ed34543318f1b2c49`
     - vendored path: `/root/ameba-river/third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
2. Model choice
   - exact model file: official `silero_vad_16k_op15.onnx`, uncompressed
   - input sample rate: `16 kHz`
   - required frame / hop:
     - integration target in `ameba-river`: `256-sample` feed from `preproc`
     - official detector window: `512 samples / 32 ms`
     - official rolling context: `64 samples / 4 ms`
   - input normalization rule:
     - upstream wrapper consumes float waveform samples from `torchaudio.load`
     - inference: expected input range is normalized PCM `[-1.0, 1.0]`
   - output semantics: speech probability + stable speech / silence decision
3. Conversion / export
   - host environment: pending actual import
   - tool versions: pending actual import
   - export command: pending actual import
   - post-processing command: pending actual import
   - generated artifact checksum: pending actual import
4. Embedded runtime choice
   - runtime backend: `TensorFlow Lite Micro`
   - why selected:
     - `RTL8730E` SDK already ships `tflite_micro`
     - keeps the detector runtime replaceable from `aivoice`
     - aligns with the later self-developed `TFLite` model direction
   - expected RAM: measure after the first real model import
   - expected flash: measure after the first real model import
   - expected per-frame latency: measure after the first real model import
5. Integration boundary
   - detector API file: `/root/ameba-river/include/river/river_voice_detector.h`
   - preproc output format: enhanced mono `PCM16`, `16 kHz`, `256 samples / 16 ms`
   - buffering strategy:
     - keep `preproc -> detector` at `16 ms`
     - stage `Silero` ingestion as `2 x 16 ms -> 32 ms`
   - timestamp strategy: carry frame cadence from `preproc`; add sample-accurate detector timestamps when the real backend lands
6. Validation
   - near-field test result:
   - far-field test result:
   - non-speech false trigger result:
   - continuous speech segmentation result:
7. Issues
   - unresolved problem:
   - workaround:
   - next action:

## Rules
- Every import, conversion, and threshold decision must be recorded here.
- If a temporary script is used, its path and invocation must be recorded here.
- If a model file is regenerated, checksum and command must be updated here immediately.
- This file is required for future re-porting and for later replacement by a self-developed `VAD`.

## Step 0 Decision
- Do not start with pruning or quantization.
- First complete a direct migration of the original `Silero VAD` model so:
  - accuracy regressions are not mixed with conversion-side regressions
  - `RTL8730E` memory, flash, and latency can be measured on real firmware
  - later compression decisions can be evidence-based instead of assumed
- Compression is only triggered if at least one of the following becomes true:
  - flash growth is no longer acceptable for the product image budget
  - runtime heap / arena pressure threatens `Wi-Fi + AEC + KWS/ASR`
  - detector latency becomes too high for wake-word / ASR segmentation
- This project's first migration step is therefore:
  - create a stable detector boundary
  - stage `Silero VAD` behind that boundary
  - keep `AEC` in `preproc`
  - defer model compression until profiling says it is necessary

## Step 1 Import Baseline
- Official upstream is now pinned locally at commit `0dd0d85ee86b1f9d178dc26a04e60e90de26a80f`.
- The first conversion input is fixed to:
  - `/root/ameba-river/third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
- Why this file:
  - `RTL8730E` project path is fixed to `16 kHz`
  - this file is smaller than the generic official ONNX
  - it stays inside official upstream rather than relying on third-party exports
- The staged detector boundary must honor the official streaming contract:
  - `512-sample` model window
  - `64-sample` rolling context
  - recurrent state `2 x batch x 128`
