#!/usr/bin/env python3
"""Generate publication figures from committed PHY Model V2 summaries."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

ROOT = Path(__file__).resolve().parents[1]
BASELINES = ROOT / "docs" / "baselines"
FIGURES = ROOT / "docs" / "figures"

PROFILE_LABELS = {
    "offline_indoor_hotspot_n78_pedestrian": "Indoor n78",
    "offline_rural_n78_vehicular": "RMa n78",
    "offline_uma_n78_pedestrian": "UMa n78",
    "offline_umi_n258_fwa": "UMi n258",
    "offline_umi_n40_npn": "UMi n40",
}
GRID_LABELS = {
    "20mhz_mu1": "20 MHz, μ=1",
    "100mhz_mu1": "100 MHz, μ=1",
    "400mhz_mu3": "400 MHz, μ=3",
}
COLORS = {
    "20mhz_mu1": "#0072B2",
    "100mhz_mu1": "#E69F00",
    "400mhz_mu3": "#009E73",
}


def rows(path: Path) -> list[dict[str, str]]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


def save(fig: plt.Figure, name: str) -> None:
    FIGURES.mkdir(parents=True, exist_ok=True)
    path = FIGURES / name
    fig.savefig(
        path,
        format="svg",
        bbox_inches="tight",
        metadata={"Date": None},
    )
    plt.close(fig)
    path.write_text(
        "\n".join(line.rstrip() for line in path.read_text().splitlines())
        + "\n"
    )


def throughput_figure() -> None:
    data = rows(BASELINES / "phy-v2-production-comparison.csv")
    labels = [
        f"{PROFILE_LABELS[row['profile']]} {row['direction'].upper()}"
        for row in data
    ]
    baseline = [float(row["baseline_throughput_mbps"]) for row in data]
    current = [float(row["current_throughput_mbps"]) for row in data]
    positions = range(len(data))
    fig, ax = plt.subplots(figsize=(10.5, 4.8))
    width = 0.38
    ax.bar(
        [value - width / 2 for value in positions],
        baseline,
        width,
        label="Original baseline",
        color="#999999",
    )
    ax.bar(
        [value + width / 2 for value in positions],
        current,
        width,
        label="PHY Model V2",
        color="#0072B2",
    )
    ax.set_ylabel("Delivered throughput (Mbit/s)")
    ax.set_xticks(list(positions), labels, rotation=38, ha="right")
    ax.set_title("Unpaired historical baseline and V2 runs")
    ax.legend(frameon=False)
    ax.grid(axis="y", alpha=0.25)
    save(fig, "phy-v2-throughput.svg")


def los_figure() -> None:
    data = rows(BASELINES / "map-v2-multiseed-los-radial.csv")
    selections = [
        ("URBAN_MICROCELL", "2.38", "UMi 2.38 GHz"),
        ("URBAN_MACROCELL", "3.5", "UMa 3.5 GHz"),
        ("RURAL_MACROCELL", "3.5", "RMa 3.5 GHz"),
        ("INDOOR_OPEN_OFFICE", "3.5", "Indoor open office 3.5 GHz"),
    ]
    fig, axes = plt.subplots(2, 2, figsize=(9.5, 6.8))
    for ax, (scenario, frequency, title) in zip(axes.flat, selections):
        selected = [
            row
            for row in data
            if row["scenario"] == scenario
            and float(row["frequency_ghz"]) == float(frequency)
        ]
        selected.sort(key=lambda row: int(row["radial_bin"]))
        distance = [
            0.5
            * (float(row["radius_low_m"]) + float(row["radius_high_m"]))
            for row in selected
        ]
        observed = [
            float(row["observed_los_probability"]) for row in selected]
        lower = [float(row["observed_ci_low"]) for row in selected]
        upper = [float(row["observed_ci_high"]) for row in selected]
        target = [
            float(row["target_los_probability"]) for row in selected]
        ax.fill_between(
            distance, lower, upper, color="#56B4E9", alpha=0.3,
            label="95% CI")
        ax.plot(
            distance, observed, marker="o", color="#0072B2",
            label="30-seed mean")
        ax.plot(
            distance, target, linestyle="--", color="#D55E00",
            label="Declared marginal")
        ax.set_title(title)
        ax.set_xlabel("Radial distance (m)")
        ax.set_ylabel("LOS probability")
        ax.set_ylim(-0.03, 1.03)
        ax.grid(alpha=0.25)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=3, frameon=False)
    fig.suptitle("V2 LOS-state ensemble validation", y=1.02)
    fig.tight_layout()
    save(fig, "phy-v2-map-los.svg")


def runtime_figure() -> None:
    data = rows(BASELINES / "pf-runtime-envelope.csv")
    fig, axes = plt.subplots(1, 2, figsize=(11.0, 4.5), sharey=True)
    mode_filters = [
        ("grouped", "Localized, grouped RBG"),
        ("per_rb", "Distributed, per-PRB"),
    ]
    for ax, (frequency_mode, title) in zip(axes, mode_filters):
        for grid in GRID_LABELS:
            for reranking, linestyle in (
                ("none", "--"),
                ("allocation_unit", "-"),
            ):
                selected = [
                    row
                    for row in data
                    if row["grid"] == grid
                    and row["frequency_mode"] == frequency_mode
                    and row["reranking"] == reranking
                ]
                selected.sort(key=lambda row: int(row["ues"]))
                ax.plot(
                    [int(row["ues"]) for row in selected],
                    [float(row["us_per_tti_p99"]) for row in selected],
                    marker="o",
                    linestyle=linestyle,
                    color=COLORS[grid],
                    label=(
                        f"{GRID_LABELS[grid]}, "
                        f"{'rerank' if reranking == 'allocation_unit' else 'none'}"
                    ),
                )
        ax.axhline(
            1000.0, color="#CC3311", linewidth=1.2, label="1 ms budget")
        ax.set_yscale("log")
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 16, 64, 256], ["1", "16", "64", "256"])
        ax.set_xlabel("UEs")
        ax.set_title(title)
        ax.grid(which="both", alpha=0.25)
    axes[0].set_ylabel("P99 execution time (μs/TTI)")
    handles, labels = axes[1].get_legend_handles_labels()
    fig.legend(
        handles, labels, loc="upper center", ncol=4, frameon=False,
        fontsize=8)
    fig.suptitle("Measured scheduler runtime envelope", y=1.08)
    fig.tight_layout()
    save(fig, "phy-v2-runtime.svg")


def reranking_figure() -> None:
    data = [
        row
        for row in rows(BASELINES / "pf-functional-long.csv")
        if int(row["ues"]) == 64
    ]
    categories = []
    none_by_category = {}
    rerank_by_category = {}
    for row in data:
        category = (
            row["grid"],
            row["frequency_mode"],
        )
        if category not in categories:
            categories.append(category)
        destination = (
            rerank_by_category
            if row["reranking"] == "allocation_unit"
            else none_by_category
        )
        destination[category] = row
    labels = [
        f"{GRID_LABELS[grid]}\n{'grouped' if mode == 'grouped' else 'per-PRB'}"
        for grid, mode in categories
    ]
    x_values = list(range(len(categories)))
    width = 0.38
    fig, axes = plt.subplots(2, 1, figsize=(10.5, 6.2), sharex=True)
    axes[0].bar(
        [value - width / 2 for value in x_values],
        [float(none_by_category[key]["dl_jain"]) for key in categories],
        width,
        color="#999999",
        label="No intra-TTI reranking",
    )
    axes[0].bar(
        [value + width / 2 for value in x_values],
        [float(rerank_by_category[key]["dl_jain"]) for key in categories],
        width,
        color="#009E73",
        label="Allocation-unit reranking",
    )
    axes[0].set_ylabel("DL Jain index")
    axes[0].set_ylim(0, 1.05)
    axes[0].legend(frameon=False, ncol=2)
    axes[0].grid(axis="y", alpha=0.25)
    axes[1].bar(
        [value - width / 2 for value in x_values],
        [
            float(none_by_category[key]["dl_max_service_gap_ttis"])
            for key in categories
        ],
        width,
        color="#999999",
    )
    axes[1].bar(
        [value + width / 2 for value in x_values],
        [
            float(rerank_by_category[key]["dl_max_service_gap_ttis"])
            for key in categories
        ],
        width,
        color="#009E73",
    )
    axes[1].set_ylabel("Maximum DL service gap (TTIs)")
    axes[1].set_xticks(x_values, labels)
    axes[1].grid(axis="y", alpha=0.25)
    fig.suptitle("Reranking effect at 64 homogeneous UEs")
    fig.tight_layout()
    save(fig, "phy-v2-reranking.svg")


def pipeline_figure() -> None:
    stages = [
        "Position,\nscenario, seed",
        "V2 map\n+ O2I/vehicle",
        "Per-PRB power,\nnoise, interference",
        "Nominal SINR,\nMCS, rank",
        "PF planning\n+ reranking",
        "UL final power\n+ rate recompute",
        "Grant,\npacket/queue",
        "Effective bits\n+ TTI EWMA",
    ]
    fig, ax = plt.subplots(figsize=(13.5, 2.5))
    ax.set_xlim(0, len(stages))
    ax.set_ylim(0, 1)
    ax.axis("off")
    for index, stage in enumerate(stages):
        x_position = index + 0.08
        box = FancyBboxPatch(
            (x_position, 0.30),
            0.84,
            0.40,
            boxstyle="round,pad=0.03",
            facecolor="#E6F2F8",
            edgecolor="#0072B2",
            linewidth=1.3,
        )
        ax.add_patch(box)
        ax.text(
            x_position + 0.42,
            0.50,
            stage,
            ha="center",
            va="center",
            fontsize=9,
        )
        if index < len(stages) - 1:
            ax.annotate(
                "",
                xy=(index + 1.06, 0.50),
                xytext=(index + 0.94, 0.50),
                arrowprops={"arrowstyle": "->", "color": "#333333"},
            )
    ax.set_title("FikoRE resource-consistent PHY/MAC abstraction")
    save(fig, "phy-v2-pipeline.svg")


def main() -> None:
    matplotlib.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 9,
            "axes.titlesize": 11,
            "axes.labelsize": 9,
            "legend.fontsize": 8,
            "svg.fonttype": "none",
            "svg.hashsalt": "fikore-phy-v2",
        }
    )
    throughput_figure()
    los_figure()
    runtime_figure()
    reranking_figure()
    pipeline_figure()
    print(FIGURES)


if __name__ == "__main__":
    main()
