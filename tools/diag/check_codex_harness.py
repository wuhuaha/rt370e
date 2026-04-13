#!/usr/bin/env python3
"""Validate that Codex-facing repository entry points stay aligned."""

from __future__ import annotations

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
CANONICAL_SDK = "/root/ameba-rtos"
OLD_SDK = "/root/ameba-rtos-1.2"


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def current_branch() -> str:
    result = subprocess.run(
        ["git", "branch", "--show-current"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def report(label: str, ok: bool, detail: str) -> None:
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {label}")
    if not ok:
        print(f"       {detail}")


def main() -> int:
    branch = current_branch()

    agents = read_text("AGENTS.md")
    readme = read_text("README.md")
    build = read_text("build.md")
    plan = read_text("plan.md")
    active_context = read_text(".codex/active_context.md")
    active_plans = read_text(".codex/active_plans.md")
    execution_plan_template = read_text("doc/EXECUTION_PLAN_TEMPLATE_ZH.md")

    checks = [
        (
            "AGENTS points to the canonical active context",
            ".codex/active_context.md" in agents,
            "Add the active-context pointer to AGENTS.md.",
        ),
        (
            "AGENTS points to the canonical active-plan index",
            ".codex/active_plans.md" in agents,
            "Add the active-plan index pointer to AGENTS.md.",
        ),
        (
            "AGENTS documents the harness check command",
            "python3 tools/diag/check_codex_harness.py" in agents,
            "Document the required harness check command in AGENTS.md.",
        ),
        (
            "README points to the canonical active context",
            ".codex/active_context.md" in readme,
            "README.md should send readers to .codex/active_context.md.",
        ),
        (
            "README points to the canonical active-plan index",
            ".codex/active_plans.md" in readme,
            "README.md should send readers to .codex/active_plans.md.",
        ),
        (
            "README documents the harness check command",
            "python3 tools/diag/check_codex_harness.py" in readme,
            "README.md should tell users how to validate harness consistency.",
        ),
        (
            "README avoids the old default SDK path",
            OLD_SDK not in readme,
            "README.md should not advertise /root/ameba-rtos-1.2 as the default SDK.",
        ),
        (
            "build.md uses the canonical SDK baseline",
            CANONICAL_SDK in build and "AMEBA_SDK_ROOT" in build,
            "build.md should use /root/ameba-rtos and document AMEBA_SDK_ROOT.",
        ),
        (
            "build.md avoids the old default SDK path",
            OLD_SDK not in build,
            "build.md should not advertise /root/ameba-rtos-1.2 as the default SDK.",
        ),
        (
            "active context tracks the current git branch",
            f"Current working branch: `{branch}`" in active_context,
            f".codex/active_context.md should name the current branch `{branch}`.",
        ),
        (
            "active context tracks the canonical SDK baseline",
            f"Active SDK baseline: `{CANONICAL_SDK}`" in active_context,
            ".codex/active_context.md should name /root/ameba-rtos as the baseline.",
        ),
        (
            "active context points to the active-plan index",
            ".codex/active_plans.md" in active_context,
            ".codex/active_context.md should point to .codex/active_plans.md.",
        ),
        (
            "active-plan index points to the execution-plan template",
            "doc/EXECUTION_PLAN_TEMPLATE_ZH.md" in active_plans,
            ".codex/active_plans.md should reference the execution-plan template.",
        ),
        (
            "execution-plan template is clearly labeled",
            "# 执行计划模板" in execution_plan_template,
            "doc/EXECUTION_PLAN_TEMPLATE_ZH.md should contain the execution-plan template header.",
        ),
        (
            "plan.md is marked as historical",
            "historical snapshot" in plan and ".codex/active_context.md" in plan,
            "Root plan.md should clearly defer to .codex/active_context.md.",
        ),
    ]

    print(f"Repository root: {ROOT}")
    print(f"Current branch: {branch}")

    failures = 0
    for label, ok, detail in checks:
        report(label, ok, detail)
        if not ok:
            failures += 1

    if failures:
        print(f"check_codex_harness: {failures} issue(s) found")
        return 1

    print("check_codex_harness: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
