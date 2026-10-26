#!/usr/bin/env python3
"""Generate the deterministic canonical v2 scenario-frequency catalog."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from generator_v2 import (
    GENERATOR_NAME,
    GENERATOR_VERSION,
    MAP_SEMANTIC_VERSION,
    generate_map,
    write_map,
)

ROOT = Path(__file__).resolve().parents[2]

CATALOG = {
    "RURAL_MACROCELL": (0.7, 0.8, 3.5),
    "URBAN_MACROCELL": (3.5, 4.9, 26.0, 28.0),
    "URBAN_MICROCELL": (2.38, 3.5, 4.9, 26.0, 28.0),
    "INDOOR_OPEN_OFFICE": (3.5, 26.0, 28.0),
    "INDOOR_MIXED_OFFICE": (3.5, 26.0, 28.0),
    "INDOOR_SHOPPING_MALL": (3.5, 26.0, 60.0),
}


def derived_seed(master_seed: int, scenario: str, frequency_ghz: float) -> int:
    key = f"{master_seed}:{scenario}:{frequency_ghz:g}".encode()
    return int.from_bytes(hashlib.sha256(key).digest()[:8], "big")


def frequency_text(value: float) -> str:
    return f"{value:g}"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--master-seed", type=int, default=20260927)
    parser.add_argument("--cell-number", type=int, default=291)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "results" / "maps-v2-candidates",
    )
    args = parser.parse_args()

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for scenario in sorted(CATALOG):
        for frequency_ghz in CATALOG[scenario]:
            seed = derived_seed(
                args.master_seed, scenario, frequency_ghz)
            payload = generate_map(
                scenario,
                frequency_ghz,
                seed,
                args.cell_number,
            )
            filename = (
                f"macroscopic_fading_map_{scenario}_"
                f"{frequency_text(frequency_ghz)}.json"
            )
            path = output / filename
            write_map(payload, path)
            entries.append(
                {
                    "file": filename,
                    "scenario": scenario,
                    "frequency_ghz": frequency_ghz,
                    "seed": seed,
                    "realization_id": payload["metadata"]["realization_id"],
                    "sha256": file_sha256(path),
                }
            )
            print(path)

    manifest = {
        "schema_version": 2,
        "semantic_version": MAP_SEMANTIC_VERSION,
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
        },
        "master_seed": args.master_seed,
        "cell_number": args.cell_number,
        "maps": entries,
    }
    (output / "CATALOG.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
