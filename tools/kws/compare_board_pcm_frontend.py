#!/usr/bin/env python3
"""Compare a board KWS PCM snapshot with board and training frontend features."""

from __future__ import annotations

import argparse
import json
import math
import wave
from pathlib import Path

import numpy as np

from replay_board_tensor_dump import decode_feature_hash, fnv1a32, parse_dump_records


DEFAULT_CONTRACT = Path(
    "/root/kws-trainint/artifacts/exports/"
    "student_conv_resnet_ed_nano_teacher_a_new_target_cycle24_2s_v1/"
    "frontend_contract.json"
)


def htk_hz_to_mel(freq: float) -> float:
    return 2595.0 * math.log10(1.0 + (freq / 700.0))


def htk_mel_to_hz(mels: np.ndarray) -> np.ndarray:
    return 700.0 * ((10.0 ** (mels / 2595.0)) - 1.0)


def firmware_hz_to_mel(freq: float) -> float:
    f_sp = 200.0 / 3.0
    min_log_hz = 1000.0
    min_log_mel = min_log_hz / f_sp
    logstep = math.log(6.4) / 27.0
    if freq < min_log_hz:
        return freq / f_sp
    return min_log_mel + math.log(freq / min_log_hz) / logstep


def firmware_mel_to_hz(mels: np.ndarray) -> np.ndarray:
    f_sp = 200.0 / 3.0
    min_log_hz = 1000.0
    min_log_mel = min_log_hz / f_sp
    logstep = math.log(6.4) / 27.0
    linear = mels * f_sp
    logarithmic = min_log_hz * np.exp(logstep * (mels - min_log_mel))
    return np.where(mels < min_log_mel, linear, logarithmic)


def normalize(features: np.ndarray, correction: int) -> np.ndarray:
    values = features.astype(np.float64, copy=False)
    mean = float(values.mean())
    denom = max(values.size - correction, 1)
    std = math.sqrt(float(np.square(values - mean).sum()) / float(denom))
    if std < 1.0e-5:
        std = 1.0
    return ((values - mean) / std).astype(np.float32)


def periodic_hann(size: int) -> np.ndarray:
    return (0.5 - 0.5 * np.cos((2.0 * np.pi * np.arange(size)) / size)).astype(
        np.float32
    )


def symmetric_hann(size: int) -> np.ndarray:
    return (
        0.5
        - 0.5 * np.cos((2.0 * np.pi * np.arange(size)) / float(size - 1))
    ).astype(np.float32)


