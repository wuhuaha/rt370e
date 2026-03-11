#!/usr/bin/env python3
import argparse
from pathlib import Path


def format_bytes(data: bytes, width: int = 12) -> str:
    rows = []
    for index in range(0, len(data), width):
        chunk = data[index : index + width]
        rows.append("    " + ",".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(rows)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a C++ source/header pair that embeds a TFLite model."
    )
    parser.add_argument("--input", required=True, help="Path to the input .tflite file")
    parser.add_argument("--header", required=True, help="Path to the generated header")
    parser.add_argument("--source", required=True, help="Path to the generated source")
    parser.add_argument(
        "--symbol",
        required=True,
        help="Base symbol name, for example river_silero_vad_model_data",
    )
    args = parser.parse_args()

    input_path = Path(args.input).resolve()
    header_path = Path(args.header).resolve()
    source_path = Path(args.source).resolve()
    symbol = args.symbol
    data = input_path.read_bytes()

    header_guard = f"{symbol.upper()}_H_"
    header = f"""#ifndef {header_guard}
#define {header_guard}

#ifdef __cplusplus
extern "C" {{
#endif

extern const unsigned int g_{symbol}_size;
extern const unsigned char g_{symbol}[];

#ifdef __cplusplus
}}
#endif

#endif
"""
    source = f"""#include <cstdint>

#include "{header_path.name}"

const unsigned int g_{symbol}_size = {len(data)};
alignas(16) const unsigned char g_{symbol}[] = {{
{format_bytes(data)}
}};
"""

    header_path.parent.mkdir(parents=True, exist_ok=True)
    source_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(header)
    source_path.write_text(source)


if __name__ == "__main__":
    main()
