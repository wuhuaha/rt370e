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
PCM_META_RE = re.compile(
    r"kws pcm dump meta: "
    r"seq=(?P<seq>\d+) infer=(?P<infer>\d+) "
    r"label=(?P<label>\w+) sample_rate=(?P<sample_rate>\d+) "
    r"samples=(?P<samples>\d+) bytes=(?P<bytes>\d+) "
    r"hash=0x(?P<hash>[0-9a-fA-F]+) "
    r"center_pad=(?P<center_pad>\d+) window=(?P<window>\d+) "
    r"hop=(?P<hop>\d+) frames=(?P<frames>\d+)"
)
RAW_PCM_META_RE = re.compile(
    r"kws raw pcm dump meta: "
    r"seq=(?P<seq>\d+) infer=(?P<infer>\d+) "
    r"label=(?P<label>\w+) sample_rate=(?P<sample_rate>\d+) "
    r"channels=(?P<channels>\d+) samples=(?P<samples>\d+) "
    r"bytes=(?P<bytes>\d+) hash=0x(?P<hash>[0-9a-fA-F]+) "
    r"center_pad=(?P<center_pad>\d+) window=(?P<window>\d+) "
    r"hop=(?P<hop>\d+) frames=(?P<frames>\d+)"
)


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
    pcm_chunks: dict[int, bytes] = field(default_factory=dict)
    raw_pcm_chunks: dict[int, bytes] = field(default_factory=dict)
    feat_total_chunks: int | None = None
    input_total_chunks: int | None = None
    output_total_chunks: int | None = None
    pcm_total_chunks: int | None = None
    raw_pcm_total_chunks: int | None = None
    pcm_sample_rate: int | None = None
    pcm_samples: int | None = None
    pcm_bytes: int | None = None
    pcm_hash: int | None = None
    pcm_center_pad: int | None = None
    pcm_window: int | None = None
    pcm_hop: int | None = None
    pcm_frames: int | None = None
    raw_pcm_sample_rate: int | None = None
    raw_pcm_channels: int | None = None
    raw_pcm_samples: int | None = None
    raw_pcm_bytes: int | None = None
    raw_pcm_hash: int | None = None
    raw_pcm_center_pad: int | None = None
    raw_pcm_window: int | None = None
    raw_pcm_hop: int | None = None
    raw_pcm_frames: int | None = None
    chunk_parse_errors: list[str] = field(default_factory=list)
    ended: bool = False

    def chunk_complete(self, chunks: dict[int, bytes], total: int | None) -> bool:
        return total is not None and len(chunks) == total

    def require_replayable(self) -> None:
        missing: list[str] = []
        if self.shape is None:
            missing.append("begin")
        if self.feat_hash is None:
            missing.append("meta")
        if not self.chunk_complete(self.feat_chunks, self.feat_total_chunks):
            missing.append("feat_f32")
        if not self.chunk_complete(self.output_chunks, self.output_total_chunks):
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
                hex_payload = match.group("hex")
                if label == "feat_f32":
                    record.feat_total_chunks = total
                elif label == "input_raw":
                    record.input_total_chunks = total
                elif label == "output_raw":
                    record.output_total_chunks = total
                elif label in ("preproc_s16", "pcm_s16"):
                    record.pcm_total_chunks = total
                elif label == "raw_capture_s16":
                    record.raw_pcm_total_chunks = total
                try:
                    payload = bytes.fromhex(hex_payload)
                except ValueError as exc:
                    record.chunk_parse_errors.append(
                        f"{label} chunk={chunk_idx}/{total} parse_error={exc}"
                    )
                    continue
                if label == "feat_f32":
                    record.feat_chunks[chunk_idx] = payload
                elif label == "input_raw":
                    record.input_chunks[chunk_idx] = payload
                elif label == "output_raw":
                    record.output_chunks[chunk_idx] = payload
                elif label in ("preproc_s16", "pcm_s16"):
                    record.pcm_chunks[chunk_idx] = payload
                elif label == "raw_capture_s16":
                    record.raw_pcm_chunks[chunk_idx] = payload
                continue

            if match := PCM_META_RE.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                if record.infer is None:
                    record.infer = int(match.group("infer"))
                record.pcm_sample_rate = int(match.group("sample_rate"))
                record.pcm_samples = int(match.group("samples"))
                record.pcm_bytes = int(match.group("bytes"))
                record.pcm_hash = int(match.group("hash"), 16)
                record.pcm_center_pad = int(match.group("center_pad"))
                record.pcm_window = int(match.group("window"))
                record.pcm_hop = int(match.group("hop"))
                record.pcm_frames = int(match.group("frames"))
                continue

            if match := RAW_PCM_META_RE.search(line):
                seq = int(match.group("seq"))
                record = records.setdefault(seq, DumpRecord(seq=seq))
                if record.infer is None:
                    record.infer = int(match.group("infer"))
                record.raw_pcm_sample_rate = int(match.group("sample_rate"))
                record.raw_pcm_channels = int(match.group("channels"))
                record.raw_pcm_samples = int(match.group("samples"))
                record.raw_pcm_bytes = int(match.group("bytes"))
                record.raw_pcm_hash = int(match.group("hash"), 16)
                record.raw_pcm_center_pad = int(match.group("center_pad"))
                record.raw_pcm_window = int(match.group("window"))
                record.raw_pcm_hop = int(match.group("hop"))
                record.raw_pcm_frames = int(match.group("frames"))
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


