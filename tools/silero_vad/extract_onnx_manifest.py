#!/usr/bin/env python3
import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

import onnx
from onnx import TensorProto


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tensor_type_name(data_type: int) -> str:
    return TensorProto.DataType.Name(data_type)


def dims_to_list(dims) -> list:
    values = []
    for dim in dims:
        if dim.HasField("dim_value"):
            values.append(dim.dim_value)
        elif dim.HasField("dim_param"):
            values.append(dim.dim_param)
        else:
            values.append(None)
    return values


def value_info_to_dict(value_info) -> dict:
    tensor_type = value_info.type.tensor_type
    return {
        "name": value_info.name,
        "shape": dims_to_list(tensor_type.shape.dim),
        "dtype": tensor_type_name(tensor_type.elem_type),
    }


def initializer_to_dict(initializer) -> dict:
    return {
        "name": initializer.name,
        "shape": list(initializer.dims),
        "dtype": tensor_type_name(initializer.data_type),
    }


def summarize_graph(graph, path: str) -> dict:
    op_counts = Counter(node.op_type for node in graph.node)
    summary = {
        "path": path,
        "inputs": [value_info_to_dict(item) for item in graph.input],
        "outputs": [value_info_to_dict(item) for item in graph.output],
        "node_count": len(graph.node),
        "op_counts": dict(sorted(op_counts.items())),
        "initializer_count": len(graph.initializer),
        "initializers": [initializer_to_dict(item) for item in graph.initializer],
    }

    children = []
    for node in graph.node:
        for attribute in node.attribute:
            if attribute.type == onnx.AttributeProto.GRAPH:
                child_path = f"{path}/{node.name or node.op_type}:{attribute.name}"
                children.append(summarize_graph(attribute.g, child_path))
            elif attribute.type == onnx.AttributeProto.GRAPHS:
                for index, child_graph in enumerate(attribute.graphs):
                    child_path = (
                        f"{path}/{node.name or node.op_type}:{attribute.name}[{index}]"
                    )
                    children.append(summarize_graph(child_graph, child_path))
    if children:
        summary["subgraphs"] = children
    return summary


def build_manifest(model_path: Path) -> dict:
    model = onnx.load(str(model_path))
    return {
        "source_file": str(model_path),
        "sha256": sha256_file(model_path),
        "ir_version": model.ir_version,
        "producer_name": model.producer_name,
        "producer_version": model.producer_version,
        "model_version": model.model_version,
        "opset_imports": [
            {"domain": item.domain, "version": item.version}
            for item in model.opset_import
        ],
        "top_level_inputs": [value_info_to_dict(item) for item in model.graph.input],
        "top_level_outputs": [value_info_to_dict(item) for item in model.graph.output],
        "top_level_initializers": [
            initializer_to_dict(item) for item in model.graph.initializer
        ],
        "graph": summarize_graph(model.graph, "main"),
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract a reproducible structural manifest from an ONNX model."
    )
    parser.add_argument("--input", required=True, help="Path to the input ONNX model")
    parser.add_argument(
        "--output",
        required=True,
        help="Path to the output JSON manifest",
    )
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    manifest = build_manifest(input_path)
    output_path.write_text(json.dumps(manifest, indent=2, sort_keys=False) + "\n")


if __name__ == "__main__":
    main()
