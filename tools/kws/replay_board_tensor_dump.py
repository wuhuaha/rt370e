#!/usr/bin/env python3
"""Replay a board-emitted KWS exact tensor dump on host TFLite."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf


DEFAULT_MODEL = Path(
    "/root/kws-training-pro/models/bc_resnet_iteration3/"
    "bc_resnet_v3_production_final_v2.tflite"
)

BEGIN_RE = re.compile(
    r"kws tensor dump begin: "
    r"seq=(?P<seq>\d+) infer=(?P<infer>\d+) gate=(?P<gate>\w+) "
    r"in_type=(?P<in_type>\w+) out_type=(?P<out_type>\w+) "
    r"layout=(?P<layout>\w+) "
    r"shape=(?P<s0>\d+),(?P<s1>\d+),(?P<s2>\d+),(?P<s3>\d+) "
    r"feat_bytes=(?P<feat_bytes>\d+) input_bytes=(?P<input_bytes>\d+) "
    r"output_bytes=(?P<output_bytes>\d+)"
)
BEGIN_RE_COMPACT = re.compile(
    r"KWSDUMP BEGIN "
    r"seq=(?P<seq>\d+) infer=(?P<infer>\d+) gate=(?P<gate>\w+) "
    r"in_type=(?P<in_type>\w+) out_type=(?P<out_type>\w+) "
    r"layout=(?P<layout>\w+) "
    r"shape=(?P<s0>\d+),(?P<s1>\d+),(?P<s2>\d+),(?P<s3>\d+) "
    r"feat_bytes=(?P<feat_bytes>\d+) input_bytes=(?P<input_bytes>\d+) "
    r"output_bytes=(?P<output_bytes>\d+)"
)
META_RE = re.compile(
    r"kws tensor dump meta: "
    r"seq=(?P<seq>\d+) "
    r"feat_hash=0x(?P<feat_hash>[0-9a-fA-F]+) "
    r"input_hash=0x(?P<input_hash>[0-9a-fA-F]+) "
    r"raw=(?P<raw>-?\d+) "
    r"score=(?P<score>[0-9eE+.\-]+) "
    r"q15=(?P<q15>\d+) "
    r"in_scale=(?P<in_scale>[0-9eE+.\-]+) "
    r"in_zp=(?P<in_zp>-?\d+) "
    r"out_scale=(?P<out_scale>[0-9eE+.\-]+) "
    r"out_zp=(?P<out_zp>-?\d+)"
)
META_RE_COMPACT = re.compile(
    r"KWSDUMP META "
    r"seq=(?P<seq>\d+) "
    r"feat_hash=0x(?P<feat_hash>[0-9a-fA-F]+) "
    r"input_hash=0x(?P<input_hash>[0-9a-fA-F]+) "
    r"raw=(?P<raw>-?\d+) "
    r"score=(?P<score>[0-9eE+.\-]+) "
    r"q15=(?P<q15>\d+) "
    r"in_scale=(?P<in_scale>[0-9eE+.\-]+) "
    r"in_zp=(?P<in_zp>-?\d+) "
    r"out_scale=(?P<out_scale>[0-9eE+.\-]+) "
    r"out_zp=(?P<out_zp>-?\d+)"
)
CHUNK_RE = re.compile(
    r"kws tensor dump (?P<label>\w+): "
    r"seq=(?P<seq>\d+) chunk=(?P<chunk>\d+)/(?P<total>\d+) hex=(?P<hex>[0-9a-fA-F]+)"
)
CHUNK_RE_COMPACT = re.compile(
    r"KWSDUMP CHUNK "
    r"label=(?P<label>\w+) "
    r"seq=(?P<seq>\d+) chunk=(?P<chunk>\d+)/(?P<total>\d+) hex=(?P<hex>[0-9a-fA-F]+)"
)
END_RE = re.compile(r"kws tensor dump end: seq=(?P<seq>\d+) infer=(?P<infer>\d+)")
END_RE_COMPACT = re.compile(r"KWSDUMP END seq=(?P<seq>\d+) infer=(?P<infer>\d+)")


def fnv1a32(data: bytes, seed: int = 2166136261) -> int:
    value = seed
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def round_half_away_from_zero(values: np.ndarray) -> np.ndarray:
    return np.trunc(
        np.where(values >= 0.0, values + 0.5, values - 0.5)
    ).astype(np.int32, copy=False)


def input_dtype_from_name(name: str) -> np.dtype[Any]:
    if name == "int8":
        return np.dtype(np.int8)
    if name == "uint8":
        return np.dtype(np.uint8)
    if name == "float32":
        return np.dtype(np.float32)
    raise ValueError(f"unsupported input dtype: {name}")


@dataclass
class DumpRecord:
    seq: int
    infer: int | None = None
    gate: str | None = None
    in_type: str | None = None
    out_type: str | None = None
    layout: str | None = None
    shape: tuple[int, int, int, int] | None = None
    feat_bytes: int | None = None
    input_bytes: int | None = None
    output_bytes: int | None = None
    feat_hash: int | None = None
    input_hash: int | None = None
    raw: int | None = None
    score: float | None = None
    q15: int | None = None
    in_scale: float | None = None
    in_zp: int | None = None
    out_scale: float | None = None
    out_zp: int | None = None
    feat_chunks: dict[int, bytes] = field(default_factory=dict)
    input_chunks: dict[int, bytes] = field(default_factory=dict)
    output_chunks: dict[int, bytes] = field(default_factory=dict)
    feat_total_chunks: int | None = None
    input_total_chunks: int | None = None
    output_total_chunks: int | None = None
    ended: bool = False

    def require_complete(self) -> None:
        missing: list[str] = []
        if self.shape is None:
            missing.append("begin")
        if self.feat_hash is None:
            missing.append("meta")
        if self.feat_total_chunks is None or len(self.feat_chunks) != self.feat_total_chunks:
            missing.append("feat_f32")
        if self.input_total_chunks is None or len(self.input_chunks) != self.input_total_chunks:
            missing.append("input_raw")
        if self.output_total_chunks is None or len(self.output_chunks) != self.output_total_chunks:
            missing.append("output_raw")
        if missing:
            raise ValueError(f"dump seq={self.seq} incomplete: {', '.join(missing)}")

    def assemble(self, chunks: dict[int, bytes], expected_total: int | None) -> bytes:
        if expected_total is None:
            raise ValueError(f"dump seq={self.seq} missing chunk count")
        return b"".join(chunks[index] for index in range(1, expected_total + 1))


def parse_dump_records(log_path: Path) -> dict[int, DumpRecord]:
    records: dict[int, DumpRecord] = {}
    with log_path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            line = line.replace("\x00", "")

            if match := BEGIN_RE.search(line) or BEGIN_RE_COMPACT.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                record.infer = int(match.group("infer"))
                record.gate = match.group("gate")
                record.in_type = match.group("in_type")
                record.out_type = match.group("out_type")
                record.layout = match.group("layout")
                record.shape = tuple(
                    int(match.group(name)) for name in ("s0", "s1", "s2", "s3")
                )
                record.feat_bytes = int(match.group("feat_bytes"))
                record.input_bytes = int(match.group("input_bytes"))
                record.output_bytes = int(match.group("output_bytes"))
                continue

            if match := META_RE.search(line) or META_RE_COMPACT.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                record.feat_hash = int(match.group("feat_hash"), 16)
                record.input_hash = int(match.group("input_hash"), 16)
                record.raw = int(match.group("raw"))
                record.score = float(match.group("score"))
                record.q15 = int(match.group("q15"))
                record.in_scale = float(match.group("in_scale"))
                record.in_zp = int(match.group("in_zp"))
                record.out_scale = float(match.group("out_scale"))
                record.out_zp = int(match.group("out_zp"))
                continue

            if match := CHUNK_RE.search(line) or CHUNK_RE_COMPACT.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                label = match.group("label")
                chunk_idx = int(match.group("chunk"))
                total = int(match.group("total"))
                payload = bytes.fromhex(match.group("hex"))
                if label == "feat_f32":
                    record.feat_chunks[chunk_idx] = payload
                    record.feat_total_chunks = total
                elif label == "input_raw":
                    record.input_chunks[chunk_idx] = payload
                    record.input_total_chunks = total
                elif label == "output_raw":
                    record.output_chunks[chunk_idx] = payload
                    record.output_total_chunks = total
                continue

            if match := END_RE.search(line) or END_RE_COMPACT.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                record.ended = True
                if record.infer is None:
                    record.infer = int(match.group("infer"))

    return records


def quantize_feature_tensor(
    feature_tensor: np.ndarray, in_type: str, scale: float, zero_point: int
) -> bytes:
    if in_type == "float32":
        return feature_tensor.astype("<f4", copy=False).tobytes()

    if scale <= 0.0:
        raise ValueError(f"invalid quantization scale: {scale}")

    quantized = round_half_away_from_zero(feature_tensor / scale) + zero_point
    if in_type == "int8":
        return np.clip(quantized, -128, 127).astype(np.int8, copy=False).tobytes()
    if in_type == "uint8":
        return np.clip(quantized, 0, 255).astype(np.uint8, copy=False).tobytes()
    raise ValueError(f"unsupported quantized type: {in_type}")


def decode_feature_hash(feature_tensor: np.ndarray) -> int:
    feature_milli = round_half_away_from_zero(feature_tensor * 1000.0).astype("<i4")
    return fnv1a32(feature_milli.tobytes())


def decode_raw_scalar(output_array: np.ndarray, out_type: str) -> int:
    flat = output_array.reshape(-1)
    if out_type == "float32":
        return int(round_half_away_from_zero(flat.astype(np.float32) * 1000.0)[0])
    return int(flat[0])


def dequant_score(raw_scalar: int, out_type: str, out_scale: float, out_zp: int) -> float:
    if out_type == "float32":
        return raw_scalar / 1000.0
    return float(raw_scalar - out_zp) * out_scale


def first_diff_indices(lhs: bytes, rhs: bytes, limit: int = 8) -> list[int]:
    diffs: list[int] = []
    for index, (lval, rval) in enumerate(zip(lhs, rhs)):
        if lval != rval:
            diffs.append(index)
            if len(diffs) >= limit:
                break
    if len(lhs) != len(rhs) and len(diffs) < limit:
        diffs.append(min(len(lhs), len(rhs)))
    return diffs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True, help="serial monitor log file")
    parser.add_argument(
        "--model", type=Path, default=DEFAULT_MODEL, help="tflite model to replay"
    )
    parser.add_argument(
        "--seq",
        default="latest",
        help="dump sequence number to replay, or 'latest' (default)",
    )
    args = parser.parse_args()

    records = parse_dump_records(args.log)
    if not records:
        raise SystemExit(f"no board tensor dump found in {args.log}")

    if args.seq == "latest":
        seq = max(records)
    else:
        seq = int(args.seq)
    if seq not in records:
        raise SystemExit(f"dump seq={seq} not found in {args.log}")

    record = records[seq]
    record.require_complete()

    feature_bytes = record.assemble(record.feat_chunks, record.feat_total_chunks)
    input_bytes = record.assemble(record.input_chunks, record.input_total_chunks)
    output_bytes = record.assemble(record.output_chunks, record.output_total_chunks)

    if record.feat_bytes is not None and len(feature_bytes) != record.feat_bytes:
        raise SystemExit(
            f"feature bytes mismatch: parsed={len(feature_bytes)} expected={record.feat_bytes}"
        )
    if record.input_bytes is not None and len(input_bytes) != record.input_bytes:
        raise SystemExit(
            f"input bytes mismatch: parsed={len(input_bytes)} expected={record.input_bytes}"
        )
    if record.output_bytes is not None and len(output_bytes) != record.output_bytes:
        raise SystemExit(
            f"output bytes mismatch: parsed={len(output_bytes)} expected={record.output_bytes}"
        )

    assert record.shape is not None
    assert record.in_type is not None
    assert record.out_type is not None
    assert record.in_scale is not None
    assert record.in_zp is not None
    assert record.out_scale is not None
    assert record.out_zp is not None
    assert record.raw is not None
    assert record.score is not None

    feature_tensor = np.frombuffer(feature_bytes, dtype="<f4").reshape(record.shape)
    recomputed_feature_hash = decode_feature_hash(feature_tensor)
    recomputed_input_hash = fnv1a32(input_bytes)
    feature_raw_hash = fnv1a32(feature_bytes)
    requantized_input = quantize_feature_tensor(
        feature_tensor, record.in_type, record.in_scale, record.in_zp
    )
    quant_diff = sum(a != b for a, b in zip(requantized_input, input_bytes))

    effective_input_bytes = input_bytes
    effective_input_source = "input_raw"
    if (
        record.in_type == "float32"
        and record.input_hash != recomputed_input_hash
        and record.input_hash == feature_raw_hash
        and len(feature_bytes) == len(input_bytes)
    ):
        # Some board logs carry a corrupted input_raw stream even though the
        # recorded input_hash still matches the feature tensor's raw float bytes.
        effective_input_bytes = feature_bytes
        effective_input_source = "feat_f32_fallback"

    effective_input_hash = fnv1a32(effective_input_bytes)

    interpreter = tf.lite.Interpreter(model_path=str(args.model))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    input_dtype = input_dtype_from_name(record.in_type)
    input_tensor = np.frombuffer(effective_input_bytes, dtype=input_dtype).reshape(
        record.shape
    )
    interpreter.set_tensor(input_details["index"], input_tensor)
    interpreter.invoke()
    host_output = interpreter.get_tensor(output_details["index"])
    host_output_bytes = host_output.astype(output_details["dtype"], copy=False).tobytes()
    host_raw = decode_raw_scalar(host_output, record.out_type)
    host_score = dequant_score(host_raw, record.out_type, record.out_scale, record.out_zp)

    print(f"dump_seq={record.seq} infer={record.infer} gate={record.gate}")
    print(
        "board_meta:"
        f" input_type={record.in_type} output_type={record.out_type}"
        f" shape={record.shape} layout={record.layout}"
    )
    print(
        "board_hash:"
        f" feature=0x{record.feat_hash:08x} input=0x{record.input_hash:08x}"
    )
    print(
        "host_hash:"
        f" feature=0x{recomputed_feature_hash:08x}"
        f" logged_input=0x{recomputed_input_hash:08x}"
        f" effective_input=0x{effective_input_hash:08x}"
        f" source={effective_input_source}"
    )
    print(
        "quant_parity:"
        f" diff_bytes={quant_diff}/{len(input_bytes)}"
        f" first_diff={first_diff_indices(requantized_input, input_bytes)}"
    )
    print(
        "board_output:"
        f" raw={record.raw} score={record.score:.6f} q15={record.q15}"
    )
    print(f"host_output: raw={host_raw} score={host_score:.6f}")
    print(
        "output_parity:"
        f" bytes_equal={'yes' if host_output_bytes == output_bytes else 'no'}"
        f" raw_equal={'yes' if host_raw == record.raw else 'no'}"
        f" first_diff={first_diff_indices(host_output_bytes, output_bytes)}"
    )

    if record.feat_hash != recomputed_feature_hash:
        print("warning: feature hash mismatch", file=sys.stderr)
    if record.input_hash != recomputed_input_hash:
        print("warning: input hash mismatch", file=sys.stderr)
    if effective_input_source != "input_raw":
        print(
            "note: using feature tensor bytes as effective input because they match the board input hash",
            file=sys.stderr,
        )
    if host_output_bytes != output_bytes:
        print("warning: host replay output does not match board dump", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
