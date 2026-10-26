#!/usr/bin/env python3
"""Hash and characterize the embedded legacy BLER lookup table."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TABLE_PATH = ROOT / "include" / "mac_layer" / "mac_definitions.h"
EXPECTED_VALUES = 2 * 5 * 4 * 28 * 60
NUMBER = re.compile(
    r"(?<![A-Za-z_])[-+]?(?:\d+\.\d*|\.\d+|\d+)"
    r"(?:[eE][-+]?\d+)?"
)


def initializer(text: str) -> str:
    marker = "LEGACY_BLER_MCS_SINR"
    declaration = text.index(marker)
    start = text.index("{", declaration)
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise ValueError("legacy BLER table initializer is not balanced")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-sha256")
    args = parser.parse_args()

    payload = initializer(TABLE_PATH.read_text())
    values = [float(match.group()) for match in NUMBER.finditer(payload)]
    if len(values) != EXPECTED_VALUES:
        raise SystemExit(
            f"expected {EXPECTED_VALUES} table values, found {len(values)}"
        )

    packed = b"".join(struct.pack("<d", value) for value in values)
    digest = hashlib.sha256(packed).hexdigest()
    if args.expect_sha256 is not None and digest != args.expect_sha256:
        raise SystemExit(
            f"legacy BLER hash mismatch: expected {args.expect_sha256}, "
            f"observed {digest}"
        )

    curves = [
        tuple(values[offset : offset + 60])
        for offset in range(0, len(values), 60)
    ]
    duplicate_curves = len(curves) - len(set(curves))
    increasing_steps = sum(
        current > previous
        for curve in curves
        for previous, current in zip(curve, curve[1:])
    )
    out_of_range_values = sum(
        value < 0.0 or value > 1.0 for value in values
    )
    report = {
        "schema_version": 1,
        "source": str(TABLE_PATH.relative_to(ROOT)),
        "shape": [2, 5, 4, 28, 60],
        "value_count": len(values),
        "packed_float64_little_endian_sha256": digest,
        "duplicate_curves": duplicate_curves,
        "increasing_sinr_steps": increasing_steps,
        "out_of_range_values": out_of_range_values,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
