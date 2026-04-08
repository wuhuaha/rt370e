#!/usr/bin/env python3
"""Apply the Ameba River RTL8730E SDK memory layout patch.

This tool intentionally keeps the patch small and explicit so the project can
track SDK-side edits from inside the repository.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


CONSERVATIVE_8MB = {
    "name": "conservative_ca32_8mb",
    "psram_end": "0x60C00000",
    "ca32_end": "0x60B00000",
    "km4_ext_origin": "0x60B00000",
    "ca32_comment": "8MB",
    "km4_comment": "1MB, conservative expansion",
}

AIVOICE_17MB = {
    "name": "aivoice_ca32_17mb",
    "psram_end": "0x61500000",
    "ca32_end": "0x61400000",
    "km4_ext_origin": "0x61400000",
    "ca32_comment": "17MB",
    "km4_comment": "1MB, aivoice-style debug expansion",
}

VARIANTS = {
    CONSERVATIVE_8MB["name"]: CONSERVATIVE_8MB,
    AIVOICE_17MB["name"]: AIVOICE_17MB,
}


def replace_or_fail(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError(f"failed to patch {label}: pattern not found or not unique")
    return updated


def patch_layout(text: str, variant: dict[str, str]) -> str:
    updated = text
    updated = replace_or_fail(
        updated,
        r"(#define PSRAM_END\s+)\(0x[0-9A-Fa-f]+\)",
        rf"\g<1>({variant['psram_end']})",
        "ameba_layout.ld PSRAM_END",
    )
    updated = replace_or_fail(
        updated,
        r"(CA32_BL3_DRAM_NS \(rwx\) :\s+ORIGIN = 0x60300000, LENGTH = )0x[0-9A-Fa-f]+ - 0x60300000(\s+/\* CA32 BL3 DRAM NS: )[^*]+(\*/)",
        rf"\g<1>{variant['ca32_end']} - 0x60300000\g<2>{variant['ca32_comment']} \g<3>",
        "ameba_layout.ld CA32_BL3_DRAM_NS",
    )
    updated = replace_or_fail(
        updated,
        r"(KM4_DRAM_HEAP_EXT \(rwx\) :\s+ORIGIN = )0x[0-9A-Fa-f]+(, LENGTH = PSRAM_END - )0x[0-9A-Fa-f]+(\s+/\* KM4 PSRAM HEAP EXT: )1MB[^*]*(\*/)",
        rf"\g<1>{variant['km4_ext_origin']}\g<2>{variant['km4_ext_origin']}\g<3>{variant['km4_comment']} \g<4>",
        "ameba_layout.ld KM4_DRAM_HEAP_EXT",
    )
    return updated


def patch_hal_platform(text: str, variant: dict[str, str]) -> str:
    return replace_or_fail(
        text,
        r"(#define PSRAM_END\s+)0x[0-9A-Fa-f]+",
        rf"\g<1>{variant['psram_end']}",
        "hal_platform.h PSRAM_END",
    )


def write_if_changed(path: pathlib.Path, content: str) -> bool:
    original = path.read_text(encoding="utf-8")
    if original == content:
        return False
    path.write_text(content, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--sdk-root",
        default="/root/ameba-rtos-1.2",
        help="SDK root to patch or check",
    )
    parser.add_argument(
        "--variant",
        default=CONSERVATIVE_8MB["name"],
        choices=sorted(VARIANTS.keys()),
        help="SDK memory layout variant to apply",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate that the variant is already applied without modifying files",
    )
    args = parser.parse_args()

    variant = VARIANTS[args.variant]
    sdk_root = pathlib.Path(args.sdk_root).resolve()
    sdk_layout = sdk_root / "component" / "soc" / "amebasmart" / "project" / "ameba_layout.ld"
    sdk_hal_platform = (
        sdk_root
        / "component"
        / "soc"
        / "amebasmart"
        / "fwlib"
        / "include"
        / "hal_platform.h"
    )

    if not sdk_layout.exists():
        raise FileNotFoundError(f"layout file not found: {sdk_layout}")
    if not sdk_hal_platform.exists():
        raise FileNotFoundError(f"hal_platform file not found: {sdk_hal_platform}")

    layout_original = sdk_layout.read_text(encoding="utf-8")
    hal_original = sdk_hal_platform.read_text(encoding="utf-8")
    layout_updated = patch_layout(layout_original, variant)
    hal_updated = patch_hal_platform(hal_original, variant)

    if args.check:
        ok = layout_original == layout_updated and hal_original == hal_updated
        print("applied" if ok else "not-applied")
        return 0 if ok else 1

    layout_changed = write_if_changed(sdk_layout, layout_updated)
    hal_changed = write_if_changed(sdk_hal_platform, hal_updated)

    print(
        f"sdk_root={sdk_root} "
        f"variant={variant['name']} "
        f"layout={'changed' if layout_changed else 'unchanged'} "
        f"hal={'changed' if hal_changed else 'unchanged'}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
