#!/usr/bin/env python3
import argparse
from pathlib import Path


def format_bytes(data: bytes, width: int = 12) -> str:
    rows = []
    for index in range(0, len(data), width):
        chunk = data[index:index + width]
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(rows)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Embed a TFLite model into the legacy river KWS header format."
    )
    parser.add_argument("--input", required=True, help="Path to .tflite model")
    parser.add_argument("--header", required=True, help="Path to generated header")
    parser.add_argument("--symbol", default="kws_model", help="C array symbol name")
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    header_path = Path(args.header).resolve()
    payload = input_path.read_bytes()

    header = (
        "#pragma once\n\n"
        "// Generated TFLite model header\n"
        f"// Model: {input_path.name}\n\n"
        f"static const unsigned char {args.symbol}[] = {{\n"
        f"{format_bytes(payload)}\n"
        "};\n"
        f"static const unsigned int {args.symbol}_len = {len(payload)};\n"
    )
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
