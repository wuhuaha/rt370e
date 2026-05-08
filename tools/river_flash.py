#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from pathlib import Path


def resolve_profile(project_root: Path, sdk_root: Path, device: str, memory_type: str, use_sdk: bool) -> Path:
    local_dir = project_root / "board" / "rtl8730e" / "profiles"
    local_profile = local_dir / f"{device}_{memory_type.upper()}.rdev"
    if not use_sdk and local_profile.exists():
        return local_profile

    sdk_profile = sdk_root / "tools" / "ameba" / "Flash" / "Devices" / "Profiles" / f"{device}_{memory_type.upper()}.rdev"
    if sdk_profile.exists():
        return sdk_profile

    raise FileNotFoundError(f"no profile found for {device} {memory_type}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Project-owned flash wrapper for ameba-river.")
    parser.add_argument("-p", "--port", nargs="+", required=True, help="Serial port")
    parser.add_argument("-b", "--baudrate", type=int, default=1500000, help="Serial baud rate")
    parser.add_argument("-m", "--memory-type", choices=["nor", "nand", "ram"], default="nor", help="Memory type")
    parser.add_argument("--device", default="RTL8730E", help="Device name")
    parser.add_argument("--image-dir", help="Image directory")
    parser.add_argument("--chip-erase", action="store_true", help="Chip erase before download")
    parser.add_argument("--no-reset", action="store_true", help="Do not reset after flash")
    parser.add_argument("--log-level", default="info", help="Flash tool log level")
    parser.add_argument("--use-sdk-profile", action="store_true", help="Use SDK stock profile instead of project profile")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    sdk_root = Path(os.environ.get("AMEBA_SDK_ROOT", "/root/ameba-rtos")).resolve()
    flash_tool = sdk_root / "tools" / "ameba" / "Flash" / "AmebaFlash.py"
    profile = resolve_profile(project_root, sdk_root, args.device, args.memory_type, args.use_sdk_profile)

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

    print(f"[river_flash] profile={profile}")
    print(f"[river_flash] image_dir={image_dir}")
    result = subprocess.run(cmd)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
