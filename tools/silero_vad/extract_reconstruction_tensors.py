#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper


def sha256_bytes(buffer: bytes) -> str:
    return hashlib.sha256(buffer).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tensor_summary(name: str, array: np.ndarray, kind: str) -> dict:
    array = np.asarray(array)
    return {
        "name": name,
        "kind": kind,
        "shape": list(array.shape),
        "dtype": str(array.dtype),
        "size": int(array.size),
        "nbytes": int(array.nbytes),
        "sha256": sha256_bytes(array.tobytes()),
        "min": float(array.min()) if array.size else None,
        "max": float(array.max()) if array.size else None,
    }


def constant_map(graph) -> dict[str, np.ndarray]:
    values = {}
    for node in graph.node:
        if node.op_type != "Constant":
            continue
        for attribute in node.attribute:
            if attribute.name == "value":
                values[node.output[0]] = numpy_helper.to_array(attribute.t)
                break
    return values


def find_node_by_name(nodes, name: str):
    for node in nodes:
        if node.name == name:
            return node
    raise KeyError(f"node not found: {name}")


def find_graph(model, if_node_name: str, branch_name: str):
    for node in model.graph.node:
        if node.name != if_node_name:
            continue
        for attribute in node.attribute:
            if attribute.name == branch_name:
                return attribute.g
    raise KeyError(f"graph not found: {if_node_name}:{branch_name}")


def apply_slice(array: np.ndarray, start: int, end: int, axis: int) -> np.ndarray:
    index = [slice(None)] * array.ndim
    index[axis] = slice(start, end)
    return array[tuple(index)]


def build_decoder_lstm_tensors(
    branch_graph, shared_initializers: dict[str, np.ndarray]
) -> dict[str, np.ndarray]:
    initializers = dict(shared_initializers)
    initializers.update(
        {item.name: numpy_helper.to_array(item) for item in branch_graph.initializer}
    )
    constants = constant_map(branch_graph)

    outputs = {}
    for node in branch_graph.node:
        if node.op_type == "Slice":
            source = initializers[node.input[0]]
            start = int(np.asarray(constants[node.input[1]]).item())
            end = int(np.asarray(constants[node.input[2]]).item())
            axis = int(np.asarray(constants[node.input[3]]).item())
            outputs[node.output[0]] = apply_slice(source, start, end, axis)
        elif node.op_type == "Concat":
            axis = next(attribute.i for attribute in node.attribute if attribute.name == "axis")
            outputs[node.output[0]] = np.concatenate(
                [outputs[input_name] for input_name in node.input], axis=axis
            )

    return {
        "decoder.lstm.W": outputs["/model/decoder/rnn/Concat_output_0"],
        "decoder.lstm.R": outputs["/model/decoder/rnn/Concat_1_output_0"],
        "decoder.lstm.B": outputs["/model/decoder/rnn/Concat_4_output_0"],
    }


def build_manifest(model_path: Path) -> tuple[dict, dict[str, np.ndarray]]:
    model = onnx.load(str(model_path))
    tensors = {}

    for initializer in model.graph.initializer:
        array = numpy_helper.to_array(initializer)
        tensors[initializer.name] = array

    top_level_constants = constant_map(model.graph)
    if "self.model.stft.hop_length" in top_level_constants:
        tensors["self.model.stft.hop_length"] = top_level_constants[
            "self.model.stft.hop_length"
        ]

    decoder_then = find_graph(model, "/model/decoder/If_1", "then_branch")
    tensors.update(build_decoder_lstm_tensors(decoder_then, tensors))

    summary = {
        "source_file": str(model_path),
        "source_sha256": sha256_file(model_path),
        "decoder_reconstruction_branch": "/model/decoder/If_1:then_branch",
        "tensors": [
            tensor_summary(name, array, "derived" if name.startswith("decoder.lstm.") else "source")
            for name, array in sorted(tensors.items())
        ],
    }
    return summary, tensors


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Extract reconstruction-oriented tensor metadata from the pinned Silero ONNX "
            "model, including the decoder LSTM tensors after the ONNX slice/concat layout."
        )
    )
    parser.add_argument("--input", required=True, help="Path to the input ONNX model")
    parser.add_argument(
        "--output",
        required=True,
        help="Path to the output JSON manifest",
    )
    parser.add_argument(
        "--npz",
        help="Optional path to a compressed NPZ bundle containing the extracted tensors",
    )
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    manifest, tensors = build_manifest(input_path)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n")

    if args.npz:
        npz_path = Path(args.npz).resolve()
        npz_path.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(npz_path, **tensors)


if __name__ == "__main__":
    main()
