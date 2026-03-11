#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
import stat
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Copy a vendored Silero source artifact to an isolated conversion path so "
            "host-side tools never mutate the repository copy in place."
        )
    )
    parser.add_argument("--input", required=True, help="Path to the vendored source file")
    parser.add_argument("--output", required=True, help="Path to the staged output file")
    parser.add_argument(
        "--writable",
        action="store_true",
        help="Keep the staged output writable instead of forcing read-only permissions.",
    )
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy2(input_path, output_path)
    if not args.writable:
        output_path.chmod(stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)

    source_sha = sha256_file(input_path)
    staged_sha = sha256_file(output_path)
    if source_sha != staged_sha:
        raise RuntimeError(
            f"sha256 mismatch after staging: source={source_sha} staged={staged_sha}"
        )

    payload = {
        "input": str(input_path),
        "output": str(output_path),
        "sha256": staged_sha,
        "size": output_path.stat().st_size,
        "readonly": not args.writable,
    }
    print(json.dumps(payload, indent=2))


if __name__ == "__main__":
    main()
