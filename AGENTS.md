# Ameba River Agent Rules

## Scope
- This repository is an external Ameba RTOS project for `RTL8730E`.
- Default SDK baseline is the latest SDK checkout at `/root/ameba-rtos`.
- Unless the user explicitly asks otherwise, use `/root/ameba-rtos` for build, flash, runtime validation, SDK capability checks, and future wakeword-model bring-up work.
- Do not modify SDK sources under `/root/ameba-rtos-1.2` unless the user explicitly asks.
- Keep all project-specific code, docs, and process records inside this repository.

## Delivery Mode
- Work in small, board-verifiable steps.
- After each step:
  1. update `.codex/changes.md`
  2. update `.codex/verification.md` with the exact build/run check for the user
  3. commit the step with a focused git message
- Unless the user explicitly asks otherwise, use clear Chinese commit messages
  for future `git commit` operations in this repository.

## Hardware Flashing Policy
- The current NAND hardware requires manually entering flashing/download mode.
- Unless the user explicitly asks in the current turn, do not run flash tools or
  serial runtime monitors against the board; after a successful build, report
  that the image is ready and let the user perform flashing and board runtime
  validation.
- It is still acceptable to update project-owned flash profiles/wrappers and to
  document the exact command and expected logs for user-run validation.

## Codex Harness Hygiene
- Treat `.codex/active_context.md` as the canonical volatile context for the current branch, active objective, SDK baseline, and latest verified step.
- Treat `.codex/active_plans.md` as the canonical index of currently active multi-step execution plans.
- Keep `README.md` and `build.md` low-entropy:
  - they should stay stable and point to the active context instead of embedding fast-stale branch snapshots
  - historical branch-specific plans, status snapshots, and dated investigations belong under `doc/`
- For multi-step work that spans multiple modules or multiple sessions:
  - create or update an execution plan in `doc/`, starting from `doc/EXECUTION_PLAN_TEMPLATE_ZH.md`
  - register that plan in `.codex/active_plans.md`
- After changing repo-level Codex harness files (`AGENTS.md`, `README.md`, `build.md`, `plan.md`, `.codex/active_context.md`, `.codex/active_plans.md`, `doc/EXECUTION_PLAN_TEMPLATE_ZH.md`), run:
  - `python3 tools/diag/check_codex_harness.py`

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
  - `components/river_core`: Orvibo orchestration, state, and event routing
  - `components/river_voice`: local capture, VAD, wake word, preproc, AEC/BF, playback, and reference services
  - `components/river_cloud`: Orvibo realtime protocol, MCP volume, Wi-Fi, WebSocket, and Opus helpers
  - `components/river_diag`: debug and monitor commands
- New online features must go behind Orvibo-owned interfaces first, then bind to a concrete protocol.
- Local speech capability must be integrated through `river_voice_*` interfaces rather than directly from `app/`.

## Coding Style
- Prefer simple C over macro-heavy abstractions.
- Keep headers in `include/river/`.
- Keep platform-specific code replaceable.
- Favor deterministic logs and explicit error returns.

## Wakeword Debugging Discipline
- For all future wakeword-model debugging, preserve the existing board-side vs local comparison and parity code paths.
- Do not remove, bypass, or weaken tensor dumps, alignment replay, feature/input/output comparison hooks, or other board/local cross-check tooling just to speed up model bring-up.
- New wakeword-model adaptations should keep a path that can prove board behavior matches the intended local model contract before model-quality conclusions are drawn.
- Treat deployment-verification code as part of the wakeword debug infrastructure, not as temporary scaffolding to delete after one model trial.
- If a future change truly requires replacing that comparison path, first provide an equivalent or stronger validation path; do not regress this capability without explicit user approval.

## Current Objective
- Rebuild this branch as an Orvibo voice client mainline.
- The first external wire contract is XiaoZhi-compatible, but new files, public functions, runtime ownership, and diagnostics should use Orvibo naming.
- Current VAD, wake-word/KWS, tensor dump, alignment replay, and board/local parity paths are protected.
- Legacy dialog/session/cloud-adapter/split-provider/online-control code should not be reintroduced unless explicitly requested.