def decode_exact_output_scalar(output_bytes: bytes, out_type: str) -> float | None:
    if out_type == "float32":
        return float(np.frombuffer(output_bytes, dtype="<f4")[0])
    return None


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
    parser.add_argument(
        "--builtin-ref",
        action="store_true",
        help=(
            "use the TensorFlow Lite builtin reference resolver on host to avoid "
            "delegate-level FP32 drift during exact board parity checks"
        ),
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
    record.require_replayable()

    feature_bytes = record.assemble(record.feat_chunks, record.feat_total_chunks)
    output_bytes = record.assemble(record.output_chunks, record.output_total_chunks)
    feature_raw_hash = fnv1a32(feature_bytes)
    input_bytes: bytes | None = None
    input_parse_issue: str | None = None

    if record.chunk_complete(record.input_chunks, record.input_total_chunks):
        candidate_input_bytes = record.assemble(record.input_chunks, record.input_total_chunks)
        if record.input_bytes is not None and len(candidate_input_bytes) != record.input_bytes:
            input_parse_issue = (
                f"input bytes mismatch: parsed={len(candidate_input_bytes)}"
                f" expected={record.input_bytes}"
            )
        else:
            input_bytes = candidate_input_bytes
    elif record.input_total_chunks is None:
        input_parse_issue = "input_raw chunk count missing"
    else:
        input_parse_issue = (
            f"input_raw incomplete: parsed={len(record.input_chunks)}/{record.input_total_chunks}"
        )

    if record.feat_bytes is not None and len(feature_bytes) != record.feat_bytes:
        raise SystemExit(
            f"feature bytes mismatch: parsed={len(feature_bytes)} expected={record.feat_bytes}"
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
    recomputed_input_hash: int | None = None
    requantized_input: bytes | None = None
    quant_diff: int | None = None
    quant_first_diff: list[int] | None = None

    if input_bytes is not None:
        recomputed_input_hash = fnv1a32(input_bytes)
        requantized_input = quantize_feature_tensor(
            feature_tensor, record.in_type, record.in_scale, record.in_zp
        )
        quant_diff = sum(a != b for a, b in zip(requantized_input, input_bytes))
        quant_first_diff = first_diff_indices(requantized_input, input_bytes)

    effective_input_bytes = input_bytes
    effective_input_source = "input_raw"
    if (
        record.in_type == "float32"
        and record.input_hash == feature_raw_hash
        and record.input_bytes == len(feature_bytes)
        and (
            recomputed_input_hash is None or record.input_hash != recomputed_input_hash
        )
    ):
        # Some board logs carry a corrupted or truncated input_raw stream even
        # though the recorded input_hash still matches the feature tensor's raw
        # float bytes. For this debug path, replay the exact feature bytes.
        effective_input_bytes = feature_bytes
        effective_input_source = (
            "feat_f32_fallback" if input_bytes is not None else "feat_f32_missing_input_raw"
        )

    if effective_input_bytes is None:
        detail = input_parse_issue or "input_raw unavailable"
        if record.chunk_parse_errors:
            detail += f"; parse_errors={'; '.join(record.chunk_parse_errors)}"
        raise SystemExit(f"dump seq={record.seq} unusable: {detail}")

    effective_input_hash = fnv1a32(effective_input_bytes)

    import tensorflow as tf

    interpreter_kwargs: dict[str, Any] = {"model_path": str(args.model)}
    if args.builtin_ref:
        interpreter_kwargs["experimental_op_resolver_type"] = (
            tf.lite.experimental.OpResolverType.BUILTIN_REF
        )

    interpreter = tf.lite.Interpreter(**interpreter_kwargs)
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
    board_exact_score = decode_exact_output_scalar(output_bytes, record.out_type)
    host_exact_score = decode_exact_output_scalar(host_output_bytes, record.out_type)

    print(f"dump_seq={record.seq} infer={record.infer} gate={record.gate}")
    print(
        "board_meta:"
        f" input_type={record.in_type} output_type={record.out_type}"
        f" shape={record.shape} layout={record.layout}"
    )
    print(f"host_runtime: builtin_ref={'yes' if args.builtin_ref else 'no'}")
    print(
        "board_hash:"
        f" feature=0x{record.feat_hash:08x} input=0x{record.input_hash:08x}"
    )
    print(
        "host_hash:"
        f" feature=0x{recomputed_feature_hash:08x}"
        f" logged_input={'n/a' if recomputed_input_hash is None else f'0x{recomputed_input_hash:08x}'}"
        f" effective_input=0x{effective_input_hash:08x}"
        f" source={effective_input_source}"
    )
    if quant_diff is not None and quant_first_diff is not None:
        print(
            "quant_parity:"
            f" diff_bytes={quant_diff}/{len(input_bytes)}"
            f" first_diff={quant_first_diff}"
        )
    else:
        print(
            "quant_parity:"
            f" unavailable ({input_parse_issue or 'input_raw not fully parsed'})"
        )
    print(
        "board_output:"
        f" raw={record.raw} score={record.score:.6f}"
        f"{'' if board_exact_score is None else f' exact={board_exact_score:.6f}'}"
        f" q15={record.q15}"
    )
    print(
        "host_output:"
        f" raw={host_raw} score={host_score:.6f}"
        f"{'' if host_exact_score is None else f' exact={host_exact_score:.6f}'}"
    )
    print(
        "output_parity:"
        f" bytes_equal={'yes' if host_output_bytes == output_bytes else 'no'}"
        f" raw_equal={'yes' if host_raw == record.raw else 'no'}"
        f" first_diff={first_diff_indices(host_output_bytes, output_bytes)}"
    )

    if record.feat_hash != recomputed_feature_hash:
        print("warning: feature hash mismatch", file=sys.stderr)
    if recomputed_input_hash is not None and record.input_hash != recomputed_input_hash:
        print("warning: input hash mismatch", file=sys.stderr)
    if record.chunk_parse_errors:
        print(
            "note: skipped malformed dump chunks: " + "; ".join(record.chunk_parse_errors),
            file=sys.stderr,
        )
    if effective_input_source != "input_raw":
        print(
            "note: using feature tensor bytes as effective input because they match the board input hash",
            file=sys.stderr,
        )
    if host_output_bytes != output_bytes:
        print("warning: host replay output does not match board dump", file=sys.stderr)
        if record.out_type == "float32" and not args.builtin_ref:
            print(
                "note: retry with --builtin-ref to remove host delegate drift from "
                "FP32 exact-parity checks",
                file=sys.stderr,
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
