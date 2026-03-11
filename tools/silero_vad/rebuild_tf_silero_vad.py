#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import tensorflow as tf

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import extract_reconstruction_tensors  # noqa: E402


class SileroVadModule(tf.Module):
    def __init__(self, tensors: dict[str, np.ndarray]):
        super().__init__()
        self.cutoff = 129
        self.right_pad = int(np.asarray(tensors["self.model.stft.hop_length"]).item() // 2)

        self.stft_weight = tf.constant(
            np.transpose(tensors["model.stft.forward_basis_buffer"], (2, 1, 0)),
            dtype=tf.float32,
        )

        self.conv1_weight = tf.constant(
            np.transpose(tensors["model.encoder.0.reparam_conv.weight"], (2, 1, 0)),
            dtype=tf.float32,
        )
        self.conv1_bias = tf.constant(
            tensors["model.encoder.0.reparam_conv.bias"], dtype=tf.float32
        )
        self.conv2_weight = tf.constant(
            np.transpose(tensors["model.encoder.1.reparam_conv.weight"], (2, 1, 0)),
            dtype=tf.float32,
        )
        self.conv2_bias = tf.constant(
            tensors["model.encoder.1.reparam_conv.bias"], dtype=tf.float32
        )
        self.conv3_weight = tf.constant(
            np.transpose(tensors["model.encoder.2.reparam_conv.weight"], (2, 1, 0)),
            dtype=tf.float32,
        )
        self.conv3_bias = tf.constant(
            tensors["model.encoder.2.reparam_conv.bias"], dtype=tf.float32
        )
        self.conv4_weight = tf.constant(
            np.transpose(tensors["model.encoder.3.reparam_conv.weight"], (2, 1, 0)),
            dtype=tf.float32,
        )
        self.conv4_bias = tf.constant(
            tensors["model.encoder.3.reparam_conv.bias"], dtype=tf.float32
        )
        self.final_conv_weight = tf.constant(
            np.transpose(tensors["model.decoder.decoder.2.weight"], (2, 1, 0)),
            dtype=tf.float32,
        )
        self.final_conv_bias = tf.constant(
            tensors["model.decoder.decoder.2.bias"], dtype=tf.float32
        )

        self.lstm_w = tf.constant(tensors["decoder.lstm.W"], dtype=tf.float32)
        self.lstm_r = tf.constant(tensors["decoder.lstm.R"], dtype=tf.float32)
        self.lstm_b = tf.constant(tensors["decoder.lstm.B"], dtype=tf.float32)

    def _conv1d(self, x: tf.Tensor, weight: tf.Tensor, bias: tf.Tensor, stride: int) -> tf.Tensor:
        x = tf.pad(x, paddings=[[0, 0], [1, 1], [0, 0]])
        y = tf.nn.conv1d(x, filters=weight, stride=stride, padding="VALID")
        y = tf.nn.bias_add(y, bias)
        return tf.nn.relu(y)

    def _lstm_step(self, x: tf.Tensor, state: tf.Tensor) -> tuple[tf.Tensor, tf.Tensor]:
        h = state[0]
        c = state[1]
        wb = self.lstm_b[:512]
        rb = self.lstm_b[512:]
        gates = (
            tf.matmul(x, self.lstm_w, transpose_b=True)
            + tf.matmul(h, self.lstm_r, transpose_b=True)
            + wb
            + rb
        )
        i, o, f, g = tf.split(gates, num_or_size_splits=4, axis=-1)
        i = tf.math.sigmoid(i)
        o = tf.math.sigmoid(o)
        f = tf.math.sigmoid(f)
        g = tf.math.tanh(g)
        c_new = f * c + i * g
        h_new = o * tf.math.tanh(c_new)
        return h_new, tf.stack([h_new, c_new], axis=0)

    @tf.function(
        input_signature=[
            tf.TensorSpec(shape=[1, 576], dtype=tf.float32, name="input"),
            tf.TensorSpec(shape=[2, 1, 128], dtype=tf.float32, name="state"),
        ]
    )
    def __call__(self, input, state):
        x = tf.pad(
            tf.expand_dims(input, axis=-1),
            paddings=[[0, 0], [0, self.right_pad], [0, 0]],
            mode="REFLECT",
        )
        x = tf.nn.conv1d(x, filters=self.stft_weight, stride=128, padding="VALID")
        real = x[:, :, : self.cutoff]
        imag = x[:, :, self.cutoff :]
        x = tf.math.sqrt(tf.math.square(real) + tf.math.square(imag))

        x = self._conv1d(x, self.conv1_weight, self.conv1_bias, stride=1)
        x = self._conv1d(x, self.conv2_weight, self.conv2_bias, stride=2)
        x = self._conv1d(x, self.conv3_weight, self.conv3_bias, stride=2)
        x = self._conv1d(x, self.conv4_weight, self.conv4_bias, stride=1)

        x = tf.squeeze(x, axis=1)
        h_new, state_new = self._lstm_step(x, state)

        y = tf.expand_dims(tf.nn.relu(h_new), axis=1)
        y = tf.nn.conv1d(y, filters=self.final_conv_weight, stride=1, padding="SAME")
        y = tf.nn.bias_add(y, self.final_conv_bias)
        y = tf.math.sigmoid(y)
        y = tf.reduce_mean(y, axis=1)
        return {"output": y, "stateN": state_new}


def verify_against_onnx(
    module: SileroVadModule,
    onnx_path: Path,
    num_cases: int,
    seed: int,
) -> dict:
    rng = np.random.default_rng(seed)
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])

    output_max = 0.0
    state_max = 0.0
    for _ in range(num_cases):
        audio = rng.uniform(-1.0, 1.0, size=(1, 576)).astype(np.float32)
        state = rng.normal(0.0, 0.2, size=(2, 1, 128)).astype(np.float32)

        onnx_out = session.run(
            ["output", "stateN"],
            {"input": audio, "state": state, "sr": np.array(16000, dtype=np.int64)},
        )
        tf_out = module(
            tf.constant(audio, dtype=tf.float32),
            tf.constant(state, dtype=tf.float32),
        )

        output_diff = np.max(np.abs(onnx_out[0] - tf_out["output"].numpy()))
        state_diff = np.max(np.abs(onnx_out[1] - tf_out["stateN"].numpy()))
        output_max = max(output_max, float(output_diff))
        state_max = max(state_max, float(state_diff))

    return {
        "num_cases": num_cases,
        "seed": seed,
        "max_abs_diff_output": output_max,
        "max_abs_diff_state": state_max,
    }