def torchaudio_mel_fbanks(
    n_freqs: int,
    f_min: float,
    f_max: float,
    n_mels: int,
    sample_rate: int,
    norm: str | None,
) -> np.ndarray:
    all_freqs = np.linspace(0.0, float(sample_rate // 2), n_freqs, dtype=np.float64)
    m_min = htk_hz_to_mel(f_min)
    m_max = htk_hz_to_mel(f_max)
    m_pts = np.linspace(m_min, m_max, n_mels + 2, dtype=np.float64)
    f_pts = htk_mel_to_hz(m_pts)
    f_diff = f_pts[1:] - f_pts[:-1]
    slopes = f_pts[np.newaxis, :] - all_freqs[:, np.newaxis]
    down_slopes = (-1.0 * slopes[:, :-2]) / f_diff[:-1]
    up_slopes = slopes[:, 2:] / f_diff[1:]
    fb = np.maximum(0.0, np.minimum(down_slopes, up_slopes))
    if norm == "slaney":
        fb *= (2.0 / (f_pts[2 : n_mels + 2] - f_pts[:n_mels]))[np.newaxis, :]
    return fb.astype(np.float32)


def frame_power(
    waveform: np.ndarray,
    *,
    n_fft: int,
    win_length: int,
    hop_length: int,
    window: np.ndarray,
) -> np.ndarray:
    frame_count = 1 + ((waveform.shape[0] - n_fft) // hop_length)
    powers = np.empty((frame_count, (n_fft // 2) + 1), dtype=np.float32)
    for frame_index in range(frame_count):
        start = frame_index * hop_length
        frame = waveform[start : start + win_length].astype(np.float32, copy=False)
        spectrum = np.fft.rfft(frame * window, n=n_fft)
        powers[frame_index] = (spectrum.real * spectrum.real) + (
            spectrum.imag * spectrum.imag
        )
    return powers


def training_frontend(
    samples: np.ndarray,
    contract: dict,
    *,
    sample_count: int | None,
    pad_mode: str,
    max_frames: int | None,
) -> np.ndarray:
    audio = contract["audio"]
    frontend = contract["frontend"]
    sample_rate = int(audio["sample_rate_hz"])
    n_fft = int(frontend["n_fft"])
    win_length = int(frontend["win_length"])
    hop_length = int(frontend["hop_length"])
    n_mels = int(frontend["feature_dim"])
    if sample_count is not None:
        if samples.shape[0] > sample_count:
            samples = samples[:sample_count]
        elif samples.shape[0] < sample_count:
            samples = np.pad(samples, (0, sample_count - samples.shape[0]))
    waveform = samples.astype(np.float32, copy=False) / 32768.0
    if bool(frontend.get("center", True)):
        pad = n_fft // 2
        waveform = np.pad(waveform, (pad, pad), mode=pad_mode)
    powers = frame_power(
        waveform,
        n_fft=n_fft,
        win_length=win_length,
        hop_length=hop_length,
        window=periodic_hann(win_length),
    )
    fb = torchaudio_mel_fbanks(
        (n_fft // 2) + 1,
        float(frontend["f_min_hz"]),
        float(frontend["f_max_hz"]),
        n_mels,
        sample_rate,
        norm=None,
    )
    mel = np.matmul(powers, fb)
    if max_frames is not None:
        mel = mel[:max_frames]
    features = np.log(np.maximum(mel.T, 1.0e-5))
    return normalize(features, correction=1)


def board_frontend_from_union(samples: np.ndarray, contract: dict) -> np.ndarray:
    audio = contract["audio"]
    frontend = contract["frontend"]
    sample_rate = int(audio["sample_rate_hz"])
    n_fft = int(frontend["n_fft"])
    win_length = int(frontend["win_length"])
    hop_length = int(frontend["hop_length"])
    n_mels = int(frontend["feature_dim"])
    frame_count = int(frontend["frame_count"])
    powers = frame_power(
        samples.astype(np.float32, copy=False),
        n_fft=n_fft,
        win_length=win_length,
        hop_length=hop_length,
        window=symmetric_hann(win_length),
    )[:frame_count]

    mel_min = firmware_hz_to_mel(float(frontend["f_min_hz"]))
    mel_max = firmware_hz_to_mel(float(frontend["f_max_hz"]))
    mel_points = np.linspace(mel_min, mel_max, n_mels + 2, dtype=np.float64)
    hz_points = firmware_mel_to_hz(mel_points)
    fft_bins = (n_fft // 2) + 1
    mel = np.zeros((frame_count, n_mels), dtype=np.float32)
    for band in range(n_mels):
        left_hz = float(hz_points[band])
        center_hz = float(hz_points[band + 1])
        right_hz = float(hz_points[band + 2])
        start_bin = int(math.floor(((win_length + 1) * left_hz) / sample_rate))
        center_bin = int(math.floor(((win_length + 1) * center_hz) / sample_rate))
        end_bin = int(math.floor(((win_length + 1) * right_hz) / sample_rate))
        start_bin = min(start_bin, fft_bins - 1)
        center_bin = min(max(center_bin, start_bin + 1), fft_bins - 1)
        end_bin = min(max(end_bin, center_bin + 1), fft_bins - 1)
        energy = np.zeros(frame_count, dtype=np.float64)
        for bin_index in range(start_bin, center_bin):
            denom = float(center_bin - start_bin)
            weight = ((bin_index - start_bin) / denom) if denom > 0.0 else 0.0
            energy += powers[:, bin_index].astype(np.float64) * weight
        for bin_index in range(center_bin, end_bin + 1):
            denom = float(end_bin - center_bin)
            weight = ((end_bin - bin_index) / denom) if denom > 0.0 else 0.0
            energy += powers[:, bin_index].astype(np.float64) * max(weight, 0.0)
        energy *= 2.0 / max(right_hz - left_hz, 1.0)
        mel[:, band] = np.log(np.maximum(energy, 1.0e-5)).astype(np.float32)
    return normalize(mel.T, correction=0)


def compare(name: str, candidate: np.ndarray, board: np.ndarray) -> str:
    diff = candidate.astype(np.float64) - board.astype(np.float64)
    flat_candidate = candidate.reshape(-1).astype(np.float64)
    flat_board = board.reshape(-1).astype(np.float64)
    corr = float(np.corrcoef(flat_candidate, flat_board)[0, 1])
    abs_diff = np.abs(diff)
    return (
        f"{name}: shape={candidate.shape} "
        f"mae={abs_diff.mean():.6f} rmse={math.sqrt(float(np.square(diff).mean())):.6f} "
        f"max_abs={abs_diff.max():.6f} p95={np.percentile(abs_diff, 95):.6f} "
        f"corr={corr:.6f} feature_hash=0x{decode_feature_hash(candidate.reshape(1, *candidate.shape, 1)):08x}"
    )


def compare_pcm(name: str, candidate: np.ndarray, reference: np.ndarray) -> str:
    count = min(candidate.shape[0], reference.shape[0])
    if count == 0:
        return f"{name}: empty"
    diff = candidate[:count].astype(np.int32) - reference[:count].astype(np.int32)
    abs_diff = np.abs(diff)
    return (
        f"{name}: samples={count} mae={abs_diff.mean():.3f} "
        f"max_abs={int(abs_diff.max())} "
        f"exact={(int(np.count_nonzero(diff == 0)) / count):.6f}"
    )


def run_tflite_scores(
    model_path: Path,
    board: np.ndarray,
    candidates: list[tuple[str, np.ndarray]],
) -> list[tuple[str, float]]:
    import tensorflow as tf

    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    scores: list[tuple[str, float]] = []
    for name, feature in [("board_feature", board), *candidates]:
        tensor = feature.astype(np.float32, copy=False).reshape(1, *feature.shape, 1)
        interpreter.set_tensor(input_details["index"], tensor)
        interpreter.invoke()
        output = interpreter.get_tensor(output_details["index"])
        scores.append((name, float(output.reshape(-1)[0])))
    return scores


def write_wav(path: Path, samples: np.ndarray, channels: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if channels == 1:
        payload = samples.reshape(-1).astype("<i2", copy=False)
    else:
        payload = samples.reshape(-1, channels).astype("<i2", copy=False)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(2)
        handle.setframerate(16000)
        handle.writeframes(payload.tobytes())


def infer_shape_from_contract(feature_bytes: bytes, contract: dict) -> tuple[int, int, int, int]:
    frontend = contract["frontend"]
    shape = (1, int(frontend["feature_dim"]), int(frontend["frame_count"]), 1)
    expected_bytes = int(np.prod(shape)) * np.dtype("<f4").itemsize
    if len(feature_bytes) != expected_bytes:
        raise SystemExit(
            f"dump missing tensor shape and feature byte count does not match "
            f"contract: parsed={len(feature_bytes)} expected={expected_bytes}"
        )
    return shape


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--seq", default="latest")
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--require-raw", action="store_true")
    parser.add_argument("--model", type=Path)
    args = parser.parse_args()

    records = parse_dump_records(args.log)
    if not records:
        raise SystemExit(f"no board dump found in {args.log}")
    seq = max(records) if args.seq == "latest" else int(args.seq)
    record = records.get(seq)
    if record is None:
        raise SystemExit(f"dump seq={seq} not found")
    if not record.chunk_complete(record.feat_chunks, record.feat_total_chunks):
        raise SystemExit(f"dump seq={seq} missing complete feat_f32 chunks")
    if not record.chunk_complete(record.pcm_chunks, record.pcm_total_chunks):
        raise SystemExit(f"dump seq={seq} missing complete preproc_s16 chunks")
    raw_available = record.chunk_complete(
        record.raw_pcm_chunks,
        record.raw_pcm_total_chunks,
    )
    if args.require_raw and not raw_available:
        raise SystemExit(f"dump seq={seq} missing complete raw_capture_s16 chunks")

    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    feature_bytes = record.assemble(record.feat_chunks, record.feat_total_chunks)
    if record.shape is None:
        record.shape = infer_shape_from_contract(feature_bytes, contract)
    if record.layout is None:
        record.layout = "mels_frames"
    pcm_bytes = record.assemble(record.pcm_chunks, record.pcm_total_chunks)
    if record.pcm_bytes is not None and len(pcm_bytes) != record.pcm_bytes:
        raise SystemExit(
            f"pcm byte mismatch: parsed={len(pcm_bytes)} expected={record.pcm_bytes}"
        )
    pcm_hash = fnv1a32(pcm_bytes)
    if record.pcm_hash is not None and pcm_hash != record.pcm_hash:
        raise SystemExit(
            f"pcm hash mismatch: parsed=0x{pcm_hash:08x} expected=0x{record.pcm_hash:08x}"
        )

    raw_feature = np.frombuffer(feature_bytes, dtype="<f4").reshape(record.shape)
    if record.layout == "mels_frames":
        board = raw_feature[0, :, :, 0]
    elif record.layout == "frames_mels":
        board = raw_feature[0, :, :, 0].T
    else:
        raise SystemExit(f"unsupported layout: {record.layout}")

    pcm_union = np.frombuffer(pcm_bytes, dtype="<i2").copy()
    center_pad = record.pcm_center_pad if record.pcm_center_pad is not None else 200
    stream_actual = pcm_union[center_pad:]
    window_samples = int(contract["audio"]["window_samples"])
    frame_count = int(contract["frontend"]["frame_count"])

    candidates = [
        ("board_numpy_from_pcm_union", board_frontend_from_union(pcm_union, contract)),
        (
            "training_32000_reflect",
            training_frontend(
                stream_actual,
                contract,
                sample_count=window_samples,
                pad_mode="reflect",
                max_frames=frame_count,
            ),
        ),
        (
            "training_32000_constant",
            training_frontend(
                stream_actual,
                contract,
                sample_count=window_samples,
                pad_mode="constant",
                max_frames=frame_count,
            ),
        ),
        (
            "training_32200_reflect_first201",
            training_frontend(
                stream_actual,
                contract,
                sample_count=None,
                pad_mode="reflect",
                max_frames=frame_count,
            ),
        ),
    ]

    raw_union = None
    raw_actual = None
    raw_channels = 0
    if raw_available:
        raw_bytes = record.assemble(record.raw_pcm_chunks, record.raw_pcm_total_chunks)
        if record.raw_pcm_bytes is not None and len(raw_bytes) != record.raw_pcm_bytes:
            raise SystemExit(
                f"raw pcm byte mismatch: parsed={len(raw_bytes)} "
                f"expected={record.raw_pcm_bytes}"
            )
        raw_hash = fnv1a32(raw_bytes)
        if record.raw_pcm_hash is not None and raw_hash != record.raw_pcm_hash:
            raise SystemExit(
                f"raw pcm hash mismatch: parsed=0x{raw_hash:08x} "
                f"expected=0x{record.raw_pcm_hash:08x}"
            )
        raw_channels = record.raw_pcm_channels or 0
        if raw_channels <= 0:
            raise SystemExit(f"dump seq={seq} raw_capture_s16 missing channel count")
        raw_flat = np.frombuffer(raw_bytes, dtype="<i2").copy()
        if raw_flat.size % raw_channels != 0:
            raise SystemExit(
                f"raw pcm sample count {raw_flat.size} is not divisible by "
                f"channels={raw_channels}"
            )
        raw_union = raw_flat.reshape(-1, raw_channels)
        raw_center_pad = (
            record.raw_pcm_center_pad
            if record.raw_pcm_center_pad is not None
            else center_pad
        )
        raw_actual = raw_union[raw_center_pad:]
        raw_primary_union = raw_union[:, 0]
        raw_primary_actual = raw_actual[:, 0]
        candidates.extend(
            [
                (
                    "board_numpy_from_raw_ch0_union",
                    board_frontend_from_union(raw_primary_union, contract),
                ),
                (
                    "training_raw_ch0_32000_reflect",
                    training_frontend(
                        raw_primary_actual,
                        contract,
                        sample_count=window_samples,
                        pad_mode="reflect",
                        max_frames=frame_count,
                    ),
                ),
                (
                    "training_raw_ch0_32000_constant",
                    training_frontend(
                        raw_primary_actual,
                        contract,
                        sample_count=window_samples,
                        pad_mode="constant",
                        max_frames=frame_count,
                    ),
                ),
            ]
        )

    print(f"dump_seq={seq} infer={record.infer} gate={record.gate}")
    print(
        f"board_feature: shape={board.shape} hash=0x{decode_feature_hash(raw_feature):08x} "
        f"logged=0x{record.feat_hash or 0:08x}"
    )
    print(
        f"pcm: samples={pcm_union.shape[0]} bytes={len(pcm_bytes)} hash=0x{pcm_hash:08x} "
        f"center_pad={center_pad} actual_samples={stream_actual.shape[0]}"
    )
    if raw_available and raw_union is not None and raw_actual is not None:
        print(
            f"raw_capture: frames={raw_union.shape[0]} channels={raw_channels} "
            f"bytes={record.raw_pcm_bytes or raw_union.nbytes} "
            f"hash=0x{record.raw_pcm_hash or fnv1a32(raw_union.astype('<i2', copy=False).tobytes()):08x} "
            f"actual_frames={raw_actual.shape[0]}"
        )
        print(compare_pcm("preproc_vs_raw_ch0_union", pcm_union, raw_union[:, 0]))
        print(compare_pcm("preproc_vs_raw_ch0_actual", stream_actual, raw_actual[:, 0]))
    else:
        print("raw_capture: missing")
    for name, candidate in candidates:
        print(compare(name, candidate, board))
    if args.model is not None:
        for name, score in run_tflite_scores(args.model, board, candidates):
            print(f"model_score[{name}]={score:.6f}")

    if args.out_dir is not None:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        write_wav(args.out_dir / f"kws_seq{seq}_preproc_union.wav", pcm_union)
        write_wav(args.out_dir / f"kws_seq{seq}_preproc_actual.wav", stream_actual)
        if raw_available and raw_union is not None and raw_actual is not None:
            write_wav(
                args.out_dir / f"kws_seq{seq}_raw_capture_union.wav",
                raw_union,
                channels=raw_channels,
            )
            write_wav(
                args.out_dir / f"kws_seq{seq}_raw_capture_actual.wav",
                raw_actual,
                channels=raw_channels,
            )
            write_wav(
                args.out_dir / f"kws_seq{seq}_raw_ch0_actual.wav",
                raw_actual[:, 0],
            )
        np.save(args.out_dir / f"kws_seq{seq}_board_feat.npy", board)
        for name, candidate in candidates:
            np.save(args.out_dir / f"kws_seq{seq}_{name}.npy", candidate)
        print(f"wrote={args.out_dir}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
