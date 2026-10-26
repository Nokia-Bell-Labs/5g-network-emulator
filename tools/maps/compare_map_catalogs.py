#!/usr/bin/env python3
"""Compare legacy and candidate map catalogs on common spatial samples."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path

import numpy as np

MAP_NAME = re.compile(
    r"macroscopic_fading_map_(?P<scenario>[A-Z_]+)_(?P<frequency>[0-9.]+)\.json"
)


def load_map(path: Path) -> dict:
    payload = json.loads(path.read_text())
    return {
        "schema_version": int(payload.get("schema_version", 1)),
        "cell_number": int(payload["cell_number"]),
        "cell_size": float(payload["cell_size"]),
        "values": np.asarray(payload["map"], dtype=float),
    }


def apothem(data: dict) -> float:
    if data["schema_version"] >= 2:
        return (data["cell_number"] - 1) * data["cell_size"] / 2.0
    return data["cell_number"] * data["cell_size"] / 2.0


def interpolate(data: dict, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    count = data["cell_number"]
    origin = (
        (count - 1) / 2.0
        if data["schema_version"] >= 2
        else count / 2.0
    )
    x_index = np.clip(x / data["cell_size"] + origin, 0, count - 1)
    y_index = np.clip(y / data["cell_size"] + origin, 0, count - 1)
    x0 = np.floor(x_index).astype(int)
    x1 = np.ceil(x_index).astype(int)
    y0 = np.floor(y_index).astype(int)
    y1 = np.ceil(y_index).astype(int)
    wx = x_index - x0
    wy = y_index - y0
    values = data["values"]
    return (
        (1.0 - wx) * (1.0 - wy) * values[y0, x0]
        + wx * (1.0 - wy) * values[y0, x1]
        + (1.0 - wx) * wy * values[y1, x0]
        + wx * wy * values[y1, x1]
    )


def markdown_table(rows: list[dict]) -> str:
    columns = [
        "map",
        "samples",
        "legacy_mean_db",
        "v2_mean_db",
        "delta_p05_db",
        "delta_median_db",
        "delta_p95_db",
        "delta_rmse_db",
    ]
    lines = [
        "| " + " | ".join(columns) + " |",
        "|" + "|".join("---" for _ in columns) + "|",
    ]
    for row in rows:
        lines.append(
            "| "
            + " | ".join(str(row[column]) for column in columns)
            + " |")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("legacy", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--seed", type=int, default=20260927)
    parser.add_argument("--samples", type=int, default=20000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    legacy_dir = args.legacy.resolve()
    candidate_dir = args.candidate.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(args.seed)
    rows = []

    for candidate_path in sorted(
        candidate_dir.glob("macroscopic_fading_map_*.json")):
        if MAP_NAME.fullmatch(candidate_path.name) is None:
            continue
        legacy_path = legacy_dir / candidate_path.name
        if not legacy_path.is_file():
            continue
        legacy = load_map(legacy_path)
        candidate = load_map(candidate_path)
        radius = min(apothem(legacy), apothem(candidate))
        x = rng.uniform(-radius, radius, args.samples)
        y = rng.uniform(-radius, radius, args.samples)
        old_values = interpolate(legacy, x, y)
        new_values = interpolate(candidate, x, y)
        delta = new_values - old_values
        rows.append(
            {
                "map": candidate_path.name,
                "samples": args.samples,
                "legacy_mean_db": round(float(np.mean(old_values)), 3),
                "v2_mean_db": round(float(np.mean(new_values)), 3),
                "delta_p05_db": round(float(np.quantile(delta, 0.05)), 3),
                "delta_median_db": round(float(np.median(delta)), 3),
                "delta_p95_db": round(float(np.quantile(delta, 0.95)), 3),
                "delta_rmse_db": round(
                    math.sqrt(float(np.mean(delta * delta))), 3),
            }
        )

    csv_path = output / "map_deltas.csv"
    with csv_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    report = [
        "# Legacy versus v2 Map Comparison",
        "",
        f"Common spatial samples per map: {args.samples}.",
        "",
        "A positive delta means the v2 map has higher macroscopic link gain "
        "(less attenuation) at the sampled position.",
        "",
        markdown_table(rows),
        "",
        "These deltas combine the approved origin, UMa LOS, binary LOS-state, "
        "and new stochastic realization changes. They are not a pointwise "
        "regression tolerance.",
        "",
    ]
    (output / "report.md").write_text("\n".join(report))
    print(output)


if __name__ == "__main__":
    main()
