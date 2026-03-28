# ameba-river

External Ameba RTOS project for `RTL8730E`, focused on a board-side voice pipeline for wake word, VAD, cloud interaction, and smart-home control.

## Current Status

Current working branch:
- `DS-CNN`

Current engineering objective:
- keep the branch `build-stable`
- verify the board runtime with the newly landed experimental `DS-CNN` wake model
- keep the target business flow as `wake word -> XiaoZhi realtime session`, with VAD-assisted audio uplink
- do board smoke before any further model iteration

Current default local wake path on this branch:
- `capture -> fixed_dsb -> silero_vad(gate) -> dscnn_kws`

Current KWS runtime state:
- baseline model fallback is still retained
- the default experimental model variant is `round6_targeted_experimental`
- `PAD` op support has already been added to the board TFLM resolver for this model
- local interaction debug router and direct cloud text/TTS debug injection are now compile-time gated and disabled by default on this branch

Externalized workspaces:
- training / TTS / host recording lab assets are maintained in `/root/river-openwakeword-lab`
- repo-side paths such as `tools/openwakeword`, `tools/tts`, `tools/recording_lab`, `artifacts/openwakeword`, and `artifacts/recording_lab` are preserved as symlinks for compatibility

## Build

```bash
cd /root/ameba-river
source env.sh
python3 /root/ameba-rtos-1.2/ameba.py build -p
```

Latest verified output sizes on this branch:
- `build_RTL8730E/km4_boot_all.bin` = `51872`
- `build_RTL8730E/km0_km4_ca32_app.bin` = `3564896`
- `build_RTL8730E/ota_all.bin` = `3564928`

See [build.md](/root/ameba-river/build.md) for the validated build and flash path.

## Flash

Use the project flash path, not the SDK stock `flash` command:

```bash
cd /root/ameba-river
source env.sh
python3 tools/river_flash.py -p /dev/ttyUSB0
```

Important note:
- the current app image still exceeds the SDK stock `RTL8730E` app window
- continue using the project custom flash profile under `board/rtl8730e/profiles/RTL8730E_NOR.rdev`

## Current Validation Focus

Board smoke target for this branch:
- boot the board successfully
- confirm startup log contains `variant=round6_targeted_experimental`
- confirm status logs show `interaction_diag=compiled=no`
- confirm local `VAD + KWS` chain starts normally
- confirm wakeup can enter the XiaoZhi realtime conversation window with VAD-assisted audio flow
- then decide whether the experimental runtime should remain the default lab profile

## Repository Entry Files

Files kept at repository root for ongoing work:
- `README.md`
- `plan.md`
- `build.md`
- `AGENTS.md`
- `TIPS.md`
- `REVIEW.md`

Summary / report / design notes have been moved into [doc/README.md](/root/ameba-river/doc/README.md).

## Recommended Reading

- [plan.md](/root/ameba-river/plan.md)
- [build.md](/root/ameba-river/build.md)
- [doc/PROJECT_STATUS_ZH.md](/root/ameba-river/doc/PROJECT_STATUS_ZH.md)
- [doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md](/root/ameba-river/doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md)
- [doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md](/root/ameba-river/doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md)
- [doc/VOICE_FRONTEND_CHAIN_STATUS_ZH.md](/root/ameba-river/doc/VOICE_FRONTEND_CHAIN_STATUS_ZH.md)

Process records are maintained under `.codex/`.
