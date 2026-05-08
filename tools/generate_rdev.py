#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an encrypted Ameba .rdev profile from JSON.")
    parser.add_argument("--input", required=True, help="Path to decrypted profile JSON")
    parser.add_argument("--output", required=True, help="Path to encrypted .rdev output")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    sdk_root = Path(os.environ.get("AMEBA_SDK_ROOT", "/root/ameba-rtos")).resolve()
    sdk_flash_root = sdk_root / "tools" / "ameba" / "Flash"
    sys.path.insert(0, str(sdk_flash_root))

    try:
        from base.json_utils import JsonUtils
    except Exception as err:
        print(f"failed to import SDK JsonUtils: {err}", file=sys.stderr)
        return 1

    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()
    profile = JsonUtils.load_from_file(str(input_path), need_decrypt=False)
    if profile is None:
        print(f"failed to load profile json: {input_path}", file=sys.stderr)
        return 1

    JsonUtils.save_to_file(str(output_path), profile, need_encrypt=True)
    print(f"generated {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
