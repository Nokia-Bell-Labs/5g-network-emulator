#!/usr/bin/env python3
"""Publish portable HARQ campaign inputs, summaries, and manifests."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS_FILES = (
    "profile-summary.csv",
    "ue-summary.csv",
    "report.md",
    "analysis-metadata.json",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_campaign(value: str) -> tuple[str, Path]:
    try:
        label, raw_path = value.split("=", 1)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "campaign must use LABEL=PATH"
        ) from error
    if not label:
        raise argparse.ArgumentTypeError("campaign label is empty")
    return label, Path(raw_path)


def publish_campaign(
    label: str,
    campaign: Path,
    output: Path,
) -> dict:
    campaign = campaign.resolve()
    source_manifest = campaign / "manifest.json"
    analysis = campaign / "analysis"
    if not source_manifest.is_file():
        raise FileNotFoundError(source_manifest)
    for name in ANALYSIS_FILES:
        if not (analysis / name).is_file():
            raise FileNotFoundError(analysis / name)

    manifest = json.loads(source_manifest.read_text())
    input_dir = output / "inputs" / label
    input_dir.mkdir(parents=True, exist_ok=True)
    for run in manifest["runs"]:
        source_input = Path(run["rendered_config"])
        destination = input_dir / source_input.name
        shutil.copyfile(source_input, destination)
        run["rendered_config"] = str(destination.relative_to(ROOT))
        run["rendered_config_sha256"] = sha256(destination)
        run["raw_logs_committed"] = False
        run.pop("log_dir", None)
        run.pop("stdout", None)
    manifest["artifact_policy"] = (
        "Portable rendered inputs and compact summaries are committed; "
        "raw logs remain local-only."
    )
    destination_manifest = output / f"{label}-manifest.json"
    destination_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    )

    artifacts = [destination_manifest]
    for name in ANALYSIS_FILES:
        suffix = Path(name).suffix
        stem = Path(name).stem
        destination = output / f"{label}-{stem}{suffix}"
        shutil.copyfile(analysis / name, destination)
        artifacts.append(destination)

    with (analysis / "profile-summary.csv").open() as handle:
        rows = list(csv.DictReader(handle))
    return {
        "label": label,
        "source_sha": manifest["source_sha"],
        "seed": manifest["seed"],
        "duration_s": manifest["duration_s"],
        "manifest": destination_manifest,
        "rows": rows,
        "artifacts": artifacts,
    }


def numeric(row: dict[str, str], key: str) -> float | None:
    value = row.get(key, "")
    return float(value) if value not in ("", None) else None


def format_number(value: float | None, digits: int = 2) -> str:
    return "n/a" if value is None else f"{value:.{digits}f}"


def write_comparison(campaigns: list[dict], output: Path) -> list[Path]:
    comparison_rows = []
    for campaign in campaigns:
        for row in campaign["rows"]:
            comparison_rows.append(
                {
                    "campaign": campaign["label"],
                    "source_sha": campaign["source_sha"],
                    "seed": campaign["seed"],
                    "duration_s": campaign["duration_s"],
                    **row,
                }
            )
    comparison_csv = output / "comparison.csv"
    fieldnames = []
    for row in comparison_rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with comparison_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=fieldnames,
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(comparison_rows)

    by_key: dict[tuple[str, str], dict[str, dict[str, str]]] = {}
    for row in comparison_rows:
        key = (row["profile"], row["direction"])
        by_key.setdefault(key, {})[row["campaign"]] = row
    labels = [campaign["label"] for campaign in campaigns]
    lines = [
        "# HARQ Campaign Evidence",
        "",
        "All rates are Mbit/s. Each row uses identical profile geometry, "
        "traffic, mobility, and seed within its paired campaign.",
        "",
        "The disabled, no-retry, and production arms last 180 seconds "
        "with a 20-second warm-up. The two additional production seed "
        "sweeps last 30 seconds with a 2-second warm-up and are "
        "sensitivity checks, not direct long-run estimates.",
        "",
        "| Profile | Direction | "
        + " | ".join(labels)
        + " |",
        "|---|---|" + "|".join("---:" for _ in labels) + "|",
    ]
    for (profile, direction), rows in sorted(by_key.items()):
        throughput = [
            format_number(
                numeric(rows[label], "throughput_mbps")
                if label in rows
                else None
            )
            for label in labels
        ]
        lines.append(
            f"| {profile} | {direction.upper()} | "
            + " | ".join(throughput)
            + " |"
        )

    observable = [
        campaign
        for campaign in campaigns
        if any(
            row.get("retransmitted_mbps", "") != ""
            for row in campaign["rows"]
        )
    ]
    if observable:
        lines.extend(
            [
                "",
                "## HARQ and accounting telemetry",
                "",
                "| Campaign | Maximum retransmission rate | "
                "Maximum radio-drop rate | Maximum HARQ queue | "
                "Maximum absolute closure residual |",
                "|---|---:|---:|---:|---:|",
            ]
        )
        for campaign in observable:
            rows = campaign["rows"]
            retransmission = max(
                value
                for row in rows
                if (value := numeric(row, "retransmitted_mbps"))
                is not None
            )
            radio_drop = max(
                value
                for row in rows
                if (value := numeric(row, "radio_dropped_mbps"))
                is not None
            )
            queue = max(
                value
                for row in rows
                if (value := numeric(row, "harq_queue_blocks_max"))
                is not None
            )
            residual = max(
                value
                for row in rows
                if (
                    value := numeric(
                        row,
                        "conservation_residual_abs_max_bits",
                    )
                )
                is not None
            )
            lines.append(
                f"| {campaign['label']} | {retransmission:.2f} Mbit/s | "
                f"{radio_drop:.2f} Mbit/s | {queue:.0f} blocks | "
                f"{residual:.0f} bit |"
            )
    lines.extend(
        [
            "",
            "The embedded legacy BLER table is reproducible but not "
            "externally calibrated. These results characterize this exact "
            "implementation; they do not establish deployment BLER.",
            "",
        ]
    )
    report = output / "report.md"
    report.write_text("\n".join(lines))
    return [comparison_csv, report]


def write_o2i_pair(output: Path) -> Path:
    source = output / "disabled-ue-summary.csv"
    with source.open() as handle:
        rows = list(csv.DictReader(handle))
    by_profile: dict[str, dict[tuple[str, str], dict[str, str]]] = {}
    for row in rows:
        if row["profile"] not in {
            "offline_umi_n258_fwa",
            "offline_umi_n258_fwa_high_loss",
        }:
            continue
        key = (row["ue_id"], row["direction"])
        by_profile.setdefault(row["profile"], {})[key] = row
    nominal = by_profile["offline_umi_n258_fwa"]
    high_loss = by_profile["offline_umi_n258_fwa_high_loss"]
    if nominal.keys() != high_loss.keys():
        raise ValueError("n258 O2I arms do not contain the same UE keys")

    output_rows = []
    for direction in ("dl", "ul"):
        keys = sorted(
            (key for key in nominal if key[1] == direction),
            key=lambda key: int(key[0]),
        )
        for key in keys:
            reductions = {
                metric: (
                    float(nominal[key][metric])
                    - float(high_loss[key][metric])
                )
                for metric in (
                    "sinr_p05_db",
                    "sinr_p50_db",
                    "sinr_p95_db",
                )
            }
            values = list(reductions.values())
            output_rows.append(
                {
                    "direction": direction,
                    "ue_id": key[0],
                    "p05_reduction_db": reductions["sinr_p05_db"],
                    "p50_reduction_db": reductions["sinr_p50_db"],
                    "p95_reduction_db": reductions["sinr_p95_db"],
                    "mean_reduction_db": statistics.fmean(values),
                    "quantile_reduction_std_db": statistics.pstdev(values),
                    "quantile_reduction_span_db": max(values) - min(values),
                }
            )
    destination = output / "o2i-paired-sinr.csv"
    with destination.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=list(output_rows[0]),
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(output_rows)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--campaign",
        action="append",
        type=parse_campaign,
        required=True,
        help="LABEL=PATH; may be repeated",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        if not args.replace:
            raise SystemExit(
                f"refusing to replace existing output: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True)

    campaigns = [
        publish_campaign(label, path, output)
        for label, path in args.campaign
    ]
    artifacts = [
        artifact
        for campaign in campaigns
        for artifact in campaign["artifacts"]
    ]
    artifacts.extend(write_comparison(campaigns, output))
    artifacts.append(write_o2i_pair(output))
    evidence = {
        "schema_version": 1,
        "campaigns": [
            {
                "label": campaign["label"],
                "source_sha": campaign["source_sha"],
                "seed": campaign["seed"],
                "duration_s": campaign["duration_s"],
                "manifest": str(
                    campaign["manifest"].relative_to(ROOT)
                ),
                "manifest_sha256": sha256(campaign["manifest"]),
            }
            for campaign in campaigns
        ],
        "artifacts": [
            {
                "path": str(path.relative_to(ROOT)),
                "sha256": sha256(path),
            }
            for path in sorted(artifacts)
        ],
    }
    evidence_path = output / "evidence.json"
    evidence_path.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    )
    print(output)


if __name__ == "__main__":
    main()
