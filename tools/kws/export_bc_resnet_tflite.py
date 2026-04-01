#!/usr/bin/env python3
import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np
import tensorflow as tf
import torch


TRAINING_ROOT = Path("/root/kws-training-pro")
DEFAULT_MANIFESTS = (
    Path("/root/kws-dataset-pro-blueprint/data/train_manifest.jsonl"),
    Path("/root/kws-dataset-pro-blueprint/data/iteration4_supplement.jsonl"),
    Path("/root/kws-dataset-pro-blueprint/data/llm_supplement.jsonl"),
)

sys.path.insert(0, str(TRAINING_ROOT))

from model_bc_resnet import BCResNet  # type: ignore  # noqa: E402
from river_kws_features import (  # type: ignore  # noqa: E402
    RiverKwsFeatureExtractor,
    RiverKwsFrontendConfig,
    load_wav_mono_int16,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export BC-ResNet checkpoint to TFLite with board-aligned calibration."
    )
    parser.add_argument("--checkpoint", required=True, help="Path to BC-ResNet .pth checkpoint")
    parser.add_argument("--output", required=True, help="Path to output .tflite")
    parser.add_argument(
        "--quantization",
        choices=("float32", "int8"),
        default="float32",
        help="Export mode",
    )
    parser.add_argument("--base-channels", type=int, default=24)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument(
        "--rep-samples",
        type=int,
        default=256,
        help="Representative samples for int8 calibration",
    )
    parser.add_argument(
        "--manifest",
        action="append",
        default=[],
        help="JSONL manifest with audio_path entries; may be repeated",
    )
    return parser.parse_args()


