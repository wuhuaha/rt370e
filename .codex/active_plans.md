# Codex Active Plans

This file is the canonical index of currently active multi-step execution
plans. It is intentionally short: point to the live plan documents here instead
of encoding long plan content directly in root entry files.

## Active Plans

- Primary active execution plan:
  - none pinned right now
- Secondary active execution plans:
  - none

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
- `doc/PROJECT_REFACTOR_EXECUTION_PLAN_ZH.md`
  - historical reference
- `doc/XIAOZHI_INTEGRATION_IMPLEMENTATION_PLAN_ZH.md`
  - historical/reference implementation plan
