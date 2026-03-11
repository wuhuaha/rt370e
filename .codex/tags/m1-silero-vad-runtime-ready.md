# Tag: `m1-silero-vad-runtime-ready`

## Milestone
- `RTL8730E` board-side `Silero VAD` runtime is integrated and reaches `runtime ready`.
- The original official `Silero VAD` model is migrated first in `Float32` form.
- The detector now runs on-device with `TFLite Micro` and produces live speech/silence decisions.

## Achieved Effect
- Boot log reaches:
  - `silero_vad runtime ready: model=silero_vad_16k_b1_fp32.tflite arena=256KB used=101264B threshold_q15=16384`
- Detector is active in the live audio path:
  - `local_detector=silero_vad`
  - `det_ok` increases
  - `det_fail=0`
  - `vad_prob_q15` and `vad=speech|silence` change with input speech
- Current measured detector baseline:
  - tensor arena configured: `256KB`
  - tensor arena used at runtime: about `101264B`
- The board keeps running with:
  - `AIVoice AFE`
  - `Silero VAD`
  - audio echo debug path
  - Wi-Fi initialization

## Key Changes Included In This Tag
- Added `Silero VAD` migration pipeline:
  - pinned official upstream model
  - reproducible ONNX -> TF/TFLite conversion flow
  - generated embedded model data for firmware integration
- Added on-device detector runtime:
  - `river_voice_detector_silero.cc`
  - `TFLite Micro` interpreter setup
  - fixed op resolver
  - recurrent-state management
  - rolling `512 + 64` sample input assembly
- Added board-side diagnostics:
  - `vad_prob_q15`
  - `vad=speech|silence`
  - detector success/failure counters
- Fixed multiple `RTL8730E SDK TFLite Micro` compatibility issues:
  - C++ object lifetime in runtime context
  - top-level tensor order assumptions
  - missing or degraded tensor buffer views
  - optional eval-tensor policy
  - `kTfLiteNoType` compatibility on top-level tensors
- Added project-owned NOR flash profile for oversized development images:
  - custom `.rdev`
  - project flash helper

## Verification Summary
- Board-side verification confirms:
  - boot reaches `silero_vad runtime ready`
  - detector opens successfully
  - detector emits changing speech probabilities
  - detector transitions between `silence` and `speech`
- Example observed runtime log characteristics:
  - `det_ok=62`
  - `det_fail=0`
  - `vad_prob_q15` ranges from low values to near full-scale on speech

## Known Limitations At This Tag
- `Silero VAD` is integrated and running, but policy tuning is still basic.
- Echo debug playback still shows saturation at times and is not a final ASR quality metric.
- `AEC` and `ASR-first` profile coexist, but the final ASR control policy is not finished.
- No compression work is included yet:
  - no quantization
  - no pruning
- `Silero VAD` has not yet been wrapped with full production-grade hysteresis / hangover logic.

## Why This Tag Matters
- This is the first stable point where the project can move from:
  - model/runtime bring-up
  - SDK compatibility debugging
- to:
  - VAD decision policy tuning
  - ASR gating
  - latency / stack / cache instrumentation
  - later custom-model replacement work

## Suggested Next Steps After This Tag
- Add `Silero VAD` decision smoothing and hangover policy.
- Add `Invoke()` latency and task stack watermark instrumentation.
- Reduce debug replay saturation so front-end behavior is easier to judge.
- Start wiring VAD output into the real ASR control path.
