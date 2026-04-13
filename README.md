# ameba-river

External Ameba RTOS project for `RTL8730E`, focused on a board-side voice pipeline for wake word, VAD, cloud interaction, and smart-home control.

## Start Here

Canonical entry points for Codex and human collaborators:

- [AGENTS.md](/root/ameba-river/AGENTS.md)
- [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- [build.md](/root/ameba-river/build.md)
- [doc/README.md](/root/ameba-river/doc/README.md)

After changing repo-level harness files, run:

```bash
cd /root/ameba-river
python3 tools/diag/check_codex_harness.py
```

Notes:

- `README.md` and `build.md` are kept branch-agnostic on purpose.
- Active branch, current objective, and the latest verified step live in
  `.codex/active_context.md`.
- Historical design notes, status snapshots, and dated investigations live
  under `doc/`.

## Build

```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; source env.sh; python3 /root/ameba-rtos/ameba.py build -p'
```

The default SDK baseline is `/root/ameba-rtos`. `env.sh` also honors
`AMEBA_SDK_ROOT` if a task explicitly needs another checkout.

See [build.md](/root/ameba-river/build.md) for the stable build, flash, and
monitor commands.

## Flash

Use the project flash path, not the SDK stock `flash` command:

```bash
cd /root/ameba-river
bash -lc 'export AMEBA_SDK_ROOT=/root/ameba-rtos; python3 tools/river_flash.py -p /dev/ttyUSB0 -b 1500000 -m nor'
```

Important note:
- continue using the project custom flash profile under
  `board/rtl8730e/profiles/RTL8730E_NOR.rdev`

## Repository Layout

- `app/`: Ameba entrypoint only
- `components/river_core`: orchestration and runtime state
- `components/river_voice`: capture, VAD, KWS, and future local speech modules
- `components/river_cloud`: XiaoZhi / cloud transport and online integrations
- `components/river_diag`: monitor commands and diagnostics

User-owned review inputs:

- `TIPS.md`
- `REVIEW.md`

Process records:

- [.codex/changes.md](/root/ameba-river/.codex/changes.md)
- [.codex/verification.md](/root/ameba-river/.codex/verification.md)

## Recommended Reading

- [.codex/active_context.md](/root/ameba-river/.codex/active_context.md)
- [build.md](/root/ameba-river/build.md)
- [doc/PROJECT_STATUS_ZH.md](/root/ameba-river/doc/PROJECT_STATUS_ZH.md)
- [doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md](/root/ameba-river/doc/RIVER_OPENWAKEWORD_LAB_MIGRATION_REPORT_ZH.md)
- [doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md](/root/ameba-river/doc/DSCNN_KWS_TRAINING_PRO_MIGRATION_REPORT_ZH.md)
- [doc/VOICE_FRONTEND_CHAIN_STATUS_ZH.md](/root/ameba-river/doc/VOICE_FRONTEND_CHAIN_STATUS_ZH.md)
