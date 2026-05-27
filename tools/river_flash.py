#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


NAND_FLASH_SETTING_OVERRIDES = {
    "RequestRetryCount": 20,
    "RequestRetryIntervalInMillisecond": 50,
    "AsyncResponseTimeoutInMilliseccond": 2000,
    "SyncResponseTimeoutInMillisecond": 2000,
    "WriteResponseTimeoutInMillisecond": 5000,
}


def resolve_profile(project_root: Path, sdk_root: Path, device: str, memory_type: str, use_sdk: bool) -> Path:
    local_dir = project_root / "board" / "rtl8730e" / "profiles"
    local_profile = local_dir / f"{device}_{memory_type.upper()}.rdev"
    if not use_sdk and local_profile.exists():
        return local_profile

    sdk_profile = sdk_root / "tools" / "ameba" / "Flash" / "Devices" / "Profiles" / f"{device}_{memory_type.upper()}.rdev"
    if sdk_profile.exists():
        return sdk_profile

    raise FileNotFoundError(f"no profile found for {device} {memory_type}")


def create_nand_flash_tool_copy(sdk_root: Path) -> tuple[Path, tempfile.TemporaryDirectory]:
    sdk_flash_dir = sdk_root / "tools" / "ameba" / "Flash"
    tmp_dir = tempfile.TemporaryDirectory(prefix="ameba_river_flash_")
    flash_dir = Path(tmp_dir.name) / "Flash"
    shutil.copytree(sdk_flash_dir, flash_dir, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))

    settings_path = flash_dir / "Settings.json"
    with settings_path.open("r", encoding="utf-8") as f:
        settings = json.load(f)

    settings.update(NAND_FLASH_SETTING_OVERRIDES)

    with settings_path.open("w", encoding="utf-8") as f:
        json.dump(settings, f, indent=2)
        f.write("\n")

    return flash_dir / "AmebaFlash.py", tmp_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="Project-owned flash wrapper for ameba-river.")
    parser.add_argument("-p", "--port", nargs="+", required=True, help="Serial port")
    parser.add_argument("-b", "--baudrate", type=int, default=1500000, help="Serial baud rate")
    parser.add_argument(
        "-m",
        "--memory-type",
        choices=["nor", "nand", "ram"],
        default="nand",
        help="Memory type; defaults to nand for the current hardware",
    )
    parser.add_argument("--device", default="RTL8730E", help="Device name")
    parser.add_argument("--image-dir", help="Image directory")
    parser.add_argument("--chip-erase", action="store_true", help="Chip erase before download")
    parser.add_argument("--no-reset", action="store_true", help="Do not reset after flash")
    parser.add_argument("--log-level", default="info", help="Flash tool log level")
    parser.add_argument("--use-sdk-profile", action="store_true", help="Use SDK stock profile instead of project profile")
    parser.add_argument("--use-sdk-flash-tool", action="store_true", help="Run SDK flash tool directly without project NAND settings")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    sdk_root = Path(os.environ.get("AMEBA_SDK_ROOT", "/root/ameba-rtos")).resolve()
    flash_tool = sdk_root / "tools" / "ameba" / "Flash" / "AmebaFlash.py"
    profile = resolve_profile(project_root, sdk_root, args.device, args.memory_type, args.use_sdk_profile)
    flash_tool_tmp = None

    if args.memory_type == "nand" and not args.use_sdk_flash_tool:
        flash_tool, flash_tool_tmp = create_nand_flash_tool_copy(sdk_root)

    image_dir = Path(args.image_dir).resolve() if args.image_dir else (
        project_root / "build_RTL8730E" / "build" / "project_hp" / "image"
    )

    cmd = [
        sys.executable,
        str(flash_tool),
        "--download",
        "--profile",
        str(profile),
        "--port",
        *args.port,
        "--baudrate",
        str(args.baudrate),
        "--memory-type",
        args.memory_type,
        "--log-level",
        args.log_level.upper(),
        "--image-dir",
        str(image_dir),
    ]

    if args.chip_erase:
        cmd.append("--chip-erase")
    if args.no_reset:
        cmd.append("--no-reset")

    print(f"[river_flash] profile={profile}", flush=True)
    print(f"[river_flash] image_dir={image_dir}", flush=True)
    if flash_tool_tmp is not None:
        print(f"[river_flash] flash_tool={flash_tool}", flush=True)
        print(f"[river_flash] nand_settings={NAND_FLASH_SETTING_OVERRIDES}", flush=True)

    try:
        result = subprocess.run(cmd)
        return result.returncode
    finally:
        if flash_tool_tmp is not None:
            flash_tool_tmp.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
