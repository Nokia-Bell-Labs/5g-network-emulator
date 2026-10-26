#!/usr/bin/env python3
"""Summarize deterministic PHY Model V2 profile logs."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

import numpy as np

TOKEN = re.compile(r"^([^:]+):(.+)$")
DIRECTIONS = ("dl", "ul")
WINDOWS_S = (0.01, 0.1, 1.0)


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


def safe_mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else math.nan


def finite_sum(values: list[float]) -> float:
    finite = [value for value in values if math.isfinite(value)]
    return sum(finite) if finite else math.nan


def cumulative_rate_mbps(
    samples: list[tuple[float, float]],
) -> float:
    if len(samples) < 2:
        return math.nan
    elapsed = samples[-1][0] - samples[0][0]
    if elapsed <= 0.0:
        return math.nan
    return (samples[-1][1] - samples[0][1]) / elapsed / 1e6


def percentile(values: list[float], probability: float) -> float:
    return (
        float(np.quantile(np.asarray(values, dtype=float), probability))
        if values
        else math.nan
    )


def sampled_interval(timestamps: list[float]) -> float:
    differences = [
        later - earlier
        for earlier, later in zip(timestamps[:-1], timestamps[1:])
        if later > earlier
    ]
    return statistics.median(differences) if differences else 0.0


def zero_window_counts(
    samples: list[tuple[float, float, float]],
    warmup_s: float,
    window_s: float,
) -> tuple[int, int]:
    windows: dict[int, list[float]] = defaultdict(lambda: [0.0, 0.0])
    for timestamp, delivered, generated in samples:
        index = int(math.floor((timestamp - warmup_s) / window_s + 1e-9))
        windows[index][0] += delivered
        windows[index][1] += generated
    eligible = [values for values in windows.values() if values[1] > 0.0]
    return sum(values[0] <= 1e-9 for values in eligible), len(eligible)


def service_gaps(
    samples: list[tuple[float, float, float]],
    warmup_s: float,
    end_s: float,
    sample_interval_s: float | None = None,
) -> list[float]:
    if not samples:
        return []
    timestamps = [sample[0] for sample in samples]
    interval = (
        sample_interval_s
        if sample_interval_s is not None
        else sampled_interval(timestamps)
    )
    served = [
        timestamp
        for timestamp, delivered, generated in samples
        if generated > 0.0 and delivered > 1e-9
    ]
    if not served:
        return [max(0.0, end_s - warmup_s)]
    gaps = [max(0.0, served[0] - warmup_s)]
    gaps.extend(
        max(0.0, later - earlier - interval)
        for earlier, later in zip(served[:-1], served[1:])
    )
    gaps.append(max(0.0, end_s - served[-1] - interval))
    return gaps


def positive_offer_segments(
    samples: list[tuple[float, float, float]],
) -> list[list[tuple[float, float, float]]]:
    if not samples:
        return []
    interval = sampled_interval([sample[0] for sample in samples])
    segments: list[list[tuple[float, float, float]]] = []
    current: list[tuple[float, float, float]] = []
    previous_timestamp = None
    for sample in samples:
        timestamp, _, generated = sample
        discontinuity = (
            previous_timestamp is not None
            and interval > 0.0
            and timestamp - previous_timestamp > 1.5 * interval
        )
        if generated <= 0.0 or discontinuity:
            if current:
                segments.append(current)
                current = []
        if generated > 0.0:
            current.append(sample)
        previous_timestamp = timestamp
    if current:
        segments.append(current)
    return segments


def parse_ue(
    path: Path,
    profile: str,
    warmup_s: float,
    configured_duration_s: float | None,
) -> list[dict]:
    ue_id = int(path.stem.rsplit("_", 1)[1])
    traffic: dict[str, list[tuple[float, float, float, float]]] = {
        direction: [] for direction in DIRECTIONS
    }
    quality: dict[str, dict[str, list[float]]] = {
        direction: defaultdict(list) for direction in DIRECTIONS
    }
    telemetry: dict[str, dict[str, list[tuple[float, float]]]] = {
        direction: defaultdict(list) for direction in DIRECTIONS
    }
    maximum_timestamp = 0.0
    with path.open() as handle:
        for line in handle:
            values = parse_tokens(line)
            timestamp = values.get("ts")
            if timestamp is None:
                continue
            maximum_timestamp = max(maximum_timestamp, timestamp)
            if timestamp < warmup_s or (
                configured_duration_s is not None
                and timestamp >= configured_duration_s
            ):
                continue
            if "rul" in values:
                for direction in DIRECTIONS:
                    suffix = direction
                    traffic[direction].append(
                        (
                            timestamp,
                            values.get(f"r{suffix}", 0.0),
                            values.get(f"g{suffix}", 0.0),
                            values.get(f"e{suffix}", 0.0),
                        )
                    )
                    tokens = {
                        "admitted_bits": f"admb{suffix}",
                        "delivered_bits": f"delb{suffix}",
                        "expired_bits": f"expb{suffix}",
                        "queue_dropped_bits": f"qdropb{suffix}",
                        "radio_dropped_bits": f"rdropb{suffix}",
                        "retransmitted_bits": f"rtxb{suffix}",
                        "pending_bits": f"pendb{suffix}",
                        "conservation_residual_bits": f"cresb{suffix}",
                        "harq_queue_blocks": f"harqq{suffix}",
                        "harq_high_water_blocks": f"harqhw{suffix}",
                        "harq_oldest_age_s": f"harqage{suffix}",
                        "harq_retry_ordinal": f"harqretry{suffix}",
                    }
                    for name, token in tokens.items():
                        value = values.get(token)
                        if value is not None and math.isfinite(value):
                            telemetry[direction][name].append(
                                (timestamp, value)
                            )
            elif "sinr" in values and "tx" in values:
                direction = "ul" if int(values["tx"]) == 1 else "dl"
                for key in ("sinr", "mcs"):
                    value = values.get(key)
                    if value is not None and math.isfinite(value):
                        quality[direction][key].append(value)

    end_s = configured_duration_s or maximum_timestamp
    rows = []
    for direction in DIRECTIONS:
        samples = traffic[direction]
        delivered = [sample[1] for sample in samples]
        generated = [sample[2] for sample in samples]
        errors = [sample[3] for sample in samples]
        generated_mean = safe_mean(generated)
        delivered_mean = safe_mean(delivered)
        mcs = quality[direction]["mcs"]
        sinr = quality[direction]["sinr"]
        outage_fraction = (
            sum(value < 0.0 for value in mcs) / len(mcs)
            if mcs
            else math.nan
        )
        outage = bool(
            math.isfinite(outage_fraction) and outage_fraction >= 0.99)
        active_samples = [
            (timestamp, received, offered)
            for timestamp, received, offered, _ in samples
        ]
        active_segments = positive_offer_segments(active_samples)
        traffic_interval = sampled_interval(
            [sample[0] for sample in active_samples])
        gaps = (
            []
            if outage
            else [
                gap
                for segment in active_segments
                for gap in service_gaps(
                    segment,
                    segment[0][0],
                    min(
                        end_s,
                        segment[-1][0] + traffic_interval,
                    ),
                    traffic_interval,
                )
            ]
        )
        row = {
            "profile": profile,
            "ue_id": ue_id,
            "direction": direction,
            "generated_mbps": generated_mean,
            "throughput_mbps": delivered_mean,
            "error_mbps": safe_mean(errors),
            "demand_satisfaction": (
                min(delivered_mean / generated_mean, 1.0)
                if generated_mean > 0.0
                else math.nan
            ),
            "sinr_p05_db": percentile(sinr, 0.05),
            "sinr_p50_db": percentile(sinr, 0.50),
            "sinr_p95_db": percentile(sinr, 0.95),
            "mcs_p05": percentile(mcs, 0.05),
            "mcs_p50": percentile(mcs, 0.50),
            "mcs_p95": percentile(mcs, 0.95),
            "outage_fraction": outage_fraction,
            "outage": outage,
            "zero_throughput": bool(
                math.isfinite(delivered_mean) and delivered_mean <= 1e-9),
            "service_gap_p50_ms": 1000.0 * percentile(gaps, 0.50),
            "service_gap_p95_ms": 1000.0 * percentile(gaps, 0.95),
            "service_gap_p99_ms": 1000.0 * percentile(gaps, 0.99),
            "service_gap_max_ms": (
                1000.0 * max(gaps) if gaps else math.nan),
            "_gaps_s": gaps,
            "admitted_mbps": cumulative_rate_mbps(
                telemetry[direction]["admitted_bits"]),
            "delivered_counter_mbps": cumulative_rate_mbps(
                telemetry[direction]["delivered_bits"]),
            "expired_mbps": cumulative_rate_mbps(
                telemetry[direction]["expired_bits"]),
            "queue_dropped_mbps": cumulative_rate_mbps(
                telemetry[direction]["queue_dropped_bits"]),
            "radio_dropped_mbps": cumulative_rate_mbps(
                telemetry[direction]["radio_dropped_bits"]),
            "retransmitted_mbps": cumulative_rate_mbps(
                telemetry[direction]["retransmitted_bits"]),
            "pending_bits_last": (
                telemetry[direction]["pending_bits"][-1][1]
                if telemetry[direction]["pending_bits"]
                else math.nan
            ),
            "harq_queue_blocks_max": (
                max(value for _, value in telemetry[direction]["harq_queue_blocks"])
                if telemetry[direction]["harq_queue_blocks"]
                else math.nan
            ),
            "harq_high_water_blocks": (
                telemetry[direction]["harq_high_water_blocks"][-1][1]
                if telemetry[direction]["harq_high_water_blocks"]
                else math.nan
            ),
            "harq_oldest_age_ms_max": (
                1000.0
                * max(value for _, value in telemetry[direction]["harq_oldest_age_s"])
                if telemetry[direction]["harq_oldest_age_s"]
                else math.nan
            ),
            "harq_retry_ordinal_max": (
                max(value for _, value in telemetry[direction]["harq_retry_ordinal"])
                if telemetry[direction]["harq_retry_ordinal"]
                else math.nan
            ),
            "conservation_residual_abs_max_bits": (
                max(
                    abs(value)
                    for _, value in telemetry[direction][
                        "conservation_residual_bits"
                    ]
                )
                if telemetry[direction]["conservation_residual_bits"]
                else math.nan
            ),
        }
        for window_s in WINDOWS_S:
            zero, total = (
                (0, 0)
                if outage
                else zero_window_counts(
                    active_samples, warmup_s, window_s)
            )
            label = f"{int(window_s * 1000)}ms"
            row[f"zero_{label}_windows"] = zero
            row[f"eligible_{label}_windows"] = total
            row[f"zero_{label}_fraction"] = (
                zero / total if total else math.nan)
        rows.append(row)
    return rows


def parse_grid(
    path: Path,
    warmup_s: float,
    configured_duration_s: float | None,
) -> dict:
    expected_units = 0
    grants = 0
    nominal_bits = 0.0
    effective_bits = 0.0
    units_by_timestamp: dict[float, int] = defaultdict(int)
    assigned_by_timestamp: dict[float, int] = defaultdict(int)
    summary_available = 0
    summary_assigned = 0
    summary_effective = 0
    summary_wasted = 0
    summary_empty = 0
    summary_structural = 0
    summary_rows = 0
    with path.open() as handle:
        for line_index, line in enumerate(handle):
            values = parse_tokens(line)
            if line_index == 0:
                expected_units = int(
                    values.get("f", 0) * values.get("t", 0))
                continue
            timestamp = values.get("ts")
            if (
                timestamp is None
                or timestamp < warmup_s
                or (
                    configured_duration_s is not None
                    and timestamp >= configured_duration_s
                )
            ):
                continue
            if "summary" in values:
                summary_available += int(values.get("available", 0.0))
                summary_assigned += int(values.get("assigned", 0.0))
                summary_effective += int(values.get("effective", 0.0))
                summary_wasted += int(values.get("wasted", 0.0))
                summary_empty += int(values.get("empty", 0.0))
                summary_structural += int(values.get("structural", 0.0))
                summary_rows += 1
                continue
            units_by_timestamp[timestamp] += 1
            if values.get("id", -1.0) >= 0.0:
                assigned_by_timestamp[timestamp] += 1
                grants += 1
                nominal_bits += values.get("tp", 0.0)
                effective_bits += values.get("e_tp", 0.0)
    opportunities = expected_units * len(units_by_timestamp)
    assigned = sum(assigned_by_timestamp.values())
    available = summary_available if summary_rows else opportunities
    assigned_for_fill = summary_assigned if summary_rows else assigned
    return {
        "available_units": available,
        "structural_unavailable_units": summary_structural,
        "empty_available_units": (
            summary_empty
            if summary_rows
            else max(0, opportunities - assigned)
        ),
        "grid_summary_ttis": summary_rows,
        "legacy_conditional_fill": not bool(summary_rows),
        "logged_units": sum(units_by_timestamp.values()),
        "assigned_units": assigned,
        "resource_fill_fraction": (
            assigned_for_fill / available if available else math.nan),
        "effective_resource_fill_fraction": (
            summary_effective / available
            if summary_rows and available
            else math.nan
        ),
        "zero_effective_assigned_fraction": (
            summary_wasted / summary_assigned
            if summary_rows and summary_assigned
            else math.nan
        ),
        "grant_payload_efficiency": (
            effective_bits / nominal_bits if nominal_bits > 0.0 else math.nan),
        "sampled_nominal_grant_bits": nominal_bits,
        "sampled_effective_payload_bits": effective_bits,
        "sampled_grants": grants,
    }


def summarize(
    manifest: dict,
    ue_rows: list[dict],
    grid_metrics: dict[tuple[str, str], dict],
    warmup_s: float,
) -> list[dict]:
    elapsed = {
        run["profile"]: run["elapsed_seconds"]
        for run in manifest["runs"]
    }
    grouped: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in ue_rows:
        grouped[(row["profile"], row["direction"])].append(row)
    summaries = []
    for (profile, direction), rows in sorted(grouped.items()):
        non_outage = [row for row in rows if not row["outage"]]
        gaps = [
            gap
            for row in non_outage
            for gap in row["_gaps_s"]
        ]
        ue_max_gaps_ms = [
            row["service_gap_max_ms"]
            for row in non_outage
            if math.isfinite(row["service_gap_max_ms"])
        ]
        generated = sum(row["generated_mbps"] for row in rows)
        delivered = sum(row["throughput_mbps"] for row in rows)
        grid = grid_metrics.get((profile, direction), {})
        summary = {
            "profile": profile,
            "direction": direction,
            "seed": manifest.get("seed", ""),
            "duration_s": manifest.get("duration_s", ""),
            "warmup_s": warmup_s,
            "ues": len(rows),
            "generated_mbps": generated,
            "throughput_mbps": delivered,
            "error_mbps": sum(row["error_mbps"] for row in rows),
            "admitted_mbps": finite_sum(
                [row["admitted_mbps"] for row in rows]),
            "delivered_counter_mbps": finite_sum(
                [row["delivered_counter_mbps"] for row in rows]),
            "expired_mbps": finite_sum(
                [row["expired_mbps"] for row in rows]),
            "queue_dropped_mbps": finite_sum(
                [row["queue_dropped_mbps"] for row in rows]),
            "radio_dropped_mbps": finite_sum(
                [row["radio_dropped_mbps"] for row in rows]),
            "retransmitted_mbps": finite_sum(
                [row["retransmitted_mbps"] for row in rows]),
            "pending_bits_last": finite_sum(
                [row["pending_bits_last"] for row in rows]),
            "harq_queue_blocks_max": max(
                (
                    row["harq_queue_blocks_max"]
                    for row in rows
                    if math.isfinite(row["harq_queue_blocks_max"])
                ),
                default=math.nan,
            ),
            "harq_high_water_blocks": max(
                (
                    row["harq_high_water_blocks"]
                    for row in rows
                    if math.isfinite(row["harq_high_water_blocks"])
                ),
                default=math.nan,
            ),
            "harq_oldest_age_ms_max": max(
                (
                    row["harq_oldest_age_ms_max"]
                    for row in rows
                    if math.isfinite(row["harq_oldest_age_ms_max"])
                ),
                default=math.nan,
            ),
            "harq_retry_ordinal_max": max(
                (
                    row["harq_retry_ordinal_max"]
                    for row in rows
                    if math.isfinite(row["harq_retry_ordinal_max"])
                ),
                default=math.nan,
            ),
            "conservation_residual_abs_max_bits": max(
                (
                    row["conservation_residual_abs_max_bits"]
                    for row in rows
                    if math.isfinite(
                        row["conservation_residual_abs_max_bits"])
                ),
                default=math.nan,
            ),
            "aggregate_demand_satisfaction": (
                min(delivered / generated, 1.0)
                if generated > 0.0
                else math.nan
            ),
            "mean_ue_demand_satisfaction": safe_mean(
                [row["demand_satisfaction"] for row in rows]),
            "outage_ues": sum(row["outage"] for row in rows),
            "zero_throughput_ues": sum(
                row["zero_throughput"] for row in rows),
            "sinr_p05_db": percentile(
                [row["sinr_p05_db"] for row in rows], 0.50),
            "sinr_p50_db": percentile(
                [row["sinr_p50_db"] for row in rows], 0.50),
            "sinr_p95_db": percentile(
                [row["sinr_p95_db"] for row in rows], 0.50),
            "mcs_p05": percentile(
                [row["mcs_p05"] for row in rows], 0.50),
            "mcs_p50": percentile(
                [row["mcs_p50"] for row in rows], 0.50),
            "mcs_p95": percentile(
                [row["mcs_p95"] for row in rows], 0.50),
            "pooled_service_gap_p50_ms": 1000.0 * percentile(gaps, 0.50),
            "pooled_service_gap_p95_ms": 1000.0 * percentile(gaps, 0.95),
            "pooled_service_gap_p99_ms": 1000.0 * percentile(gaps, 0.99),
            "ue_max_service_gap_p50_ms": percentile(
                ue_max_gaps_ms, 0.50),
            "ue_max_service_gap_p95_ms": percentile(
                ue_max_gaps_ms, 0.95),
            "ue_max_service_gap_p99_ms": percentile(
                ue_max_gaps_ms, 0.99),
            "ue_max_service_gap_ms": (
                max(ue_max_gaps_ms) if ue_max_gaps_ms else math.nan),
            "wall_seconds": elapsed.get(profile, math.nan),
            **grid,
        }
        for window_s in WINDOWS_S:
            label = f"{int(window_s * 1000)}ms"
            zero = sum(row[f"zero_{label}_windows"] for row in non_outage)
            total = sum(
                row[f"eligible_{label}_windows"] for row in non_outage)
            summary[f"zero_{label}_window_fraction"] = (
                zero / total if total else math.nan)
        summaries.append(summary)
    return summaries


def csv_value(value: object) -> object:
    if isinstance(value, float) and not math.isfinite(value):
        return ""
    return value


def write_csv(path: Path, rows: list[dict], private: set[str] | None = None) -> None:
    private = private or set()
    columns = [key for key in rows[0] if key not in private]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {key: csv_value(row[key]) for key in columns})


def report(manifest: dict, summaries: list[dict], warmup_s: float) -> str:
    lines = [
        "# PHY Model V2 Five-Profile Validation",
        "",
        f"- Batch: `{manifest.get('batch_id', 'legacy-local-batch')}`",
        f"- Source: `{manifest.get('source_sha', 'not recorded')}`",
        f"- Seed: `{manifest.get('seed', 'not recorded')}`",
        f"- Warm-up excluded: {warmup_s:g} seconds",
        "",
        "| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | "
        "Zero-delivery windows (10/100/1000 ms) | "
        "UE max delivery-gap P50/P95/P99/max (ms) | "
        "Grid assigned/effective | "
        "Payload/grant | Retx/radio loss | "
        "HARQ max queue/age | Conservation residual | Wall time |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
        "---:|---:|---:|",
    ]
    for row in summaries:
        efficiency = row.get("grant_payload_efficiency", math.nan)
        efficiency_text = (
            "n/a"
            if not isinstance(efficiency, float) or not math.isfinite(efficiency)
            else f"{100.0 * efficiency:.1f}%"
        )
        fill = row.get("resource_fill_fraction", math.nan)
        fill_text = (
            "n/a"
            if not isinstance(fill, float) or not math.isfinite(fill)
            else f"{100.0 * fill:.1f}%"
        )
        effective_fill = row.get(
            "effective_resource_fill_fraction", math.nan)
        effective_fill_text = (
            "n/a"
            if not isinstance(effective_fill, float)
            or not math.isfinite(effective_fill)
            else f"{100.0 * effective_fill:.1f}%"
        )
        retransmission_text = (
            "n/a"
            if not math.isfinite(row["retransmitted_mbps"])
            else f"{row['retransmitted_mbps']:.2f} / "
                 f"{row['radio_dropped_mbps']:.2f} Mbit/s"
        )
        harq_queue_text = (
            "n/a"
            if not math.isfinite(row["harq_queue_blocks_max"])
            else f"{row['harq_queue_blocks_max']:.0f} / "
                 f"{row['harq_oldest_age_ms_max']:.1f} ms"
        )
        residual_text = (
            "n/a"
            if not math.isfinite(
                row["conservation_residual_abs_max_bits"])
            else f"{row['conservation_residual_abs_max_bits']:.0f} bit"
        )
        lines.append(
            f"| {row['profile']} | {row['direction'].upper()} | "
            f"{row['generated_mbps']:.2f} | "
            f"{row['throughput_mbps']:.2f} | "
            f"{row['error_mbps']:.2f} | {row['outage_ues']} | "
            f"{100.0 * row['zero_10ms_window_fraction']:.1f}% / "
            f"{100.0 * row['zero_100ms_window_fraction']:.1f}% / "
            f"{100.0 * row['zero_1000ms_window_fraction']:.1f}% | "
            f"{row['ue_max_service_gap_p50_ms']:.0f} / "
            f"{row['ue_max_service_gap_p95_ms']:.0f} / "
            f"{row['ue_max_service_gap_p99_ms']:.0f} / "
            f"{row['ue_max_service_gap_ms']:.0f} | "
            f"{fill_text} / {effective_fill_text} | "
            f"{efficiency_text} | "
            f"{retransmission_text} | "
            f"{harq_queue_text} | "
            f"{residual_text} | "
            f"{row['wall_seconds']:.2f} s |"
        )
    lines.extend(
        [
            "",
            "Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude "
            "UEs classified as permanent PHY outage. A PHY-outage UE has MCS "
            "below zero in at least 99% of post-warm-up radio samples.",
            "",
            "A zero-delivery window has positive offered traffic and no delivered "
            "payload in that non-overlapping window. This avoids interpreting "
            "every unassigned TTI as user starvation.",
            "",
            "The delivery-gap distribution in the table is the distribution of "
            "each non-outage UE's maximum observed gap inside contiguous "
            "positive-offer segments. It is not a queue-backlog or scheduler-"
            "starvation metric. Pooled gap quantiles remain available in the CSV.",
            "",
            "Grid assigned/effective values are fractions of physically "
            "available frequency-time units. Per-TTI summaries separate "
            "TDD-unavailable, available-but-empty, and assigned-with-zero-"
            "effective-payload units.",
            "",
            "Payload/grant efficiency is sampled from logged grid grants and is "
            "the sum of effective payload bits divided by nominal grant bits.",
            "",
            "Retx/radio loss reports retransmitted air bits and terminal "
            "radio-dropped payload as Mbit/s. HARQ queue age is measured from "
            "the original IP arrival time. The conservation residual is the "
            "largest absolute difference between admitted bits and delivered, "
            "expired, queue-dropped, radio-dropped, and pending bits.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--warmup-s", type=float, default=20.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text())
    configured_duration = manifest.get("duration_s")
    ue_rows = []
    grid_metrics = {}
    for run in manifest["runs"]:
        profile = run["profile"]
        log_dir = Path(run["log_dir"])
        for path in sorted((log_dir / "ue").glob("ue_log_*.txt")):
            ue_rows.extend(
                parse_ue(
                    path,
                    profile,
                    args.warmup_s,
                    configured_duration,
                )
            )
        for direction in DIRECTIONS:
            grid_path = log_dir / "mac" / f"grid_log_{direction}.txt"
            if grid_path.is_file():
                grid_metrics[(profile, direction)] = parse_grid(
                    grid_path, args.warmup_s, configured_duration)

    summaries = summarize(
        manifest, ue_rows, grid_metrics, args.warmup_s)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    write_csv(output / "profile-summary.csv", summaries)
    write_csv(output / "ue-summary.csv", ue_rows, {"_gaps_s"})
    (output / "report.md").write_text(
        report(manifest, summaries, args.warmup_s))
    analysis_metadata = {
        "schema_version": 1,
        "source_manifest": str(manifest_path),
        "source_manifest_sha256": __import__("hashlib").sha256(
            manifest_path.read_bytes()).hexdigest(),
        "warmup_s": args.warmup_s,
        "service_windows_s": WINDOWS_S,
        "outage_definition": "MCS below zero in >=99% of eligible samples",
        "service_conditioning": (
            "contiguous positive-offer segments, non-outage UE; "
            "queue backlog is not observed"
        ),
    }
    (output / "analysis-metadata.json").write_text(
        json.dumps(analysis_metadata, indent=2, sort_keys=True) + "\n")
    print(output)


if __name__ == "__main__":
    main()
