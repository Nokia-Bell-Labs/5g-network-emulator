#!/usr/bin/env python3
"""Validate v2 map statistics across independent deterministic realizations."""

from __future__ import annotations

import argparse
import csv
import json
import math
import platform
import socket
import statistics
import subprocess
import time
from collections import defaultdict
from pathlib import Path

import numpy as np

from generate_v2_catalog import CATALOG, derived_seed
from generator_v2 import (
    GENERATOR_NAME,
    GENERATOR_VERSION,
    MAP_SEMANTIC_VERSION,
    SCENARIO_PARAMETERS,
    generate_components,
)

ROOT = Path(__file__).resolve().parents[2]
T_CRITICAL_95 = 2.045229642132703  # 29 degrees of freedom.
RADIAL_BIN_COUNT = 10

# A deliberately simple map-only proxy. It uses total carrier power and
# full-channel thermal noise, excludes O2I/fading/interference, and reports the
# fraction of map cells with pre-interference downlink SNR >= 0 dB.
CANONICAL_DL_PROXIES = {
    ("URBAN_MICROCELL", 2.38): {
        "profile": "umi-n40-npn",
        "tx_power_dbm": 43.0,
        "tx_gain_dbi": 8.7,
        "rx_gain_dbi": 0.0,
        "noise_figure_db": 9.0,
        "bandwidth_hz": 20_000_000,
    },
    ("URBAN_MACROCELL", 3.5): {
        "profile": "uma-n78-pedestrian",
        "tx_power_dbm": 46.0,
        "tx_gain_dbi": 8.7,
        "rx_gain_dbi": 0.0,
        "noise_figure_db": 9.0,
        "bandwidth_hz": 100_000_000,
    },
    ("RURAL_MACROCELL", 3.5): {
        "profile": "rma-n78-vehicular",
        "tx_power_dbm": 46.0,
        "tx_gain_dbi": 8.7,
        "rx_gain_dbi": 0.0,
        "noise_figure_db": 9.0,
        "bandwidth_hz": 100_000_000,
    },
    ("INDOOR_OPEN_OFFICE", 3.5): {
        "profile": "indoor-n78-pedestrian",
        "tx_power_dbm": 24.0,
        "tx_gain_dbi": 8.7,
        "rx_gain_dbi": 0.0,
        "noise_figure_db": 9.0,
        "bandwidth_hz": 100_000_000,
    },
    ("URBAN_MICROCELL", 26.0): {
        "profile": "umi-n258-fwa",
        "tx_power_dbm": 35.0,
        "tx_gain_dbi": 24.0,
        "rx_gain_dbi": 26.0,
        "noise_figure_db": 10.0,
        "bandwidth_hz": 400_000_000,
    },
}


def ci95(values: list[float]) -> tuple[float, float, float]:
    mean = statistics.fmean(values)
    if len(values) < 2:
        return mean, mean, mean
    critical = T_CRITICAL_95 if len(values) == 30 else 1.96
    half_width = (
        critical
        * statistics.stdev(values)
        / math.sqrt(len(values))
    )
    return mean, mean - half_width, mean + half_width


def axial_autocorrelation(field: np.ndarray, lag_cells: int) -> float:
    if lag_cells <= 0 or lag_cells >= field.shape[0]:
        raise ValueError(f"invalid autocorrelation lag {lag_cells}")
    horizontal_a = field[:, :-lag_cells].ravel()
    horizontal_b = field[:, lag_cells:].ravel()
    vertical_a = field[:-lag_cells, :].ravel()
    vertical_b = field[lag_cells:, :].ravel()
    left = np.concatenate((horizontal_a, vertical_a))
    right = np.concatenate((horizontal_b, vertical_b))
    return float(np.corrcoef(left, right)[0, 1])


def opposite_edge_correlation(field: np.ndarray) -> float:
    first = np.concatenate((field[:, 0], field[0, :]))
    opposite = np.concatenate((field[:, -1], field[-1, :]))
    return float(np.corrcoef(first, opposite)[0, 1])


