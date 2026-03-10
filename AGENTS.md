# Ameba River Agent Rules

## Scope
- This repository is an external Ameba RTOS project for `RTL8730E`.
- Do not modify SDK sources under `/root/ameba-rtos-1.2` unless the user explicitly asks.
- Keep all project-specific code, docs, and process records inside this repository.

## Delivery Mode
- Work in small, board-verifiable steps.
- After each step:
  1. update `.codex/changes.md`
  2. update `.codex/issues.md` if new risks appear
  3. update `.codex/verification.md` with the exact build/run check for the user
  4. commit the step with a focused git message

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
