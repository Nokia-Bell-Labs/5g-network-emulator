#!/usr/bin/env python3
"""Compare profile-level metrics from two deterministic FikoRE run batches."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path

TOKEN = re.compile(r"^([^:]+):(.+)$")


def parse_tokens(line: str) -> dict[str, float]:
    values = {}
    for raw in line.split():
        match = TOKEN.match(raw)
        if match is None:
            continue
        try:
            values[match.group(1)] = float(match.group(2))
        except ValueError:
            pass
    return values


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else math.nan


def parse_ue(path: Path, profile: str) -> list[dict]:
    ue_id = int(path.stem.rsplit("_", 1)[1])
    traffic: dict[str, list[float]] = defaultdict(list)
    phy: dict[str, dict[str, list[float]]] = {
        "dl": defaultdict(list),
        "ul": defaultdict(list),
    }
    with path.open() as handle:
        for line in handle:
            values = parse_tokens(line)
            if "rul" in values:
                for key in ("rul", "rdl", "gul", "gdl", "eul", "edl"):
                    if key in values:
                        traffic[key].append(values[key])
            elif "sinr" in values and "tx" in values:
                direction = "ul" if int(values["tx"]) == 1 else "dl"
                for key in ("sinr", "mcs"):
                    if key in values:
                        phy[direction][key].append(values[key])

    rows = []
    for direction in ("dl", "ul"):
        suffix = direction
        throughput = mean(traffic[f"r{suffix}"])
        generated = mean(traffic[f"g{suffix}"])
        sinr = [
            value for value in phy[direction]["sinr"]
            if math.isfinite(value)
        ]
        mcs = phy[direction]["mcs"]
        rows.append(
            {
                "profile": profile,
                "ue_id": ue_id,
                "direction": direction,
                "generated_mbps": generated,
                "throughput_mbps": throughput,
                "error_mbps": mean(traffic[f"e{suffix}"]),
                "demand_satisfaction": (
                    min(throughput / generated, 1.0)
                    if generated > 0.0
                    else math.nan
                ),
                "mean_sinr_db": mean(sinr),
                "mean_mcs": mean(mcs),
                "outage_fraction": (
                    sum(value < 0.0 for value in mcs) / len(mcs)
                    if mcs
                    else math.nan
                ),
            }
        )
    return rows


def batch_metrics(manifest_path: Path) -> list[dict]:
    manifest = json.loads(manifest_path.read_text())
    ue_rows = []
    for run in manifest["runs"]:
        log_dir = Path(run["log_dir"])
        for path in sorted((log_dir / "ue").glob("ue_log_*.txt")):
            ue_rows.extend(parse_ue(path, run["profile"]))

    grouped: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in ue_rows:
        grouped[(row["profile"], row["direction"])].append(row)
    result = []
    for (profile, direction), rows in sorted(grouped.items()):
        result.append(
            {
                "profile": profile,
                "direction": direction,
                "generated_mbps": sum(row["generated_mbps"] for row in rows),
                "throughput_mbps": sum(row["throughput_mbps"] for row in rows),
                "error_mbps": sum(row["error_mbps"] for row in rows),
                "mean_demand_satisfaction": mean(
                    [row["demand_satisfaction"] for row in rows]),
                "zero_throughput_ues": sum(
                    row["throughput_mbps"] <= 1e-6 for row in rows),
                "outage_ues": sum(
                    row["outage_fraction"] >= 0.99 for row in rows),
                "mean_sinr_db": mean(
                    [row["mean_sinr_db"] for row in rows]),
                "mean_mcs": mean([row["mean_mcs"] for row in rows]),
            }
        )
    return result


def markdown_table(rows: list[dict]) -> str:
    columns = [
        "profile",
        "direction",
        "legacy_throughput_mbps",
        "candidate_throughput_mbps",
        "throughput_delta_pct",
        "legacy_mean_sinr_db",
        "candidate_mean_sinr_db",
        "sinr_delta_db",
        "legacy_outage_ues",
        "candidate_outage_ues",
    ]
    lines = [
        "| " + " | ".join(columns) + " |",
        "|" + "|".join("---" for _ in columns) + "|",
    ]
    for row in rows:
        lines.append(
            "| " + " | ".join(str(row[column]) for column in columns) + " |")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("legacy_manifest", type=Path)
    parser.add_argument("candidate_manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    legacy = {
        (row["profile"], row["direction"]): row
        for row in batch_metrics(args.legacy_manifest.resolve())
    }
    candidate = {
        (row["profile"], row["direction"]): row
        for row in batch_metrics(args.candidate_manifest.resolve())
    }
    rows = []
    for key in sorted(set(legacy) & set(candidate)):
        old = legacy[key]
        new = candidate[key]
        throughput_delta = (
            100.0
            * (new["throughput_mbps"] - old["throughput_mbps"])
            / old["throughput_mbps"]
            if old["throughput_mbps"] > 0.0
            else math.nan
        )
        rows.append(
            {
                "profile": key[0],
                "direction": key[1],
                "legacy_throughput_mbps": round(old["throughput_mbps"], 3),
                "candidate_throughput_mbps": round(new["throughput_mbps"], 3),
                "throughput_delta_pct": round(throughput_delta, 2),
                "legacy_mean_sinr_db": round(old["mean_sinr_db"], 3),
                "candidate_mean_sinr_db": round(new["mean_sinr_db"], 3),
                "sinr_delta_db": round(
                    new["mean_sinr_db"] - old["mean_sinr_db"], 3),
                "legacy_outage_ues": old["outage_ues"],
                "candidate_outage_ues": new["outage_ues"],
                "legacy_zero_throughput_ues": old["zero_throughput_ues"],
                "candidate_zero_throughput_ues": new["zero_throughput_ues"],
                "legacy_error_mbps": round(old["error_mbps"], 3),
                "candidate_error_mbps": round(new["error_mbps"], 3),
            }
        )

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with (output / "profile_deltas.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    report = [
        "# Legacy versus v2 Candidate Profile Runs",
        "",
        "Both batches use the same code, seed, traffic profiles, and duration. "
        "Only the selected macroscopic map catalog differs.",
        "",
        markdown_table(rows),
        "",
        "Candidate maps are not activated as production defaults by this report.",
        "",
    ]
    (output / "report.md").write_text("\n".join(report))
    print(output)


if __name__ == "__main__":
    main()
