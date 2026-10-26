#!/usr/bin/env python3
"""Compare the original PHY baseline with a current profile summary."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

PROFILE_ALIASES = {
    "offline_umi_n256_fwa": "offline_umi_n258_fwa",
}


def load(path: Path) -> dict[tuple[str, str], dict]:
    with path.open() as handle:
        rows = list(csv.DictReader(handle))
    return {
        (
            PROFILE_ALIASES.get(row["profile"], row["profile"]),
            row["direction"],
        ): row
        for row in rows
    }


def number(row: dict, name: str) -> float:
    return float(row[name])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    baseline = load(args.baseline.resolve())
    current = load(args.current.resolve())
    rows = []
    for key in sorted(set(baseline) & set(current)):
        old = baseline[key]
        new = current[key]
        old_throughput = number(old, "throughput_mbps")
        new_throughput = number(new, "throughput_mbps")
        rows.append(
            {
                "profile": key[0],
                "direction": key[1],
                "baseline_throughput_mbps": old_throughput,
                "current_throughput_mbps": new_throughput,
                "throughput_delta_pct": (
                    100.0 * (new_throughput - old_throughput) / old_throughput
                    if old_throughput > 0.0
                    else math.nan
                ),
                "baseline_error_mbps": number(old, "error_mbps"),
                "current_error_mbps": number(new, "error_mbps"),
                "baseline_outage_ues": int(old["phy_outage_ues"]),
                "current_outage_ues": int(new["outage_ues"]),
                "baseline_max_gap_ms": number(
                    old, "max_nonoutage_zero_service_gap_ms"),
                "current_max_gap_ms": number(
                    new, "ue_max_service_gap_ms"),
                "baseline_payload_efficiency": number(
                    old, "sampled_payload_efficiency"),
                "current_payload_efficiency": number(
                    new, "grant_payload_efficiency"),
                "baseline_wall_s": number(old, "wall_runtime_s"),
                "current_wall_s": number(new, "wall_seconds"),
            }
        )

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with (output / "comparison.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    lines = [
        "# Original Baseline versus Production PHY Model V2",
        "",
        "Both inputs use whole-run metrics. This is a characterization of the "
        "combined approved configuration, PHY, PF, O2I, and map changes; it is "
        "not a one-factor causal attribution.",
        "",
        "| Profile | Dir. | Baseline throughput | V2 throughput | Delta | "
        "Outage UEs | Maximum non-outage gap | Payload/grant |",
        "|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row['profile']} | {row['direction'].upper()} | "
            f"{row['baseline_throughput_mbps']:.2f} | "
            f"{row['current_throughput_mbps']:.2f} | "
            f"{row['throughput_delta_pct']:+.1f}% | "
            f"{row['baseline_outage_ues']} -> {row['current_outage_ues']} | "
            f"{row['baseline_max_gap_ms']:.0f} -> "
            f"{row['current_max_gap_ms']:.0f} ms | "
            f"{100.0 * row['baseline_payload_efficiency']:.1f}% -> "
            f"{100.0 * row['current_payload_efficiency']:.1f}% |"
        )
    lines.extend(
        [
            "",
            "This combined table is not the map-catalog ablation. The paired "
            "legacy-v1 versus padded-v2.1 comparison is reported separately in "
            "`map-v2.1-paired-profile-comparison.md`.",
            "",
        ]
    )
    (output / "comparison.md").write_text("\n".join(lines))
    print(output)


if __name__ == "__main__":
    main()
