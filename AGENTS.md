# Ameba River Agent Rules

## Scope
- This repository is an external Ameba RTOS project for `RTL8730E`.
- Do not modify SDK sources under `/root/ameba-rtos-1.2` unless the user explicitly asks.
- Keep all project-specific code, docs, and process records inside this repository.

## Delivery Mode
- Work in small, board-verifiable steps.
- After each step:
  1. update `.codex/changes.md`
  2. update `.codex/verification.md` with the exact build/run check for the user
  3. commit the step with a focused git message

## User-Owned Review Files
- Treat these files as user-owned review inputs, not developer worklogs:
  - `TIPS.md`
  - `REVIEW.md`
  - `.codex/issues.md`
- Do not proactively edit `TIPS.md` or `REVIEW.md`.
- Do not use `.codex/issues.md` as a place to add new analysis, new risks, or implementation notes.
- Use those files as read-mostly inputs:
  - read them when troubleshooting
  - selectively adopt reasonable suggestions into code
  - mention adopted suggestions in normal code/docs/commit history outside those files
- `.codex/issues.md` may be updated only to minimally mark an existing issue as resolved after the fix is actually implemented and verified.
- If a new long-term rule or collaboration constraint needs to persist across turns, store it in `AGENTS.md` or another developer-owned project document, not in the user-owned review files.

## Architecture Guardrails
- Keep these layers separate:
  - `app/`: Ameba entrypoint only
  - `components/river_core`: orchestration, state, routing
  - `components/river_voice`: local VAD / wake word / future offline ASR adapters
  - `components/river_cloud`: online dialogue and home-control adapters
  - `components/river_diag`: debug and monitor commands
- New online features must go behind stable interfaces first, then bind to a concrete provider.
- Local speech capability must be integrated through `river_voice_*` interfaces rather than directly from `app/`.

## Coding Style
- Prefer simple C over macro-heavy abstractions.
- Keep headers in `include/river/`.
- Keep platform-specific code replaceable.
- Favor deterministic logs and explicit error returns.

## Current Objective
- Phase 1: bootable project with monitor-based echo and simulated device control.
- Future phases: real online control transport, local VAD, wake word, offline ASR, and online/offline fusion.
