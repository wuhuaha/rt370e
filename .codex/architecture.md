# Architecture Notes

## Why this structure
The current product direction is narrower and stricter than the initial exploration:
- `ASR-first`
- wake word and ASR quality first
- online home control still remains the business target
- `AEC` and `VAD` must remain replaceable by self-developed models later

If online control logic is written directly inside `app_example()`, later integration will become tangled around audio callbacks, cloud transport, and device state. This repository starts with explicit boundaries so the first iteration stays small without blocking later growth.

## Layers
### `app/`
Only owns the Ameba entrypoint and boot sequence handoff.

### `components/river_core`
Owns runtime boot, shared status, and business-level routing.

### `components/river_voice`
Owns local front-end interfaces. It now contains:
- `river_voice_capture`: raw microphone-array acquisition
- `river_voice_preproc`: enhancement boundary
- `river_voice_echo`: board-level debug sink on top of the front-end
- future `Silero VAD`, wake word, and self-developed model adapters

### `components/river_cloud`
Owns online control transport. Today it is a stub that echoes text and exposes a simulated device-control path. Later it will contain the real HTTP/WebSocket or vendor SDK integration.

### `components/river_diag`
Owns monitor commands so every phase can be tested without full voice input.

## Reserved interfaces
- `river_voice_frontend_*`: local voice event boundary
- `river_voice_capture_*`: raw array capture boundary
- `river_voice_preproc_*`: enhancement / AFE / future self-developed DSP boundary
- `river_voice_echo_*`: board audio bring-up and delayed debug replay boundary
- `river_cloud_adapter_*`: online provider boundary
- `river_online_control_*`: home-control service boundary

These names should remain stable unless there is a strong reason to refactor.

## Voice Pipeline Direction
The intended steady-state pipeline is:

`capture -> preproc -> detector -> router`

Where:
- `capture` owns board routing and frame cadence only
- `preproc` owns AFE, AEC, beamforming, AGC, NS, or future self-developed DSP
- `detector` owns VAD and later wake word
- `router` decides whether audio or events go to debug replay, cloud, or local recognition

This keeps the current SDK-backed step and the future self-developed step aligned:
- today: `preproc = aivoice_afe`
- later: `preproc = self_dsp` or `self_tflite`
- today: `detector = silero_vad`
- later: `detector = self_vad` or `self_kws`

## Current RTL8730E Policy
- Frame cadence is fixed to `16 ms` because SDK AIVoice AFE requires `256 samples @ 16 kHz`.
- Board geometry is fixed to `AMIC1 + AMIC3`, `2mic50mm`, matching the SDK `speechmind` / `aivoice` baseline for `AmebaSmart`.
- The voice preproc layer now exposes two explicit product-facing profiles:
  - `asr_mainline`
  - `asr_barge_in_aec`
- The active default profile is currently `asr_barge_in_aec`, but it still stays on the `AFE_FOR_ASR` side rather than switching the whole product to a communication profile.
- `AEC` is now a priority feature, but it must remain an adapter behind `river_voice_preproc_*` so the SDK backend can be replaced later.
- The playback reference remains a project-owned component because future self-developed `AEC` also needs the same `mic + ref` boundary.
- `VAD` must not be tied to SDK `aivoice`; the target direction is `Silero VAD` first, then future self-developed VAD on the same detector interface.
- `Silero VAD` is now staged behind `river_voice_detector_*`; later self-developed VAD must replace that detector backend, not modify `app`, `echo`, or `preproc`.
- The project now pins an exact official upstream detector source artifact:
  - `third_party/silero_vad/upstream/silero_vad_16k_op15.onnx`
  This is the only approved starting point for the first embedded conversion path.
- The detector contract now distinguishes two different sizes explicitly:
  - logical VAD chunk: `512` samples
  - real official model input tensor: `576` samples
  This is required because the official wrapper prepends `64` rolling-context samples before inference.
- Direct `onnx2tf` on the pinned `op15` graph is currently treated as an investigation path, not a stable production export path.
  The next preferred migration step is to reconstruct the official published network structure in host-side `Keras` from:
  - `tinygrad_model.py`
  - the pinned ONNX weights and constants
- The vendored ONNX source artifact must now be treated as immutable project input.
  - Host-side conversion tools are required to run against a staged copy under `/tmp`.
  - This prevents future graph-repair experiments from silently corrupting the pinned upstream baseline.
- Reconstruction will no longer start from opaque ONNX conversion guesses alone.
  - The project now has a dedicated extractor that derives the decoder `LSTM` tensors exactly as the ONNX graph feeds them.
  - The next `Keras` step should use that derived tensor map instead of re-slicing weights ad hoc.
- That reconstruction step is now proven for the first embedded target:
  - a batch=`1` `TFLite` artifact exists
  - it stays numerically aligned with the pinned ONNX baseline
  - the next architecture step is runtime integration, not another export experiment
- That runtime step is now also landed:
  - `river_voice_detector_silero.cc` embeds and executes the verified `TFLite` artifact through `TFLite Micro`
  - detector feed stays on enhanced mono output, not on raw array PCM
  - detector is intentionally observational first:
    - no replay gating
    - no router event emission yet
    - only diagnostics and runtime validation
- The current detector tuning knobs are project-owned config, not hard-coded model policy:
  - `CONFIG_RIVER_SILERO_VAD_TENSOR_ARENA_KB`
  - `CONFIG_RIVER_SILERO_VAD_SPEECH_THRESHOLD_Q15`
  This keeps later self-developed VAD backends aligned with the same detector boundary.
- The old direct speaker self-test path has been removed from the mainline codebase because it was only a bring-up tool, not part of the final product architecture.
- The current echo path is no longer the architecture center; it is only the first debug consumer of the reusable front-end.
## Current Online-ASR Preparation State

- default validation path:
  - `capture -> preproc(asr_mainline) -> detector(silero + sdk_vad_ref) -> segment_buffer -> segment_sink -> diagnostics`
- current intention:
  - keep `echo` only as a board test path
  - keep `vad_probe` as the main speech-segmentation validation path
  - use `segment_buffer + segment_sink` as the future handoff point into online ASR transport
- current buffering policy:
  - detector decisions stay frame-local
  - speech segments are reconstructed outside the detector using:
    - pre-roll
    - post-roll
    - max-segment clamp
- why this boundary matters:
  - later replacing `Silero` with a self-developed VAD should not change online ASR upload logic
  - later replacing `aivoice_afe` with a self-developed front-end should not change segment buffering logic