def create_keras_bc_resnet(base_channels: int = 24,
                           num_classes: int = 1,
                           input_shape: tuple[int, int, int] = (40, 98, 1)
                           ) -> tf.keras.Model:
    inputs = tf.keras.Input(shape=input_shape)
    channels = base_channels

    x = tf.keras.layers.ZeroPadding2D(padding=((2, 2), (2, 2)))(inputs)
    x = tf.keras.layers.Conv2D(
        channels, (5, 5), strides=(2, 2), padding="valid", use_bias=False
    )(x)
    x = tf.keras.layers.BatchNormalization(epsilon=1e-5)(x)
    x = tf.keras.layers.ReLU()(x)

    def keras_bc_block(x_in: tf.Tensor,
                       in_channels: int,
                       out_channels: int,
                       f_kernel: int = 3,
                       t_kernel: int = 3,
                       expansion: float = 1.5) -> tf.Tensor:
        mid_channels = int(in_channels * expansion)

        f_out = tf.keras.layers.ZeroPadding2D(
            padding=((f_kernel // 2, f_kernel // 2), (0, 0))
        )(x_in)
        f_out = tf.keras.layers.DepthwiseConv2D(
            (f_kernel, 1), strides=(1, 1), padding="valid", use_bias=False
        )(f_out)
        f_out = tf.keras.layers.BatchNormalization(epsilon=1e-5)(f_out)
        f_avg = tf.keras.layers.AveragePooling2D(pool_size=(1, 49))(f_out)
        f_out = tf.keras.layers.Add()([f_out, f_avg])
        f_out = tf.keras.layers.ReLU()(f_out)

        x_mid = tf.keras.layers.Conv2D(
            mid_channels, (1, 1), strides=(1, 1), padding="valid", use_bias=False
        )(f_out)
        x_mid = tf.keras.layers.BatchNormalization(epsilon=1e-5)(x_mid)
        x_mid = tf.keras.layers.ReLU()(x_mid)

        t_out = tf.keras.layers.ZeroPadding2D(
            padding=((0, 0), (t_kernel // 2, t_kernel // 2))
        )(x_mid)
        t_out = tf.keras.layers.DepthwiseConv2D(
            (1, t_kernel), strides=(1, 1), padding="valid", use_bias=False
        )(t_out)
        t_out = tf.keras.layers.BatchNormalization(epsilon=1e-5)(t_out)
        t_avg = tf.keras.layers.AveragePooling2D(pool_size=(20, 1))(t_out)
        t_out = tf.keras.layers.Add()([t_out, t_avg])
        t_out = tf.keras.layers.ReLU()(t_out)

        out = tf.keras.layers.Conv2D(
            out_channels, (1, 1), strides=(1, 1), padding="valid", use_bias=False
        )(t_out)
        out = tf.keras.layers.BatchNormalization(epsilon=1e-5)(out)
        if in_channels == out_channels:
            out = tf.keras.layers.Add()([out, x_in])
        return tf.keras.layers.ReLU()(out)

    x = keras_bc_block(x, channels, channels)
    x = keras_bc_block(x, channels, int(channels * 1.5))
    x = keras_bc_block(x, int(channels * 1.5), int(channels * 1.5))
    x = keras_bc_block(x, int(channels * 1.5), int(channels * 2))
    x = tf.keras.layers.AveragePooling2D(pool_size=(20, 49))(x)
    outputs = tf.keras.layers.Conv2D(num_classes, (1, 1), activation="sigmoid")(x)
    return tf.keras.Model(inputs=inputs, outputs=outputs)


def copy_weights_bc_resnet(pt_model: BCResNet, keras_model: tf.keras.Model) -> None:
    pt_dict = pt_model.state_dict()
    conv_layers = [layer for layer in keras_model.layers if isinstance(layer, tf.keras.layers.Conv2D)]
    dw_layers = [layer for layer in keras_model.layers if isinstance(layer, tf.keras.layers.DepthwiseConv2D)]
    bn_layers = [layer for layer in keras_model.layers if isinstance(layer, tf.keras.layers.BatchNormalization)]

    conv_layers[0].set_weights([pt_dict["conv1.0.weight"].permute(2, 3, 1, 0).numpy()])
    bn_layers[0].set_weights(
        [
            pt_dict["conv1.1.weight"].numpy(),
            pt_dict["conv1.1.bias"].numpy(),
            pt_dict["conv1.1.running_mean"].numpy(),
            pt_dict["conv1.1.running_var"].numpy(),
        ]
    )

    for index in range(4):
        prefix = f"blocks.{index}"
        dw_layers[index * 2].set_weights(
            [pt_dict[f"{prefix}.f_dw.weight"].permute(2, 3, 0, 1).numpy()]
        )
        bn_layers[1 + index * 4].set_weights(
            [
                pt_dict[f"{prefix}.f_bn.weight"].numpy(),
                pt_dict[f"{prefix}.f_bn.bias"].numpy(),
                pt_dict[f"{prefix}.f_bn.running_mean"].numpy(),
                pt_dict[f"{prefix}.f_bn.running_var"].numpy(),
            ]
        )

        conv_layers[1 + index * 2].set_weights(
            [pt_dict[f"{prefix}.pw1.weight"].permute(2, 3, 1, 0).numpy()]
        )
        bn_layers[2 + index * 4].set_weights(
            [
                pt_dict[f"{prefix}.pw1_bn.weight"].numpy(),
                pt_dict[f"{prefix}.pw1_bn.bias"].numpy(),
                pt_dict[f"{prefix}.pw1_bn.running_mean"].numpy(),
                pt_dict[f"{prefix}.pw1_bn.running_var"].numpy(),
            ]
        )

        dw_layers[1 + index * 2].set_weights(
            [pt_dict[f"{prefix}.t_dw.weight"].permute(2, 3, 0, 1).numpy()]
        )
        bn_layers[3 + index * 4].set_weights(
            [
                pt_dict[f"{prefix}.t_bn.weight"].numpy(),
                pt_dict[f"{prefix}.t_bn.bias"].numpy(),
                pt_dict[f"{prefix}.t_bn.running_mean"].numpy(),
                pt_dict[f"{prefix}.t_bn.running_var"].numpy(),
            ]
        )

        conv_layers[2 + index * 2].set_weights(
            [pt_dict[f"{prefix}.pw2.weight"].permute(2, 3, 1, 0).numpy()]
        )
        bn_layers[4 + index * 4].set_weights(
            [
                pt_dict[f"{prefix}.pw2_bn.weight"].numpy(),
                pt_dict[f"{prefix}.pw2_bn.bias"].numpy(),
                pt_dict[f"{prefix}.pw2_bn.running_mean"].numpy(),
                pt_dict[f"{prefix}.pw2_bn.running_var"].numpy(),
            ]
        )

    conv_layers[-1].set_weights(
        [
            pt_dict["final_conv.weight"].permute(2, 3, 1, 0).numpy(),
            pt_dict["final_conv.bias"].numpy(),
        ]
    )


def collect_representative_paths(manifest_paths: list[Path],
                                 sample_count: int,
                                 seed: int) -> list[Path]:
    positives: list[Path] = []
    negatives: list[Path] = []

    for manifest_path in manifest_paths:
        with manifest_path.open("r", encoding="utf-8") as handle:
            for raw_line in handle:
                line = raw_line.strip()
                if not line:
                    continue
                item = json.loads(line)
                audio_path = Path(item["audio_path"])
                if not audio_path.exists():
                    continue
                if item.get("label") == "positive":
                    positives.append(audio_path)
                else:
                    negatives.append(audio_path)

    rng = random.Random(seed)
    rng.shuffle(positives)
    rng.shuffle(negatives)

    half = max(1, sample_count // 2)
    chosen = positives[:half] + negatives[:half]
    if len(chosen) < sample_count:
        combined = positives[half:] + negatives[half:]
        rng.shuffle(combined)
        chosen.extend(combined[: sample_count - len(chosen)])
    rng.shuffle(chosen)
    if not chosen:
        raise RuntimeError("No representative audio paths found for int8 calibration")
    return chosen[:sample_count]


def make_representative_dataset(paths: list[Path]):
    extractor = RiverKwsFeatureExtractor(RiverKwsFrontendConfig())

    def representative_dataset():
        for audio_path in paths:
            waveform = load_wav_mono_int16(audio_path)
            features = extractor.extract_from_array(waveform).astype(np.float32, copy=False)
            yield [features[np.newaxis, :, :, np.newaxis]]

    return representative_dataset


def inspect_tflite_model(model_path: Path) -> None:
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    print(f"input_dtype={input_details['dtype'].__name__} input_shape={list(input_details['shape'])}")
    print(f"output_dtype={output_details['dtype'].__name__} output_shape={list(output_details['shape'])}")
    input_quant = input_details.get("quantization", (0.0, 0))
    output_quant = output_details.get("quantization", (0.0, 0))
    print(f"input_quant={input_quant} output_quant={output_quant}")


def main() -> None:
    args = parse_args()
    checkpoint_path = Path(args.checkpoint).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    pt_model = BCResNet(base_channels=args.base_channels)
    pt_model.load_state_dict(torch.load(checkpoint_path, map_location="cpu"))
    pt_model.eval()

    keras_model = create_keras_bc_resnet(base_channels=args.base_channels)
    copy_weights_bc_resnet(pt_model, keras_model)

    converter = tf.lite.TFLiteConverter.from_keras_model(keras_model)
    if args.quantization == "int8":
        manifest_paths = [Path(path).resolve() for path in args.manifest]
        if not manifest_paths:
            manifest_paths = [path.resolve() for path in DEFAULT_MANIFESTS]
        rep_paths = collect_representative_paths(manifest_paths, args.rep_samples, args.seed)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = make_representative_dataset(rep_paths)
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
        print(f"representative_samples={len(rep_paths)}")
    else:
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]

    tflite_model = converter.convert()
    output_path.write_bytes(tflite_model)
    print(f"wrote={output_path} bytes={len(tflite_model)} mode={args.quantization}")
    inspect_tflite_model(output_path)


if __name__ == "__main__":
    main()