def export_tflite(module: SileroVadModule, output_path: Path) -> None:
    concrete = module.__call__.get_concrete_function()
    converter = tf.lite.TFLiteConverter.from_concrete_functions([concrete], module)
    tflite_model = converter.convert()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(tflite_model)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Rebuild the official Silero VAD 16k ONNX model in TensorFlow from the "
            "pinned upstream weights and optionally export a batch=1 TFLite artifact."
        )
    )
    parser.add_argument("--input", required=True, help="Path to the official ONNX model")
    parser.add_argument(
        "--verification-output",
        help="Optional path to write a JSON parity report against ONNXRuntime",
    )
    parser.add_argument(
        "--verify-cases",
        type=int,
        default=4,
        help="How many random batch=1 cases to compare against ONNXRuntime",
    )
    parser.add_argument(
        "--seed", type=int, default=8730, help="Seed for random verification inputs"
    )
    parser.add_argument(
        "--tflite-output",
        help="Optional path to write an exported batch=1 TFLite model",
    )
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    _, tensors = extract_reconstruction_tensors.build_manifest(input_path)
    module = SileroVadModule(tensors)

    if args.verification_output:
        report = verify_against_onnx(
            module=module,
            onnx_path=input_path,
            num_cases=args.verify_cases,
            seed=args.seed,
        )
        output_path = Path(args.verification_output).resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2) + "\n")

    if args.tflite_output:
        export_tflite(module, Path(args.tflite_output).resolve())


if __name__ == "__main__":
    main()
