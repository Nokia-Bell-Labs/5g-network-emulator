#!/usr/bin/env python3
"""Characterize throughput schedulers across traffic and grid conditions."""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
import platform
import socket
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GRID_PROFILES = {
    "20mhz_mu1": (20_000_000, 1),
    "100mhz_mu1": (100_000_000, 1),
    "400mhz_mu3": (400_000_000, 3),
}

SCHEDULERS = {
    "bet": 1,
    "max_throughput": 4,
    "round_robin": 5,
    "pf": 6,
}

CHANNEL_PROFILES = ("homogeneous", "near_far")
DEMAND_PROFILES = ("full_buffer", "finite")

RF_BANDWIDTH = {
    20_000_000: 18_000_000.0,
    100_000_000: 98_280_000.0,
}


def rbg_size(frequency_rbs: int, scheduling_type: int) -> int:
    if scheduling_type != 0:
        return 1
    if frequency_rbs < 37:
        return 4
    if frequency_rbs < 73:
        return 8
    return 16


def decision_count(bandwidth: int, numerology: int, time_mode: int, frequency_mode: int) -> int:
    usable = RF_BANDWIDTH.get(bandwidth, bandwidth * 0.9)
    spacing = 15_000 * (2**numerology)
    frequency_rbs = math.floor(usable / (spacing * 12))
    frequency_groups = frequency_rbs // rbg_size(frequency_rbs, frequency_mode)
    time_groups = 2**numerology if time_mode == 0 else 1
    return frequency_groups * time_groups


def ue_section(
    name: str,
    ue_count: int,
    distance_m: int,
    target_mbps: float,
    location: str = "outdoor",
    penetration: str | None = None,
) -> str:
    penetration_line = (
        f"building_penetration: {penetration}\n"
        if penetration is not None
        else ""
    )
    return f"""[UE]
ue_id: {name}
ue_type: 1
n_ues: {ue_count}
n_antennas: 1
ul_power_mode: fixed
tx_power_ul: 23
cqi_period: 5
ri_period: 5
random_v: false
traffic_type: 0
ul_target: {target_mbps:g}
dl_target: {target_mbps:g}
var_perc: 0
pkt_size: 12000
mobility_type: 0
pos_x: {distance_m}
pos_y: 0
random_init: false
speed: 0
max_distance: {max(5000, distance_m)}
priority: 1
pkt_delay_budget: 10
ue_height: 1.5
ue_location_type: {location}
{penetration_line}"""


def config_text(
    ue_count: int,
    bandwidth: int,
    numerology: int,
    time_mode: int,
    frequency_mode: int,
    scheduler: str,
    channel: str,
    demand: str,
    reranking: str,
) -> str:
    target_mbps = (
        max(10.0, 4000.0 / ue_count)
        if demand == "full_buffer"
        else 1.0
    )
    if channel == "homogeneous":
        ue_sections = ue_section(
            "benchmarkHomogeneous", ue_count, 200, target_mbps)
    else:
        near_count = ue_count // 2
        far_count = ue_count - near_count
        ue_sections = (
            ue_section("benchmarkNear", near_count, 50, target_mbps)
            + "\n"
            + ue_section(
                "benchmarkFar",
                far_count,
                200,
                target_mbps,
                "indoor",
                "low_loss",
            )
        )

    pf_alpha = "pf_alpha: 1\n" if scheduler == "pf" else ""
    return f"""[Global]
duration: 1
period: -1
multithreading: false
threads: 0
verbose: false
progress_log_period_s: -1

{ue_sections}

[Scenario]
scenario_type: 1

[eNBConfig]
modulation_m: 1
target_ber: 0.00005
cqi_mode: 1
tx_power: 46
eNB_gain: 8.7
UT_gain: 0
frequency: 3500000000
bandwidth: {bandwidth}

[MACLayer]
metric_type: {SCHEDULERS[scheduler]}
{pf_alpha}throughput_time_window_ms: 100
throughput_intra_tti_update: {reranking}
mimo_layers: 1
n_ofdm_syms: 14
n_re_freq: 12
numerology: {numerology}
harq_model: disabled
mcs_tables: true
scheduling_mode: {time_mode}
scheduling_type: {frequency_mode}
scheduling_config: 1
duplexing_type: 0
n_dl_slots: 7
n_ul_slots: 3
transition_c: 54

[PHYLayer]
interference_ues: 0
interference_eNBs: 0
interfered_bandwidth_ratio: 0
thermal_noise: -174
enb_noise_figure: 2
ut_noise_figure: 9
"""


def run_case(
    binary: Path,
    config: Path,
    steps: int,
    warmup_steps: int,
) -> dict:
    completed = subprocess.run(
        [
            str(binary),
            str(config),
            str(steps),
            str(warmup_steps),
        ],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"return code {completed.returncode}: "
            f"{completed.stdout[-500:]}")
    for line in reversed(completed.stdout.splitlines()):
        if line.startswith("BENCHMARK "):
            return json.loads(line.removeprefix("BENCHMARK "))
    raise RuntimeError(f"benchmark output missing for {config}")


