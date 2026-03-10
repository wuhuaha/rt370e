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
Owns local front-end interfaces. Today it only reports configured mode. Later it will host VAD, wake word, and offline ASR adapters.

### `components/river_cloud`
Owns online control transport. Today it is a stub that echoes text and exposes a simulated device-control path. Later it will contain the real HTTP/WebSocket or vendor SDK integration.

### `components/river_diag`
Owns monitor commands so every phase can be tested without full voice input.

## Reserved interfaces
- `river_voice_frontend_*`: local voice event boundary
- `river_cloud_adapter_*`: online provider boundary
- `river_online_control_*`: home-control service boundary

These names should remain stable unless there is a strong reason to refactor.
