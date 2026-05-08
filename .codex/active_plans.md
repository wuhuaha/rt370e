# Codex Active Plans

This file is the canonical index of currently active multi-step execution
plans. It is intentionally short: point to the live plan documents here instead
of encoding long plan content directly in root entry files.

## Active Plans

- Primary active execution plan:
  - `doc/ORVIBO_CLIENT_REARCH_EXECUTION_PLAN_ZH.md`
- Secondary active execution plans:
  - none pinned right now

## If A New Multi-Step Task Starts

- Create or update a plan document under `doc/`.
- Start from:
  - `doc/EXECUTION_PLAN_TEMPLATE_ZH.md`
- Add the new plan here before the implementation grows across multiple
  sessions, modules, or board-validation steps.
- Link the same plan from `.codex/active_context.md` if it becomes the main
  active objective.

## Reference-Only Historical Plans

- `plan.md`
  - historical root snapshot only
- `doc/history/xiaozhi_legacy/`
  - historical XiaoZhi direct-integration plans before the Orvibo-owned mainline
- `doc/history/agent_server_v2/`
  - historical `agent-server-v2` full-duplex and voice-runtime plans
- `doc/history/refactor_legacy/`
  - historical `refactor` branch architecture and dataflow plans
- `doc/history/provider_iflytek/`
  - historical Iflytek/provider-era integration notes
- `doc/history/project_snapshots/`
  - historical branch status snapshots
- `doc/history/codex/`
  - historical Codex process-context snapshots
