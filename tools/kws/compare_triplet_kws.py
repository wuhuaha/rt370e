#!/usr/bin/env python3
"""Compare training frontend, board-faithful frontend, and TFLite/PT outputs."""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np
import tensorflow as tf
import torch


REPO_ROOT = Path(__file__).resolve().parents[2]
TRAINING_ROOT = Path("/root/kws-training-pro")
TRAINING_MODEL_DIR = TRAINING_ROOT / "models" / "bc_resnet_iteration3"
DEFAULT_CHECKPOINT = TRAINING_MODEL_DIR / "bc_resnet_best.pth"
DEFAULT_TFLITE = TRAINING_MODEL_DIR / "bc_resnet_v3_production_final_v2.tflite"
DEFAULT_FFT_LIB = Path("/tmp/libriver_webrtc_fft.so")

sys.path.insert(0, str(TRAINING_ROOT))

from export_bc_resnet_tflite import TorchExportBCResNet, remap_checkpoint_state_dict  # type: ignore  # noqa: E402
from river_kws_features import (  # type: ignore  # noqa: E402
    RIVER_KWS_FEATURE_DB_MIN,
    RIVER_KWS_FEATURE_FRAMES,
    RIVER_KWS_FEATURE_MEAN,
    RIVER_KWS_FEATURE_STD,
    RIVER_KWS_FMAX_HZ,
    RIVER_KWS_FMIN_HZ,
    RIVER_KWS_HOP_SAMPLES,
    RIVER_KWS_MEL_BINS,
    RIVER_KWS_SAMPLE_RATE_HZ,
    RIVER_KWS_WINDOW_SAMPLES,
    RiverKwsFeatureExtractor,
    RiverKwsFrontendConfig,
    hz_to_mel,
    load_wav_mono_int16,
    mel_to_hz,
)


@dataclass
class FeatureStats:
    mean: float
    std: float
    min: float
    max: float


@dataclass
class ScoreStats:
    score: float
    raw_output: int | None


@dataclass
class SampleReport:
    audio_path: str
    training_feature: FeatureStats
    board_feature: FeatureStats
    feature_diff_mean_abs: float
    feature_diff_max_abs: float
    quant_diff_count: int
    quant_total: int
    pt_score_training: ScoreStats
    pt_score_board: ScoreStats
    tflite_score_training: ScoreStats
    tflite_score_board: ScoreStats


def round_to_i32(values: np.ndarray) -> np.ndarray:
    return np.trunc(np.where(values >= 0.0, values + 0.5, values - 0.5)).astype(
        np.int32, copy=False
    )


def clamp_i16(values: np.ndarray) -> np.ndarray:
    return np.clip(values, -32768, 32767).astype(np.int16, copy=False)


def clamp_i8(values: np.ndarray) -> np.ndarray:
    return np.clip(values, -128, 127).astype(np.int8, copy=False)


def detect_repo_cpp_hann() -> np.ndarray:
    window = np.zeros(RIVER_KWS_WINDOW_SAMPLES, dtype=np.float32)
    denom = float(RIVER_KWS_WINDOW_SAMPLES - 1)
    for index in range(RIVER_KWS_WINDOW_SAMPLES):
        window[index] = 0.5 - 0.5 * math.cos((2.0 * math.pi * index) / denom)
    return window


def build_fft_library(output_path: Path) -> Path:
    build_dir = output_path.parent / f"{output_path.stem}_build"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)
    include_dir = REPO_ROOT / "third_party" / "webrtc_aecm" / "aecm"
    real_fft_obj = build_dir / "real_fft.o"
    complex_fft_obj = build_dir / "complex_fft.o"
    spl_obj = build_dir / "signal_processing_library.o"

    subprocess.run(
        [
            "gcc",
            "-fPIC",
            "-O2",
            "-c",
            str(include_dir / "real_fft.c"),
            "-I",
            str(include_dir),
            "-o",
            str(real_fft_obj),
        ],
        check=True,
    )
    subprocess.run(
        [
            "gcc",
            "-fPIC",
            "-O2",
            "-c",
            str(include_dir / "complex_fft.c"),
            "-I",
            str(include_dir),
            "-o",
            str(complex_fft_obj),
        ],
        check=True,
    )
    subprocess.run(
        [
            "g++",
            "-fPIC",
            "-O2",
            "-std=c++17",
            "-c",
            str(include_dir / "signal_processing_library.cc"),
            "-I",
            str(include_dir),
            "-o",
            str(spl_obj),
        ],
        check=True,
    )
    subprocess.run(
        [
            "g++",
            "-shared",
            "-o",
            str(output_path),
            str(real_fft_obj),
            str(complex_fft_obj),
            str(spl_obj),
        ],
        check=True,
    )
    return output_path


