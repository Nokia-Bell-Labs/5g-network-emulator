#!/usr/bin/env python3
"""Build a deterministic provenance manifest for shipped FikoRE maps."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MAP_DIR = ROOT / "include" / "maps_scenarios"
DEFAULT_OUTPUT = DEFAULT_MAP_DIR / "MANIFEST.json"
MAP_NAME = re.compile(
    r"macroscopic_fading_map_(?P<scenario>[A-Z_]+)_(?P<frequency>[0-9.]+)\.json"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def summarize_map(path: Path) -> dict:
    match = MAP_NAME.fullmatch(path.name)
    if match is None:
        raise ValueError(f"unsupported map filename: {path.name}")
    payload = json.loads(path.read_text())
    cell_number = int(payload["cell_number"])
    cell_size = float(payload["cell_size"])
    values = payload["map"]
    if len(values) != cell_number:
        raise ValueError(f"{path.name}: row count does not match cell_number")

    minimum = math.inf
    maximum = -math.inf
    total = 0.0
    samples = 0
    for row in values:
        if len(row) != cell_number:
            raise ValueError(f"{path.name}: map is not square")
        for raw_value in row:
            value = float(raw_value)
            if not math.isfinite(value):
                raise ValueError(f"{path.name}: map contains a non-finite value")
            minimum = min(minimum, value)
            maximum = max(maximum, value)
            total += value
            samples += 1

    summary = {
        "file": path.name,
        "sha256": sha256(path),
        "scenario": match.group("scenario"),
        "frequency_ghz": float(match.group("frequency")),
        "cell_number": cell_number,
        "cell_size_m": cell_size,
        "samples": samples,
        "minimum_db": minimum,
        "mean_db": total / samples,
        "maximum_db": maximum,
    }
    schema_version = int(payload.get("schema_version", 1))
    if schema_version == 1:
        summary.update(
            {
                "schema": "legacy-v1",
                "schema_version": 1,
                "generator": "legacy-matlab-unseeded",
                "seed": None,
                "provenance_status": "historical-realization",
            }
        )
        return summary

    if schema_version != 2:
        raise ValueError(
            f"{path.name}: unsupported schema version {schema_version}")
    if cell_number < 3 or cell_number % 2 == 0:
        raise ValueError(
            f"{path.name}: v2 cell_number must be odd and at least 3")
    metadata = payload.get("metadata")
    if not isinstance(metadata, dict):
        raise ValueError(f"{path.name}: v2 metadata is missing")
    required = {
        "scenario",
        "frequency_ghz",
        "seed",
        "realization_id",
        "generator",
        "semantic_version",
        "grid_origin",
        "pathloss_family",
        "los_state_model",
        "los_probability_model",
        "coefficient_validity_range",
    }
    missing = sorted(required - metadata.keys())
    if missing:
        raise ValueError(f"{path.name}: missing v2 metadata {missing}")
    if metadata["scenario"] != summary["scenario"]:
        raise ValueError(f"{path.name}: scenario metadata disagrees with filename")
    if not math.isclose(
        float(metadata["frequency_ghz"]),
        summary["frequency_ghz"],
        rel_tol=0.0,
        abs_tol=1e-12,
    ):
        raise ValueError(
            f"{path.name}: frequency metadata disagrees with filename")
    if int(metadata.get("cell_number", cell_number)) != cell_number:
        raise ValueError(f"{path.name}: metadata cell_number mismatch")
    if metadata["grid_origin"] != "explicit-center-cell":
        raise ValueError(f"{path.name}: unsupported v2 grid_origin")
    if not math.isclose(
        float(metadata.get("cell_size_m", cell_size)),
        cell_size,
        rel_tol=0.0,
        abs_tol=1e-12,
    ):
        raise ValueError(f"{path.name}: metadata cell_size_m mismatch")

    summary.update(
        {
            "schema": "v2",
            "schema_version": 2,
            "semantic_version": metadata["semantic_version"],
            "generator": metadata["generator"],
            "seed": int(metadata["seed"]),
            "realization_id": metadata["realization_id"],
            "grid_origin": metadata["grid_origin"],
            "pathloss_family": metadata["pathloss_family"],
            "los_state_model": metadata["los_state_model"],
            "los_probability_model": metadata["los_probability_model"],
            "coefficient_validity_range":
                metadata["coefficient_validity_range"],
            "metadata_sha256": canonical_sha256(metadata),
            "provenance_status": "deterministic-production-realization",
        }
    )
    return summary


def build_manifest(map_dir: Path) -> dict:
    map_paths = sorted(map_dir.glob("macroscopic_fading_map_*.json"))
    if not map_paths:
        raise ValueError(f"no FikoRE maps found in {map_dir}")
    maps = [summarize_map(path) for path in map_paths]
    schemas = sorted({entry["schema"] for entry in maps})
    all_v2 = schemas == ["v2"]
    manifest = {
        "schema_version": 2 if all_v2 else 1,
        "map_schema": schemas[0] if len(schemas) == 1 else "mixed",
        "map_count": len(map_paths),
        "notes": (
            [
                "Maps are deterministic production v2 realizations.",
                "Every map embeds its generator, seed, model, and realization metadata.",
                "Changed map bytes require a new realization identifier and checksum.",
            ]
            if all_v2
            else [
                "Legacy map arrays are historical stochastic realizations.",
                "Original RNG seeds and exact generator commits are not available.",
            ]
        ),
        "maps": maps,
    }

    catalog_path = map_dir / "CATALOG.json"
    if catalog_path.is_file():
        catalog = json.loads(catalog_path.read_text())
        catalog_entries = {
            entry["file"]: entry for entry in catalog.get("maps", [])
        }
        map_entries = {entry["file"]: entry for entry in maps}
        if set(catalog_entries) != set(map_entries):
            raise ValueError("CATALOG.json files do not match the map directory")
        for name, catalog_entry in catalog_entries.items():
            map_entry = map_entries[name]
            checks = {
                "scenario": map_entry["scenario"],
                "frequency_ghz": map_entry["frequency_ghz"],
                "seed": map_entry["seed"],
                "realization_id": map_entry.get("realization_id"),
                "sha256": map_entry["sha256"],
            }
            for key, expected in checks.items():
                if catalog_entry.get(key) != expected:
                    raise ValueError(
                        f"CATALOG.json {name}: {key} does not match map metadata")
        manifest["catalog"] = {
            "file": catalog_path.name,
            "sha256": sha256(catalog_path),
            "schema_version": catalog.get("schema_version"),
            "semantic_version": catalog.get("semantic_version"),
            "generator": catalog.get("generator"),
            "master_seed": catalog.get("master_seed"),
            "cell_number": catalog.get("cell_number"),
        }

    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--map-dir", type=Path, default=DEFAULT_MAP_DIR)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    manifest = build_manifest(args.map_dir.resolve())
    rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    output = args.output.resolve()
    if args.check:
        if not output.is_file() or output.read_text() != rendered:
            raise SystemExit(
                f"map manifest is stale; regenerate with {Path(__file__).name}")
        print(f"map manifest is current: {output}")
        return
    output.write_text(rendered)
    print(output)


if __name__ == "__main__":
    main()
