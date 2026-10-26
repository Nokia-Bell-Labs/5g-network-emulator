#!/usr/bin/env python3
"""Validate shipped map structure, checksums, and manifest consistency."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from build_manifest import build_manifest

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MAP_DIR = ROOT / "include" / "maps_scenarios"
DEFAULT_MANIFEST = DEFAULT_MAP_DIR / "MANIFEST.json"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map-dir", type=Path, default=DEFAULT_MAP_DIR)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()

    expected = build_manifest(args.map_dir.resolve())
    actual = json.loads(args.manifest.read_text())
    if actual != expected:
        expected_files = {
            entry["file"]: entry["sha256"] for entry in expected["maps"]
        }
        actual_files = {
            entry["file"]: entry["sha256"] for entry in actual.get("maps", [])
        }
        missing = sorted(set(expected_files) - set(actual_files))
        unexpected = sorted(set(actual_files) - set(expected_files))
        changed = sorted(
            name
            for name in set(expected_files) & set(actual_files)
            if expected_files[name] != actual_files[name]
        )
        raise SystemExit(
            "map manifest mismatch: "
            f"missing={missing}, unexpected={unexpected}, changed={changed}")

    print(
        f"validated {expected['map_count']} maps in "
        f"{args.map_dir.resolve()}")


if __name__ == "__main__":
    main()
