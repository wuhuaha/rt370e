# ameba-river

External Ameba RTOS project for `RTL8730E`, focused on a voice-interaction stack for a smart home control panel.

## Current Baseline

Stable ASR baseline tag:
- `m3-asr-baseline-fixed-dsb`

Stable runtime chain:
- `capture -> fixed_dsb -> silero_vad -> streaming asr`

Current TTS path:
- `river tts <text> -> iflytek ws tts -> PlaybackService -> speaker`

Current AEC experiment branch:
- `debug/webrtc-aec`

Current experimental chain:
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

## Key Documents

- `PROJECT_STATUS_ZH.md`
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