class WebRtcRealFft:
    def __init__(self, lib_path: Path) -> None:
        self.lib = ctypes.CDLL(str(lib_path))
        self.lib.WebRtcSpl_CreateRealFFT.argtypes = [ctypes.c_int]
        self.lib.WebRtcSpl_CreateRealFFT.restype = ctypes.c_void_p
        self.lib.WebRtcSpl_FreeRealFFT.argtypes = [ctypes.c_void_p]
        self.lib.WebRtcSpl_FreeRealFFT.restype = None
        self.lib.WebRtcSpl_RealForwardFFT.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_int16),
            ctypes.POINTER(ctypes.c_int16),
        ]
        self.lib.WebRtcSpl_RealForwardFFT.restype = ctypes.c_int
        self.handle = self.lib.WebRtcSpl_CreateRealFFT(9)
        if not self.handle:
            raise RuntimeError("WebRtcSpl_CreateRealFFT(9) failed")

    def forward(self, frame_int16: np.ndarray) -> np.ndarray:
        input_arr = np.ascontiguousarray(frame_int16, dtype=np.int16)
        output_arr = np.zeros(RIVER_KWS_WINDOW_SAMPLES + 2, dtype=np.int16)
        status = self.lib.WebRtcSpl_RealForwardFFT(
            self.handle,
            input_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
            output_arr.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        )
        if status != 0:
            raise RuntimeError(f"WebRtcSpl_RealForwardFFT failed: {status}")
        return output_arr

    def close(self) -> None:
        if self.handle:
            self.lib.WebRtcSpl_FreeRealFFT(self.handle)
            self.handle = None

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass


