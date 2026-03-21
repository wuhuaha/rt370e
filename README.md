# ameba-river

External Ameba RTOS project for `RTL8730E`, focused on a voice-interaction stack for a smart home control panel.

## Current Baseline

Stable ASR baseline tag:
- `m3-asr-baseline-fixed-dsb`

Current flashable full-duplex baseline tag:
- `m4-full-duplex-bargein-stable`

Current working branch:
- `xiaozhi`

Stable runtime chain:
- `capture -> fixed_dsb -> silero_vad -> streaming asr`

Current flashable cloud-interaction baseline:
- `streaming asr: iflytek_rtasr`
- `prompt/debug tts: iflytek ws tts`
- `runtime: PlaybackService + ReferenceService + InteractionState + barge-in`

Current XiaoZhi branch status:
- `XiaoZhi` realtime websocket session layer has landed on this branch
- websocket `hello / listen / abort / stt / tts / llm / mcp` text protocol is implemented
- `Opus` uplink/downlink is wired into the existing `PlaybackService / InteractionState / frame-ring` runtime
- playback-time barge-in now interrupts local playback first and propagates upstream `abort`
- the current Iflytek split `ASR + TTS` path remains as the fallback baseline

Preserved AEC experiment assets:
- `debug/webrtc-aec`
- `capture(3ch: mic0 + mic1 + ref) -> webrtc_aecm(experimental) -> fixed_dsb -> silero_vad -> streaming asr`

The experimental AEC path is isolated behind an explicit profile and must not silently replace the stable `fixed_dsb` product path.

## Build

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Generated images:
- `build_RTL8730E/build/project_hp/image/km4_boot_all.bin`
- `build_RTL8730E/build/project_hp/image/km0_km4_ca32_app.bin`

## XiaoZhi Debug Commands

Board-side diagnostic entrypoints:

```text
river xiaozhi status
river xiaozhi ota <ota_url>
river xiaozhi bootstrap
river xiaozhi set <ws_url> [token|-]
river xiaozhi protocol <2|3>
river xiaozhi mcp <on|off>
river xiaozhi connect
river xiaozhi disconnect
river xiaozhi listen start
river xiaozhi listen stop
river xiaozhi listen detect <text>
river xiaozhi abort [reason]
```

Recommended first-pass validation:
- prefer configuring XiaoZhi OTA first, then run `river xiaozhi bootstrap`
- verify `river xiaozhi status` shows `ota_set=yes` and, when provided by server, `activation_code=xxxxxx`
- use `river xiaozhi set <ws_url> [token|-]` only as a manual fallback path
- confirm `hello` completes and the session stays open
- verify uplink speech produces `stt partial/final`
- verify downlink `tts start/stop/sentence_start` reaches `PlaybackService`
- verify playback-time speech triggers local interrupt plus upstream `abort`
- verify `mcp` can drive `light / fan / curtain / socket`

## Key Documents

- `PROJECT_STATUS_ZH.md`
- `XIAOZHI_REALTIME_INTERACTION_ARCHITECTURE_ZH.md`
- `XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
- `IFLYTEK_TTS_WS_INTEGRATION_ZH.md`
- `VOICE_FRONTEND_CHAIN_STATUS_ZH.md`
- `AEC_DEBUG_PLAN_GUIDE_ZH.md`
- `VOICE_INTERACTION_REFACTOR_PROPOSAL_ZH.md`
- `WAKE_ASR_AUDIO_PROFILE_DESIGN_ZH.md`
- `FIXED_DSB_BEAMFORMING_ZH.md`
- `AEC_REFERENCE_PATH_COMPARISON_ZH.md`
- `WEBRTC_AECM_RIVER_接入记录.md`
- `ARCHITECTURE_OPTIMIZATION_ZH.md`

Process records are maintained under `.codex/`.
