# Architecture Notes

## Why this structure
The first release only needs online home control, but the product target is a voice appliance with:
- online dialogue
- local VAD or VAD + wake word
- future offline recognition
- future online/offline fusion

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
- future `VAD`, wake word, and offline ASR adapters

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

## Current RTL8730E Policy
- Frame cadence is fixed to `16 ms` because SDK AIVoice AFE requires `256 samples @ 16 kHz`.
- Board geometry is fixed to `AMIC1 + AMIC3`, `2mic50mm`, matching the SDK `speechmind` / `aivoice` baseline for `AmebaSmart`.
- The active default preproc policy is now `ASR-first`, not communication-first:
  - `AFE_FOR_ASR`
  - `SSL on`
  - `NS off`
  - fixed AGC
- The playback reference remains a project-owned component, but it is now staged infrastructure rather than part of the default runtime path.
- Future `AEC/barge-in` should be implemented as an optional profile on the same `mic + ref` boundary, not as the only front-end strategy.
- The current echo path is no longer the architecture center; it is only the first debug consumer of the reusable front-end.