def percentile(values: list[float], probability: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = probability * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def markdown_table(rows: list[dict]) -> str:
    columns = [
        "grid",
        "ues",
        "scheduler",
        "channel",
        "demand",
        "time_mode",
        "frequency_mode",
        "reranking",
        "decisions_per_tti",
        "us_per_tti_p50",
        "us_per_tti_p95",
        "us_per_tti_p99",
        "us_per_tti_max",
        "compute_budget_exceedance_pct",
        "dl_total_mbps",
        "dl_jain",
        "dl_max_service_gap_ttis",
    ]
    lines = [
        "| " + " | ".join(columns) + " |",
        "|" + "|".join("---" for _ in columns) + "|",
    ]
    for row in rows:
        lines.append(
            "| "
            + " | ".join(str(row[column]) for column in columns)
            + " |"
        )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--steps", type=int, default=50)
    parser.add_argument("--warmup-steps", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--full", action="store_true")
    parser.add_argument(
        "--schedulers",
        default=",".join(SCHEDULERS),
        help="comma-separated scheduler names",
    )
    parser.add_argument(
        "--grids",
        default=",".join(GRID_PROFILES),
        help="comma-separated grid profile names",
    )
    parser.add_argument(
        "--channels",
        default=",".join(CHANNEL_PROFILES),
        help="comma-separated channel profiles",
    )
    parser.add_argument(
        "--demands",
        default=",".join(DEMAND_PROFILES),
        help="comma-separated demand profiles",
    )
    parser.add_argument(
        "--ue-counts",
        help="comma-separated UE counts; overrides --full",
    )
    parser.add_argument(
        "--envelope-modes",
        action="store_true",
        help="run localized/grouped and distributed/per-RB modes only",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "results" / "scheduler-family",
    )
    args = parser.parse_args()
    if args.steps <= 0 or args.repeats <= 0 or args.warmup_steps < 0:
        raise SystemExit(
            "steps/repeats must be positive and warmup-steps non-negative")

    binary = ROOT / "bin" / "scheduler_family_benchmark"
    if not binary.is_file():
        raise SystemExit(f"build benchmark first: {binary}")

    schedulers = [value for value in args.schedulers.split(",") if value]
    grids = [value for value in args.grids.split(",") if value]
    channels = [value for value in args.channels.split(",") if value]
    demands = [value for value in args.demands.split(",") if value]
    unknown_schedulers = set(schedulers) - set(SCHEDULERS)
    unknown_grids = set(grids) - set(GRID_PROFILES)
    unknown_channels = set(channels) - set(CHANNEL_PROFILES)
    unknown_demands = set(demands) - set(DEMAND_PROFILES)
    if (
        unknown_schedulers
        or unknown_grids
        or unknown_channels
        or unknown_demands
    ):
        raise SystemExit(
            "unknown matrix value: "
            f"schedulers={sorted(unknown_schedulers)}, "
            f"grids={sorted(unknown_grids)}, "
            f"channels={sorted(unknown_channels)}, "
            f"demands={sorted(unknown_demands)}"
        )

    ue_counts = (
        [int(value) for value in args.ue_counts.split(",")]
        if args.ue_counts
        else ([1, 16, 64, 256] if args.full else [16, 64])
    )
    modes = (
        [(1, 0), (0, 1)]
        if args.envelope_modes
        else list(itertools.product([1, 0], [0, 1]))
    )
    scheduler_modes = [
        (scheduler, reranking)
        for scheduler in schedulers
        for reranking in (
            ["none", "allocation_unit"]
            if scheduler in {"pf", "bet"}
            else ["none"]
        )
    ]
    output = args.output.resolve()
    config_dir = output / "configs"
    config_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    sample_rows: list[dict] = []
    failure_rows: list[dict] = []

    for (
        grid_name,
        ue_count,
        (time_mode, frequency_mode),
        channel,
        demand,
        (scheduler, reranking),
    ) in itertools.product(
        grids,
        ue_counts,
        modes,
        channels,
        demands,
        scheduler_modes,
    ):
        if channel == "near_far" and ue_count < 2:
            continue
        bandwidth, numerology = GRID_PROFILES[grid_name]
        case_name = (
            f"{grid_name}_n{ue_count}_{scheduler}_{channel}_{demand}_"
            f"t{time_mode}_f{frequency_mode}_{reranking}"
        )
        config = config_dir / f"{case_name}.ini"
        config.write_text(
            config_text(
                ue_count,
                bandwidth,
                numerology,
                time_mode,
                frequency_mode,
                scheduler,
                channel,
                demand,
                reranking,
            )
        )
        samples = []
        failure = None
        for repeat in range(args.repeats):
            try:
                samples.append(
                    run_case(
                        binary,
                        config,
                        args.steps,
                        args.warmup_steps,
                    )
                )
            except RuntimeError as error:
                failure = {
                    "case": case_name,
                    "repeat": repeat,
                    "error": str(error),
                }
                failure_rows.append(failure)
                break
        if failure is not None:
            print(case_name, "FAILED", failure["error"])
            continue
        for repeat, sample in enumerate(samples):
            for tti, elapsed_us in enumerate(sample["tti_us"]):
                sample_rows.append(
                    {
                        "case": case_name,
                        "repeat": repeat,
                        "tti": tti,
                        "warmup_steps": args.warmup_steps,
                        "us_per_tti": elapsed_us,
                    }
                )
        representative = samples[0]
        runtime_samples = [
            elapsed_us
            for sample in samples
            for elapsed_us in sample["tti_us"]
        ]
        row = {
            "grid": grid_name,
            "ues": ue_count,
            "scheduler": scheduler,
            "channel": channel,
            "demand": demand,
            "time_mode": "localized" if time_mode == 1 else "distributed",
            "frequency_mode": "grouped" if frequency_mode == 0 else "per_rb",
            "reranking": reranking,
            "decisions_per_tti": decision_count(
                bandwidth, numerology, time_mode, frequency_mode
            ),
            "us_per_tti": round(
                statistics.median(runtime_samples), 3
            ),
            "us_per_tti_p50": round(
                percentile(runtime_samples, 0.50), 3),
            "us_per_tti_p95": round(
                percentile(runtime_samples, 0.95), 3),
            "us_per_tti_p99": round(
                percentile(runtime_samples, 0.99), 3),
            "us_per_tti_max": round(max(runtime_samples), 3),
            "compute_budget_exceedance_pct": round(
                100.0
                * sum(value > 1000.0 for value in runtime_samples)
                / len(runtime_samples),
                3,
            ),
            "dl_total_mbps": round(representative["dl_total_mbps"], 3),
            "ul_total_mbps": round(representative["ul_total_mbps"], 3),
            "dl_jain": round(representative["dl_jain"], 5),
            "ul_jain": round(representative["ul_jain"], 5),
            "dl_max_service_gap_ttis": representative[
                "dl_max_service_gap_ttis"
            ],
            "ul_max_service_gap_ttis": representative[
                "ul_max_service_gap_ttis"
            ],
        }
        rows.append(row)
        print(case_name, row["us_per_tti"])

    csv_path = output / "results.csv"
    if rows:
        with csv_path.open("w", newline="") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
    else:
        csv_path.write_text("")
    samples_path = output / "runtime-samples.csv"
    if sample_rows:
        with samples_path.open("w", newline="") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=list(sample_rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(sample_rows)
    else:
        samples_path.write_text("")
    failures_path = output / "failures.csv"
    if failure_rows:
        with failures_path.open("w", newline="") as handle:
            writer = csv.DictWriter(
                handle, fieldnames=list(failure_rows[0]), lineterminator="\n")
            writer.writeheader()
            writer.writerows(failure_rows)
    else:
        failures_path.write_text("")

    report = [
        "# Throughput Scheduler Family Benchmark",
        "",
        f"Warm-up steps: {args.warmup_steps}; measured steps per sample: "
        f"{args.steps}; repeats: {args.repeats}.",
        "",
        "P50/P95/P99/max are computed from individually timed warmed-up TTIs "
        "across all repeats. A compute-budget exceedance is a TTI above "
        "1,000 us.",
        "",
        f"Host: `{socket.gethostname()}`; platform: `{platform.platform()}`.",
        "",
        "Time modes: `localized` uses one time allocation group per 1 ms; "
        "`distributed` uses one group per numerology slot.",
        "",
        "Frequency modes: `grouped` uses configured RBGs; `per_rb` uses one "
        "PRB per allocation unit.",
        "",
        "Full-buffer cases offer approximately 4 Gbit/s in each direction "
        "per case, divided equally across UEs. Finite-demand cases offer "
        "1 Mbit/s per UE and direction.",
        "",
        "Homogeneous cases place every UE outdoors at 200 m. Near/far "
        "cases split UEs between 50 m outdoor and 200 m low-loss indoor "
        "conditions so both groups remain eligible while their rates differ.",
        "",
        markdown_table(rows),
        "",
        (
            f"Failed cases: {len(failure_rows)}. See `failures.csv`."
            if failure_rows
            else "Failed cases: 0."
        ),
        "",
    ]
    (output / "report.md").write_text("\n".join(report))
    source_sha = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    source_dirty = bool(
        subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=ROOT, text=True
        ).strip()
    )
    metadata = {
        "schema_version": 1,
        "source_sha": source_sha,
        "source_dirty": source_dirty,
        "host": {
            "hostname": socket.gethostname(),
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "steps": args.steps,
        "warmup_steps": args.warmup_steps,
        "repeats": args.repeats,
        "timing_samples_per_case": args.steps * args.repeats,
        "timing_unit": "individual warmed-up TTI",
        "ue_counts": ue_counts,
        "modes": modes,
        "selected_grids": grids,
        "schedulers": schedulers,
        "channels": channels,
        "demands": demands,
        "grids": GRID_PROFILES,
        "failed_cases": len(failure_rows),
    }
    (output / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    print(output)


if __name__ == "__main__":
    main()
