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
- The old direct speaker self-test path has been removed from the mainline codebase because it was only a bring-up tool, not part of the final product architecture.
- The current echo path is no longer the architecture center; it is only the first debug consumer of the reusable front-end.
