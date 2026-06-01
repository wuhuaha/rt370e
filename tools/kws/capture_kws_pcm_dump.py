#!/usr/bin/env python3
"""Capture one board KWS raw/preproc PCM dump and run host frontend compare."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

import serial


CAPTURED_RE = re.compile(r"kws tensor dump captured: seq=(?P<seq>\d+)")
SNAPSHOT_RE = re.compile(
    r"(?:kws tensor dump snapshot:|KWSDUMP SNAPSHOT) "
    r"seq=(?P<seq>\d+) infer=(?P<infer>\d+) "
    r"chunks=\[feat:(?P<feat>\d+) input:(?P<input>\d+) "
    r"output:(?P<output>\d+) pcm:(?P<pcm>\d+)"
    r"(?: raw_pcm:(?P<raw_pcm>\d+))?\]"
)
DUMP_CHUNK_BYTES = 64


class SerialCapture:
    def __init__(self, port: str, baudrate: int, log_path: Path):
        self.port = port
        self.baudrate = baudrate
        self.log_path = log_path
        self.buffer = ""
        self.serial: serial.Serial | None = None
        self.log_file = None

    def __enter__(self) -> "SerialCapture":
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_file = self.log_path.open("w", encoding="utf-8", errors="ignore")
        self.serial = serial.Serial(self.port, self.baudrate, timeout=0.05)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.serial is not None:
            self.serial.close()
        if self.log_file is not None:
            self.log_file.close()

    def read_for(self, seconds: float) -> str:
        assert self.serial is not None
        assert self.log_file is not None
        deadline = time.monotonic() + seconds
        collected = []
        while time.monotonic() < deadline:
            data = self.serial.read(4096)
            if not data:
                continue
            text = data.decode("utf-8", errors="ignore").replace("\x00", "")
            self.log_file.write(text)
            self.log_file.flush()
            self.buffer += text
            collected.append(text)
        return "".join(collected)

    def write_command(self, command: str, settle_s: float = 0.15) -> None:
        assert self.serial is not None
        self.serial.write((command + "\r").encode("utf-8"))
        self.serial.flush()
        self.read_for(settle_s)

    def wait_for(
        self,
        pattern: re.Pattern[str],
        timeout_s: float,
        start_pos: int = 0,
    ) -> re.Match[str]:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            match = pattern.search(self.buffer, start_pos)
            if match is not None:
                return match
            self.read_for(0.20)
        raise TimeoutError(f"timeout waiting for pattern: {pattern.pattern}")


def chunk_regex(label: str, seq: int, chunk: int, total: int) -> re.Pattern[str]:
    escaped_label = re.escape(label)
    if chunk < total:
        hex_payload = rf"[0-9a-fA-F]{{{DUMP_CHUNK_BYTES * 2}}}"
    else:
        hex_payload = rf"(?:[0-9a-fA-F]{{2}}){{1,{DUMP_CHUNK_BYTES}}}"
    return re.compile(
        (
            rf"(?:kws tensor dump {escaped_label}: seq={seq} "
            rf"chunk={chunk}/{total} hex=|"
            rf"KWSDUMP CHUNK label={escaped_label} seq={seq} "
            rf"chunk={chunk}/{total} hex=)"
            rf"{hex_payload}(?:\r?\n)"
        )
    )


def pull_chunks(
    capture: SerialCapture,
    label: str,
    total: int,
    seq: int,
    command_delay_s: float,
    chunk_timeout_s: float,
    chunk_retries: int,
) -> None:
    for chunk in range(1, total + 1):
        pattern = chunk_regex(label, seq, chunk, total)
        for attempt in range(1, chunk_retries + 1):
            before = len(capture.buffer)
            capture.write_command(
                f"river kws dump chunk {label} {chunk}",
                command_delay_s,
            )
            deadline = time.monotonic() + chunk_timeout_s
            while time.monotonic() < deadline:
                if pattern.search(capture.buffer, before):
                    break
                capture.read_for(0.05)
            else:
                if attempt < chunk_retries:
                    continue
                raise TimeoutError(
                    f"timeout waiting for {label} chunk {chunk}/{total} "
                    f"after {chunk_retries} attempts"
                )
            break
        else:
            raise TimeoutError(f"timeout waiting for {label} chunk {chunk}/{total}")


def run_compare(args: argparse.Namespace, log_path: Path) -> None:
    command = [
        sys.executable,
        "tools/kws/compare_board_pcm_frontend.py",
        "--log",
        str(log_path),
        "--seq",
        "latest",
        "--out-dir",
        str(args.out_dir),
    ]
    if not args.allow_missing_raw:
        command.append("--require-raw")
    if args.contract is not None:
        command.extend(["--contract", str(args.contract)])
    subprocess.run(command, cwd=args.repo_root, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-p", "--port", required=True)
    parser.add_argument("-b", "--baudrate", type=int, default=1500000)
    parser.add_argument("--log", type=Path, default=Path("tmp/kws_pcm_dump_serial.log"))
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("tmp/kws_pcm_frontend_compare"),
    )
    parser.add_argument("--contract", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--capture-timeout-s", type=float, default=90.0)
    parser.add_argument("--chunk-timeout-s", type=float, default=2.0)
    parser.add_argument("--chunk-retries", type=int, default=5)
    parser.add_argument("--command-delay-s", type=float, default=0.02)
    parser.add_argument("--post-capture-settle-s", type=float, default=0.8)
    parser.add_argument(
        "--allow-missing-raw",
        action="store_true",
        help="Allow older board images that only dump preproc_s16, not raw_capture_s16.",
    )
    parser.add_argument(
        "--no-quiet-orvibo",
        action="store_true",
        help="Do not send `river orvibo abort` after dump capture.",
    )
    parser.add_argument("--skip-compare", action="store_true")
    args = parser.parse_args()

    args.repo_root = args.repo_root.resolve()
    log_path = args.log if args.log.is_absolute() else args.repo_root / args.log
    out_dir = args.out_dir if args.out_dir.is_absolute() else args.repo_root / args.out_dir
    args.out_dir = out_dir

    with SerialCapture(args.port, args.baudrate, log_path) as capture:
        capture.read_for(0.5)
        capture.write_command("river kws dump clear", 0.20)
        capture_start = len(capture.buffer)
        capture.write_command("river kws dump next", 0.20)
        print(f"armed dump; waiting up to {args.capture_timeout_s:.1f}s")
        captured = capture.wait_for(
            CAPTURED_RE,
            args.capture_timeout_s,
            capture_start,
        )
        seq = int(captured.group("seq"))
        print(f"captured seq={seq}; pulling metadata")
        if not args.no_quiet_orvibo:
            capture.write_command("river orvibo abort", args.post_capture_settle_s)

        meta_start = len(capture.buffer)
        capture.write_command("river kws dump meta", 0.30)
        snapshot = capture.wait_for(SNAPSHOT_RE, 5.0, meta_start)
        if int(snapshot.group("seq")) != seq:
            seq = int(snapshot.group("seq"))
        raw_pcm_chunks = int(snapshot.group("raw_pcm") or 0)
        if raw_pcm_chunks <= 0 and not args.allow_missing_raw:
            raise RuntimeError(
                "dump snapshot has no raw_pcm chunks; flash an image with "
                "raw_capture_s16 support or rerun with --allow-missing-raw "
                "for preproc-only comparison"
            )
        counts = {
            "feat_f32": int(snapshot.group("feat")),
            "output_raw": int(snapshot.group("output")),
            "preproc_s16": int(snapshot.group("pcm")),
        }
        if raw_pcm_chunks > 0:
            counts["raw_capture_s16"] = raw_pcm_chunks
        print(
            "chunks: "
            + " ".join(f"{label}={total}" for label, total in counts.items())
        )
        for label, total in counts.items():
            if total <= 0:
                raise RuntimeError(f"missing chunk count for {label}: {total}")
            pull_chunks(
                capture,
                label,
                total,
                seq,
                args.command_delay_s,
                args.chunk_timeout_s,
                args.chunk_retries,
            )
            print(f"pulled {label}: {total} chunks")

    print(f"log={log_path}")
    if not args.skip_compare:
        run_compare(args, log_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