class BoardExactFeatureExtractor:
    def __init__(self, fft_lib_path: Path) -> None:
        self.fft = WebRtcRealFft(fft_lib_path)
        self.hann_window = detect_repo_cpp_hann()
        self.mel_start_bin, self.mel_center_bin, self.mel_end_bin, self.mel_band_norm = (
            self._prepare_mel_bands()
        )

    def _prepare_mel_bands(self) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        point_count = RIVER_KWS_MEL_BINS + 2
        fft_bins = (RIVER_KWS_WINDOW_SAMPLES // 2) + 1
        mel_min = hz_to_mel(RIVER_KWS_FMIN_HZ)
        mel_max = hz_to_mel(RIVER_KWS_FMAX_HZ)
        mel_step = (mel_max - mel_min) / float(point_count - 1)

        start_bins = np.zeros(RIVER_KWS_MEL_BINS, dtype=np.int32)
        center_bins = np.zeros(RIVER_KWS_MEL_BINS, dtype=np.int32)
        end_bins = np.zeros(RIVER_KWS_MEL_BINS, dtype=np.int32)
        band_norm = np.zeros(RIVER_KWS_MEL_BINS, dtype=np.float32)

        hz_points = np.array(
            [mel_to_hz(mel_min + mel_step * idx) for idx in range(point_count)],
            dtype=np.float32,
        )

        for idx in range(RIVER_KWS_MEL_BINS):
            left_hz = hz_points[idx]
            center_hz = hz_points[idx + 1]
            right_hz = hz_points[idx + 2]

            start_bin = int(math.floor(((RIVER_KWS_WINDOW_SAMPLES + 1) * left_hz) / RIVER_KWS_SAMPLE_RATE_HZ))
            center_bin = int(math.floor(((RIVER_KWS_WINDOW_SAMPLES + 1) * center_hz) / RIVER_KWS_SAMPLE_RATE_HZ))
            end_bin = int(math.floor(((RIVER_KWS_WINDOW_SAMPLES + 1) * right_hz) / RIVER_KWS_SAMPLE_RATE_HZ))

            start_bin = min(start_bin, fft_bins - 1)
            center_bin = min(max(center_bin, start_bin + 1), fft_bins - 1)
            end_bin = min(max(end_bin, center_bin + 1), fft_bins - 1)

            start_bins[idx] = start_bin
            center_bins[idx] = center_bin
            end_bins[idx] = end_bin
            band_norm[idx] = 2.0 / max(right_hz - left_hz, 1.0)

        return start_bins, center_bins, end_bins, band_norm

    def _compute_power_bins(self, frame_int16: np.ndarray) -> np.ndarray:
        scaled = frame_int16.astype(np.float32) * self.hann_window
        rounded = clamp_i16(round_to_i32(scaled))
        fft_output = self.fft.forward(rounded)

        power_bins = np.zeros((RIVER_KWS_WINDOW_SAMPLES // 2) + 1, dtype=np.float32)
        power_bins[0] = float(fft_output[0]) * float(fft_output[0])
        for band in range(1, power_bins.shape[0] - 1):
            real = float(fft_output[band * 2])
            imag = float(fft_output[(band * 2) + 1])
            power_bins[band] = (real * real) + (imag * imag)
        power_bins[-1] = float(fft_output[RIVER_KWS_WINDOW_SAMPLES]) * float(
            fft_output[RIVER_KWS_WINDOW_SAMPLES]
        )
        return power_bins

    def _compute_mel_frame(self, power_bins: np.ndarray) -> np.ndarray:
        mel_frame = np.zeros(RIVER_KWS_MEL_BINS, dtype=np.float32)
        for band in range(RIVER_KWS_MEL_BINS):
            start_bin = int(self.mel_start_bin[band])
            center_bin = int(self.mel_center_bin[band])
            end_bin = int(self.mel_end_bin[band])
            energy = 0.0

            for bin_index in range(start_bin, center_bin):
                denom = float(center_bin - start_bin)
                weight = ((bin_index - start_bin) / denom) if denom > 0.0 else 0.0
                energy += float(power_bins[bin_index]) * weight

            for bin_index in range(center_bin, end_bin + 1):
                denom = float(end_bin - center_bin)
                weight = ((end_bin - bin_index) / denom) if denom > 0.0 else 0.0
                if weight < 0.0:
                    weight = 0.0
                energy += float(power_bins[bin_index]) * weight

            energy *= float(self.mel_band_norm[band])
            mel_frame[band] = np.float32(10.0 * math.log10(max(energy, 1.0e-10)))
        return mel_frame

    def extract_from_array(self, waveform: np.ndarray) -> np.ndarray:
        clip_samples = RIVER_KWS_WINDOW_SAMPLES + (
            (RIVER_KWS_FEATURE_FRAMES - 1) * RIVER_KWS_HOP_SAMPLES
        )
        data = np.ascontiguousarray(waveform, dtype=np.int16)
        if data.shape[0] > clip_samples:
            data = data[:clip_samples]
        elif data.shape[0] < clip_samples:
            data = np.pad(data, (0, clip_samples - data.shape[0]))

        features = np.zeros((RIVER_KWS_FEATURE_FRAMES, RIVER_KWS_MEL_BINS), dtype=np.float32)
        for frame_index in range(RIVER_KWS_FEATURE_FRAMES):
            start = frame_index * RIVER_KWS_HOP_SAMPLES
            frame = data[start:start + RIVER_KWS_WINDOW_SAMPLES]
            power_bins = self._compute_power_bins(frame)
            features[frame_index, :] = self._compute_mel_frame(power_bins)

        max_db = float(features.max())
        normalized = np.zeros_like(features, dtype=np.float32)
        for frame_index in range(RIVER_KWS_FEATURE_FRAMES):
            for mel_index in range(RIVER_KWS_MEL_BINS):
                relative_db = float(features[frame_index, mel_index]) - max_db
                if relative_db < RIVER_KWS_FEATURE_DB_MIN:
                    relative_db = RIVER_KWS_FEATURE_DB_MIN
                if relative_db > 0.0:
                    relative_db = 0.0
                normalized[frame_index, mel_index] = np.float32(
                    (relative_db - RIVER_KWS_FEATURE_MEAN) / RIVER_KWS_FEATURE_STD
                )

        return normalized.T.copy()


def feature_stats(features: np.ndarray) -> FeatureStats:
    return FeatureStats(
        mean=float(np.mean(features)),
        std=float(np.std(features)),
        min=float(np.min(features)),
        max=float(np.max(features)),
    )


def load_pt_model(checkpoint_path: Path, base_channels: int) -> TorchExportBCResNet:
    model = TorchExportBCResNet(base_channels=base_channels)
    model.load_state_dict(remap_checkpoint_state_dict(checkpoint_path))
    model.eval()
    return model


def run_pt(model: TorchExportBCResNet, features: np.ndarray) -> ScoreStats:
    inp = torch.from_numpy(features.astype(np.float32, copy=False)).unsqueeze(0).unsqueeze(0)
    with torch.no_grad():
        score = float(model(inp).reshape(-1)[0].item())
    return ScoreStats(score=score, raw_output=None)


class TfliteRunner:
    def __init__(self, model_path: Path) -> None:
        self.interpreter = tf.lite.Interpreter(model_path=str(model_path))
        self.interpreter.allocate_tensors()
        self.input_details = self.interpreter.get_input_details()[0]
        self.output_details = self.interpreter.get_output_details()[0]

    def quantize_input(self, features: np.ndarray) -> np.ndarray:
        scale, zero_point = self.input_details["quantization"]
        if self.input_details["dtype"] == np.int8:
            quantized = round_to_i32(features / scale) + int(zero_point)
            return clamp_i8(quantized)
        if self.input_details["dtype"] == np.uint8:
            quantized = np.clip(round_to_i32(features / scale) + int(zero_point), 0, 255)
            return quantized.astype(np.uint8, copy=False)
        return features.astype(np.float32, copy=False)

    def make_input_tensor(self, features: np.ndarray) -> np.ndarray:
        shape = tuple(int(v) for v in self.input_details["shape"])
        if shape == (1, RIVER_KWS_MEL_BINS, RIVER_KWS_FEATURE_FRAMES, 1):
            base = features
        elif shape == (1, RIVER_KWS_FEATURE_FRAMES, RIVER_KWS_MEL_BINS, 1):
            base = features.T
        else:
            raise RuntimeError(f"Unsupported input shape: {shape}")

        quantized = self.quantize_input(base)
        return quantized[np.newaxis, :, :, np.newaxis]

    def run(self, features: np.ndarray) -> ScoreStats:
        input_tensor = self.make_input_tensor(features)
        self.interpreter.set_tensor(self.input_details["index"], input_tensor)
        self.interpreter.invoke()
        raw_output = self.interpreter.get_tensor(self.output_details["index"]).reshape(-1)[0]
        raw_scalar = int(raw_output) if np.issubdtype(self.output_details["dtype"], np.integer) else None

        if raw_scalar is None:
            score = float(raw_output)
        else:
            scale, zero_point = self.output_details["quantization"]
            score = float((raw_scalar - int(zero_point)) * scale)
        return ScoreStats(score=score, raw_output=raw_scalar)


def compare_sample(
    audio_path: Path,
    training_extractor: RiverKwsFeatureExtractor,
    board_extractor: BoardExactFeatureExtractor,
    pt_model: TorchExportBCResNet,
    tflite_runner: TfliteRunner,
) -> SampleReport:
    waveform = load_wav_mono_int16(audio_path)
    training_feature = training_extractor.extract_from_array(waveform).astype(np.float32, copy=False)
    board_feature = board_extractor.extract_from_array(waveform).astype(np.float32, copy=False)

    quant_training = tflite_runner.quantize_input(training_feature)
    quant_board = tflite_runner.quantize_input(board_feature)

    return SampleReport(
        audio_path=str(audio_path),
        training_feature=feature_stats(training_feature),
        board_feature=feature_stats(board_feature),
        feature_diff_mean_abs=float(np.mean(np.abs(training_feature - board_feature))),
        feature_diff_max_abs=float(np.max(np.abs(training_feature - board_feature))),
        quant_diff_count=int(np.count_nonzero(quant_training != quant_board)),
        quant_total=int(quant_training.size),
        pt_score_training=run_pt(pt_model, training_feature),
        pt_score_board=run_pt(pt_model, board_feature),
        tflite_score_training=tflite_runner.run(training_feature),
        tflite_score_board=tflite_runner.run(board_feature),
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare board-faithful frontend vs training frontend on real board recordings."
    )
    parser.add_argument("audio", nargs="+", help="WAV paths to compare")
    parser.add_argument("--checkpoint", default=str(DEFAULT_CHECKPOINT))
    parser.add_argument("--tflite", default=str(DEFAULT_TFLITE))
    parser.add_argument("--base-channels", type=int, default=24)
    parser.add_argument("--fft-lib", default=str(DEFAULT_FFT_LIB))
    parser.add_argument("--json-out", default=None)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    fft_lib_path = Path(args.fft_lib).resolve()
    if not fft_lib_path.exists():
        build_fft_library(fft_lib_path)

    training_extractor = RiverKwsFeatureExtractor(RiverKwsFrontendConfig())
    board_extractor = BoardExactFeatureExtractor(fft_lib_path)
    pt_model = load_pt_model(Path(args.checkpoint).resolve(), args.base_channels)
    tflite_runner = TfliteRunner(Path(args.tflite).resolve())

    reports = [
        compare_sample(
            Path(audio_path).resolve(),
            training_extractor,
            board_extractor,
            pt_model,
            tflite_runner,
        )
        for audio_path in args.audio
    ]

    result: dict[str, Any] = {
        "checkpoint": str(Path(args.checkpoint).resolve()),
        "tflite": str(Path(args.tflite).resolve()),
        "reports": [asdict(report) for report in reports],
    }

    print(json.dumps(result, indent=2, ensure_ascii=False))
    if args.json_out:
        Path(args.json_out).resolve().write_text(
            json.dumps(result, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
