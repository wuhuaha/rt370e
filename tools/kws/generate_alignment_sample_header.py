#!/usr/bin/env python3
"""Generate a compiled-in mono PCM16 alignment sample header for board KWS replay."""

from __future__ import annotations

import argparse
import wave
from pathlib import Path


DEFAULT_INPUT = Path(
    "/root/kws-dataset-pro-blueprint/data/raw/real_processed/"
    "device_recordings/positive/"
    "positive__pos_neutral_near_001__小欧管家__take001__rtl8730e-board__rec-1774343613649-351d33bd.wav"
)
DEFAULT_OUTPUT = Path(
    "/root/ameba-river/components/river_voice/generated/"
    "river_kws_alignment_sample_data.h"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--frame-samples", type=int, default=256)
    parser.add_argument("--lead-silence-frames", type=int, default=20)
    return parser.parse_args()


def read_pcm16_mono(path: Path) -> bytes:
    with wave.open(str(path), "rb") as handle:
        channels = handle.getnchannels()
        sample_width = handle.getsampwidth()
        sample_rate = handle.getframerate()
        frame_count = handle.getnframes()
        if channels != 1:
            raise ValueError(f"expected mono wav, got {channels} channels")
        if sample_width != 2:
            raise ValueError(f"expected 16-bit wav, got {sample_width * 8}-bit")
        if sample_rate != 16000:
            raise ValueError(f"expected 16 kHz wav, got {sample_rate} Hz")
        return handle.readframes(frame_count)


def iter_i16_le_bytes(raw: bytes) -> list[int]:
    if len(raw) % 2 != 0:
        raise ValueError("pcm payload must be 16-bit aligned")
    values: list[int] = []
    for index in range(0, len(raw), 2):
        value = int.from_bytes(raw[index : index + 2], "little", signed=True)
        values.append(value)
    return values


def format_i16_array(values: list[int], values_per_line: int = 12) -> str:
    lines: list[str] = []
    for index in range(0, len(values), values_per_line):
        chunk = values[index : index + values_per_line]
        lines.append("    " + ", ".join(str(value) for value in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    raw = read_pcm16_mono(args.input)
    pcm_values = iter_i16_le_bytes(raw)
    lead_silence_values = [0] * (args.lead_silence_frames * args.frame_samples)
    full_values = lead_silence_values + pcm_values

    if len(full_values) % args.frame_samples != 0:
        raise ValueError(
            f"sample length {len(full_values)} is not divisible by frame size "
            f"{args.frame_samples}"
        )

    total_frames = len(full_values) // args.frame_samples
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                "// Generated KWS alignment sample header.",
                "// Mono PCM16 @ 16 kHz with synthetic leading silence for deterministic gate open.",
                "",
                "#include <stdint.h>",
                "",
                f"#define RIVER_KWS_ALIGNMENT_FRAME_SAMPLES {args.frame_samples}U",
                f"#define RIVER_KWS_ALIGNMENT_PRE_SILENCE_FRAMES {args.lead_silence_frames}U",
                f"#define RIVER_KWS_ALIGNMENT_PCM_SAMPLES {len(full_values)}U",
                f"#define RIVER_KWS_ALIGNMENT_PCM_FRAMES {total_frames}U",
                "",
                "static const int16_t g_river_kws_alignment_sample_pcm[RIVER_KWS_ALIGNMENT_PCM_SAMPLES] = {",
                format_i16_array(full_values),
                "};",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(
        f"generated {args.output} samples={len(full_values)} "
        f"frames={total_frames} lead_silence_frames={args.lead_silence_frames}"
    )


if __name__ == "__main__":
    main()