def coverage_proxy(
    scenario: str,
    frequency_ghz: float,
    link_gain_db: np.ndarray,
) -> float:
    profile = CANONICAL_DL_PROXIES.get((scenario, frequency_ghz))
    if profile is None:
        return math.nan
    noise_dbm = (
        -174.0
        + profile["noise_figure_db"]
        + 10.0 * math.log10(profile["bandwidth_hz"])
    )
    snr_db = (
        profile["tx_power_dbm"]
        + profile["tx_gain_dbi"]
        + profile["rx_gain_dbi"]
        + link_gain_db
        - noise_dbm
    )
    return float(np.mean(snr_db >= 0.0))


def current_source() -> dict:
    def git(*args: str) -> str:
        return subprocess.check_output(
            ["git", *args], cwd=ROOT, text=True).strip()

    return {
        "sha": git("rev-parse", "HEAD"),
        "dirty": bool(git("status", "--porcelain")),
    }


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def markdown_report(
    realization_count: int,
    seed_base: int,
    elapsed_seconds: float,
    map_rows: list[dict],
    radial_rows: list[dict],
) -> str:
    lines = [
        "# V2 Map Multi-Seed Validation",
        "",
        "## Method",
        "",
        f"- Independent master seeds: {realization_count}",
        f"- Master-seed sequence: `{seed_base}` through "
        f"`{seed_base + realization_count - 1}`",
        f"- Catalog entries per seed: {len(map_rows)}",
        f"- Runtime: {elapsed_seconds:.2f} seconds",
        "- Confidence intervals: two-sided 95% intervals over independent "
        "realizations; intervals are pointwise, not simultaneous, and spatial "
        "cells are not treated as replicates.",
        "- Coverage proxy: fraction of map cells with map-only, "
        "pre-interference full-channel DL SNR >= 0 dB.",
        "- Radial LOS bins cover the inscribed disk out to the map apothem; "
        "corner cells are excluded from radial diagnostics.",
        "",
        "## Per-map summary",
        "",
        "| Scenario | GHz | Link gain P50 (dB) | LOS radial RMSE | "
        "LOS shadow std (dB) | NLOS shadow std (dB) | "
        "LOS/NLOS correlation observed/target | Coverage proxy |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in map_rows:
        coverage = (
            "n/a"
            if row["coverage_proxy_mean"] == ""
            else f"{100.0 * float(row['coverage_proxy_mean']):.1f}%"
        )
        lines.append(
            f"| {row['scenario']} | {row['frequency_ghz']} | "
            f"{float(row['link_gain_p50_mean_db']):.2f} "
            f"[{float(row['link_gain_p50_ci_low_db']):.2f}, "
            f"{float(row['link_gain_p50_ci_high_db']):.2f}] | "
            f"{float(row['los_radial_rmse_mean']):.3f} | "
            f"{float(row['los_shadow_std_mean_db']):.2f} | "
            f"{float(row['nlos_shadow_std_mean_db']):.2f} | "
            f"{float(row['los_autocorr_mean']):.3f}/"
            f"{float(row['los_autocorr_target']):.3f}; "
            f"{float(row['nlos_autocorr_mean']):.3f}/"
            f"{float(row['nlos_autocorr_target']):.3f} | {coverage} |"
        )

    max_radial_bias = max(
        abs(float(row["bias"])) for row in radial_rows)
    max_shadow_std_error = max(
        max(
            abs(
                float(row["los_shadow_std_mean_db"])
                - float(row["los_shadow_std_target_db"])
            ),
            abs(
                float(row["nlos_shadow_std_mean_db"])
                - float(row["nlos_shadow_std_target_db"])
            ),
        )
        for row in map_rows
    )
    max_autocorr_error = max(
        max(
            abs(
                float(row["los_autocorr_mean"])
                - float(row["los_autocorr_target"])
            ),
            abs(
                float(row["nlos_autocorr_mean"])
                - float(row["nlos_autocorr_target"])
            ),
        )
        for row in map_rows
    )
    max_edge_correlation = max(
        max(
            abs(float(row["los_opposite_edge_correlation_mean"])),
            abs(float(row["nlos_opposite_edge_correlation_mean"])),
        )
        for row in map_rows
    )
    lines.extend(
        [
            "",
            "## Checks and interpretation",
            "",
            f"- Maximum absolute bias of the ensemble-mean radial LOS "
            f"probability: "
            f"{max_radial_bias:.3f}.",
            f"- Maximum absolute ensemble shadow-standard-deviation error: "
            f"{max_shadow_std_error:.3f} dB.",
            f"- Maximum absolute error of the ensemble-mean axial "
            f"autocorrelation at the nearest grid-representable lag: "
            f"{max_autocorr_error:.3f}.",
            f"- Maximum absolute ensemble-mean opposite-edge shadow "
            f"correlation: "
            f"{max_edge_correlation:.3f}; this diagnoses circular FFT seams "
            "and is not an acceptance pass.",
            "- Autocorrelation targets use `exp(-lag/d_cor)` at the reported "
            "integer-cell lag; they are not incorrectly compared with "
            "`exp(-1)` when the declared distance falls between cells.",
            "- Link-gain intervals quantify realization variability for the "
            "generator. They are not confidence intervals for field "
            "prediction error.",
            "- The proxy excludes penetration, small-scale fading, external "
            "interference, mobility, MIMO, scheduling, HARQ, and traffic.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--realizations", type=int, default=30)
    parser.add_argument("--master-seed-base", type=int, default=20270000)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "results" / "map-v2-multiseed",
    )
    args = parser.parse_args()
    if args.realizations < 2:
        raise SystemExit("--realizations must be at least 2")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    realization_rows: list[dict] = []
    radial_realizations: dict[tuple, list[tuple[float, float]]] = defaultdict(list)

    for scenario in sorted(CATALOG):
        parameters = SCENARIO_PARAMETERS[scenario]
        for frequency_ghz in CATALOG[scenario]:
            for realization_index in range(args.realizations):
                master_seed = args.master_seed_base + realization_index
                seed = derived_seed(master_seed, scenario, frequency_ghz)
                components = generate_components(
                    scenario, frequency_ghz, seed)
                link_gain = components.link_gain_db
                los_lag = max(
                    1,
                    round(
                        parameters.los_correlation_m
                        / components.cell_size_m
                    ),
                )
                nlos_lag = max(
                    1,
                    round(
                        parameters.nlos_correlation_m
                        / components.cell_size_m
                    ),
                )
                link_quantiles = np.quantile(
                    link_gain, [0.05, 0.5, 0.95])

                radius_limit = (
                    ((link_gain.shape[0] - 1) / 2.0)
                    * components.cell_size_m
                )
                radial_edges = np.linspace(
                    0.0, radius_limit, RADIAL_BIN_COUNT + 1)
                squared_errors = []
                for bin_index, (low, high) in enumerate(
                    zip(radial_edges[:-1], radial_edges[1:])
                ):
                    mask = (
                        (components.distance_m >= low)
                        & (
                            (components.distance_m < high)
                            if bin_index < RADIAL_BIN_COUNT - 1
                            else (components.distance_m <= high)
                        )
                    )
                    observed = float(np.mean(components.los_state[mask]))
                    target = float(
                        np.mean(components.los_probability[mask]))
                    radial_realizations[
                        (
                            scenario,
                            frequency_ghz,
                            bin_index,
                            low,
                            high,
                        )
                    ].append((observed, target))
                    squared_errors.append((observed - target) ** 2)

                realization_rows.append(
                    {
                        "scenario": scenario,
                        "frequency_ghz": frequency_ghz,
                        "realization_index": realization_index,
                        "master_seed": master_seed,
                        "derived_seed": seed,
                        "link_gain_p05_db": float(link_quantiles[0]),
                        "link_gain_p50_db": float(link_quantiles[1]),
                        "link_gain_p95_db": float(link_quantiles[2]),
                        "los_fraction": float(np.mean(components.los_state)),
                        "los_radial_rmse": math.sqrt(
                            statistics.fmean(squared_errors)),
                        "los_shadow_mean_db": float(
                            np.mean(components.los_shadow_db)),
                        "los_shadow_std_db": float(
                            np.std(components.los_shadow_db)),
                        "nlos_shadow_mean_db": float(
                            np.mean(components.nlos_shadow_db)),
                        "nlos_shadow_std_db": float(
                            np.std(components.nlos_shadow_db)),
                        "los_autocorr_at_declared_distance":
                            axial_autocorrelation(
                                components.los_shadow_db, los_lag),
                        "nlos_autocorr_at_declared_distance":
                            axial_autocorrelation(
                                components.nlos_shadow_db, nlos_lag),
                        "los_autocorr_lag_m":
                            los_lag * components.cell_size_m,
                        "nlos_autocorr_lag_m":
                            nlos_lag * components.cell_size_m,
                        "los_autocorr_target": math.exp(
                            -los_lag
                            * components.cell_size_m
                            / parameters.los_correlation_m
                        ),
                        "nlos_autocorr_target": math.exp(
                            -nlos_lag
                            * components.cell_size_m
                            / parameters.nlos_correlation_m
                        ),
                        "los_opposite_edge_correlation":
                            opposite_edge_correlation(
                                components.los_shadow_db),
                        "nlos_opposite_edge_correlation":
                            opposite_edge_correlation(
                                components.nlos_shadow_db),
                        "coverage_proxy_0db": coverage_proxy(
                            scenario, frequency_ghz, link_gain),
                    }
                )

    grouped: dict[tuple[str, float], list[dict]] = defaultdict(list)
    for row in realization_rows:
        grouped[(row["scenario"], row["frequency_ghz"])].append(row)

    map_rows = []
    for (scenario, frequency_ghz), rows in sorted(grouped.items()):
        parameters = SCENARIO_PARAMETERS[scenario]

        def interval(key: str) -> tuple[float, float, float]:
            return ci95([float(row[key]) for row in rows])

        p05 = interval("link_gain_p05_db")
        p50 = interval("link_gain_p50_db")
        p95 = interval("link_gain_p95_db")
        radial_rmse = interval("los_radial_rmse")
        los_mean = interval("los_shadow_mean_db")
        los_std = interval("los_shadow_std_db")
        nlos_mean = interval("nlos_shadow_mean_db")
        nlos_std = interval("nlos_shadow_std_db")
        los_corr = interval("los_autocorr_at_declared_distance")
        nlos_corr = interval("nlos_autocorr_at_declared_distance")
        los_edge_corr = interval("los_opposite_edge_correlation")
        nlos_edge_corr = interval("nlos_opposite_edge_correlation")
        coverage_values = [
            float(row["coverage_proxy_0db"])
            for row in rows
            if math.isfinite(float(row["coverage_proxy_0db"]))
        ]
        coverage = (
            ci95(coverage_values)
            if coverage_values
            else ("", "", "")
        )
        map_rows.append(
            {
                "scenario": scenario,
                "frequency_ghz": frequency_ghz,
                "realizations": len(rows),
                "link_gain_p05_mean_db": p05[0],
                "link_gain_p05_ci_low_db": p05[1],
                "link_gain_p05_ci_high_db": p05[2],
                "link_gain_p50_mean_db": p50[0],
                "link_gain_p50_ci_low_db": p50[1],
                "link_gain_p50_ci_high_db": p50[2],
                "link_gain_p95_mean_db": p95[0],
                "link_gain_p95_ci_low_db": p95[1],
                "link_gain_p95_ci_high_db": p95[2],
                "los_radial_rmse_mean": radial_rmse[0],
                "los_radial_rmse_ci_low": radial_rmse[1],
                "los_radial_rmse_ci_high": radial_rmse[2],
                "los_shadow_mean_db": los_mean[0],
                "los_shadow_mean_ci_low_db": los_mean[1],
                "los_shadow_mean_ci_high_db": los_mean[2],
                "los_shadow_std_mean_db": los_std[0],
                "los_shadow_std_ci_low_db": los_std[1],
                "los_shadow_std_ci_high_db": los_std[2],
                "los_shadow_std_target_db": parameters.los_sigma_db,
                "nlos_shadow_mean_db": nlos_mean[0],
                "nlos_shadow_mean_ci_low_db": nlos_mean[1],
                "nlos_shadow_mean_ci_high_db": nlos_mean[2],
                "nlos_shadow_std_mean_db": nlos_std[0],
                "nlos_shadow_std_ci_low_db": nlos_std[1],
                "nlos_shadow_std_ci_high_db": nlos_std[2],
                "nlos_shadow_std_target_db": parameters.nlos_sigma_db,
                "los_autocorr_mean": los_corr[0],
                "los_autocorr_ci_low": los_corr[1],
                "los_autocorr_ci_high": los_corr[2],
                "nlos_autocorr_mean": nlos_corr[0],
                "nlos_autocorr_ci_low": nlos_corr[1],
                "nlos_autocorr_ci_high": nlos_corr[2],
                "los_autocorr_lag_m": rows[0]["los_autocorr_lag_m"],
                "los_autocorr_target": rows[0]["los_autocorr_target"],
                "nlos_autocorr_lag_m": rows[0]["nlos_autocorr_lag_m"],
                "nlos_autocorr_target": rows[0]["nlos_autocorr_target"],
                "los_opposite_edge_correlation_mean": los_edge_corr[0],
                "los_opposite_edge_correlation_ci_low": los_edge_corr[1],
                "los_opposite_edge_correlation_ci_high": los_edge_corr[2],
                "nlos_opposite_edge_correlation_mean": nlos_edge_corr[0],
                "nlos_opposite_edge_correlation_ci_low": nlos_edge_corr[1],
                "nlos_opposite_edge_correlation_ci_high": nlos_edge_corr[2],
                "coverage_proxy_mean": coverage[0],
                "coverage_proxy_ci_low": coverage[1],
                "coverage_proxy_ci_high": coverage[2],
            }
        )

    radial_rows = []
    for key, samples in sorted(radial_realizations.items()):
        scenario, frequency_ghz, bin_index, low, high = key
        observed = ci95([sample[0] for sample in samples])
        target = statistics.fmean(sample[1] for sample in samples)
        radial_rows.append(
            {
                "scenario": scenario,
                "frequency_ghz": frequency_ghz,
                "radial_bin": bin_index,
                "radius_low_m": low,
                "radius_high_m": high,
                "realizations": len(samples),
                "observed_los_probability": observed[0],
                "observed_ci_low": observed[1],
                "observed_ci_high": observed[2],
                "target_los_probability": target,
                "bias": observed[0] - target,
            }
        )

    elapsed = time.perf_counter() - started
    write_csv(output / "realizations.csv", realization_rows)
    write_csv(output / "summary.csv", map_rows)
    write_csv(output / "los_radial.csv", radial_rows)
    (output / "report.md").write_text(
        markdown_report(
            args.realizations,
            args.master_seed_base,
            elapsed,
            map_rows,
            radial_rows,
        )
    )
    metadata = {
        "schema_version": 1,
        "source": current_source(),
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
            "map_semantic_version": MAP_SEMANTIC_VERSION,
        },
        "host": {
            "hostname": socket.gethostname(),
            "platform": platform.platform(),
            "python": platform.python_version(),
            "numpy": np.__version__,
        },
        "realizations": args.realizations,
        "master_seed_base": args.master_seed_base,
        "catalog_entries": sum(len(values) for values in CATALOG.values()),
        "elapsed_seconds": elapsed,
        "confidence_unit": "independent map realization",
    }
    (output / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    print(output)


if __name__ == "__main__":
    main()
