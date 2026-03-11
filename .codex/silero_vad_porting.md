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
   - host environment:
     - dedicated venv: `/root/ameba-river/.venv-silero-convert`
     - interpreter: `Python 3.10.12`
   - tool versions:
     - `onnx==1.17.0`
     - `onnxruntime==1.20.1`
     - `onnxsim==0.4.36`
     - `onnxoptimizer==0.3.13`
     - `onnx-graphsurgeon==0.5.8`
     - `onnx2tf==1.28.3`
     - `tensorflow==2.19.1`
     - `tensorflow-cpu==2.19.0`
     - `tf_keras==2.19.0`
   - export command:
     - first direct probe:
       ```bash
       source /root/ameba-river/.venv-silero-convert/bin/activate
       onnx2tf \
         -i /root/ameba-river/third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
         -o /tmp/silero_vad_16k_op15_tflite_576 \
         -b 1 \
         -ois input:1,576 state:2,1,128 \
         -coion
       ```
   - post-processing command:
     - manual parameter-replacement probe also tested:
       - `wa/model/stft/padding/Transpose perm -> [0,1]`
       - `wa/model/stft/Unsqueeze_output_0 post transpose -> [0,2,1]`
   - current export status:
     - direct `onnx2tf` is not yet stable for this graph
     - failure point `1`: `wa/model/stft/Conv`
     - failure point `2` after manual fix: `wa/model/decoder/Squeeze`
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
     - assemble official model input as `64-sample context + 512-sample current window = 576 samples`
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
  - `512-sample` logical decision window
  - `64-sample` rolling context
  - `576-sample` real model input tensor
  - recurrent state `2 x batch x 128`

## Step 2 Host Conversion Bring-Up
- Built a dedicated host-side conversion environment instead of reusing the SDK venv:
  ```bash
  python3.10 -m venv /root/ameba-river/.venv-silero-convert
  /root/ameba-river/.venv-silero-convert/bin/pip install --upgrade \
    pip setuptools wheel \
    onnx==1.17.0 onnxruntime==1.20.1 onnxsim==0.4.36 onnxoptimizer==0.3.13 \
    onnx-graphsurgeon==0.5.8 sng4onnx==1.0.4 \
    tensorflow-cpu==2.19.0 tensorflow==2.19.1 tf_keras==2.19.0 \
    onnx2tf==1.28.3 ai_edge_litert==1.2.0 \
    psutil==6.1.1 h5py==3.12.1 protobuf==5.29.3 flatbuffers==25.1.24 ml_dtypes==0.5.1
  ```
- Confirmed the official ONNX runtime I/O contract:
  - inputs:
    - `input`: `[batch, sequence]`
    - `state`: `[2, batch, 128]`
    - `sr`: scalar `int64`
  - outputs:
    - `output`: `[batch, 1]`
    - `stateN`: `[2, batch, 128]` at runtime
- Corrected an earlier staging mistake:
  - `512` is only the current chunk size
  - the real model input is `576` because official wrapper prepends `64` context samples before inference
- Direct `onnx2tf` result on `2026-03-11`:
  - probe command with `input:1,576 state:2,1,128`
  - failed at `wa/model/stft/Conv`
- Manual parameter-replacement probe result on `2026-03-11`:
  - fixed `STFT` conversion enough to pass the first `Conv`
  - next failure moved to `wa/model/decoder/Squeeze`
- Current decision:
  - stop assuming the old ONNX graph can be pushed through `onnx2tf` without graph-specific repair
  - next implementation path should reconstruct the official network from the published `tinygrad` skeleton and ONNX-extracted weights
- Source hygiene correction on `2026-03-11`:
  - the vendored ONNX was found to have been modified in place by local host-side tooling during earlier experiments
  - the repository copy must remain an immutable official source artifact
  - all future conversion experiments must stage a temporary copy first
  - staging helper:
    ```bash
    cd /root/ameba-river
    source /root/ameba-river/.venv-silero-convert/bin/activate
    python tools/silero_vad/stage_conversion_source.py \
      --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
      --output /tmp/silero_vad_16k_op15.stage.onnx
    ```
- Reconstruction scaffolding on `2026-03-11`:
  - added `tools/silero_vad/extract_reconstruction_tensors.py`
  - this extractor emits:
    - direct source tensor summaries
    - decoder `LSTM` tensors after the ONNX slice/concat graph layout
  - confirmed from the restored official ONNX that:
    - `model.decoder.rnn.weight_ih`
    - `model.decoder.rnn.weight_hh`
    - `model.decoder.rnn.bias_ih`
    - `model.decoder.rnn.bias_hh`
    live as top-level initializers in the canonical upstream artifact
  - first reproducible invocation:
    ```bash
    cd /root/ameba-river
    source /root/ameba-river/.venv-silero-convert/bin/activate
    python tools/silero_vad/extract_reconstruction_tensors.py \
      --input third_party/silero_vad/upstream/silero_vad_16k_op15.onnx \
      --output third_party/silero_vad/upstream/silero_vad_16k_op15_reconstruction_manifest.json
    ```
- Direct rebuild result on `2026-03-11`:
  - added `tools/silero_vad/rebuild_tf_silero_vad.py`
  - reconstruction strategy:
    - `STFT` and encoder convs rebuilt in `TensorFlow`
    - decoder `LSTM` rebuilt with an explicit ONNX-style gate implementation instead of assuming framework gate order
    - batch fixed to `1` for the first embedded artifact
  - verification against official ONNX:
    - output max abs diff `1.5599653124809265e-08`
    - state max abs diff `1.6689300537109375e-06`
  - generated artifact:
    - `/root/ameba-river/third_party/silero_vad/generated/silero_vad_16k_b1_fp32.tflite`
    - sha256 `5a532943646b1dd71930fb02e26e0600ba97ee80990302726294aef8a3142a05`
    - size `1248388` bytes
