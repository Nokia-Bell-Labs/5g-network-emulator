#!/usr/bin/env python3
"""Run the canonical disabled, no-retry, and production HARQ campaign."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    "offline_umi_n40_npn",
    "offline_uma_n78_pedestrian",
    "offline_rural_n78_vehicular",
    "offline_indoor_hotspot_n78_pedestrian",
    "offline_umi_n258_fwa",
    "offline_umi_n258_fwa_high_loss",
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=20260927)
    parser.add_argument("--duration-s", type=int, default=180)
    parser.add_argument(
        "--batch-id-prefix",
        default="harq",
        help="prefix for unique run IDs and log directories",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "results" / "harq-campaign",
    )
    args = parser.parse_args()
    if args.duration_s <= 0:
        raise SystemExit("--duration-s must be positive")

    profile_list = ",".join(PROFILES)
    cases = (
        ("disabled", "disabled", 4),
        ("legacy-no-retry", "legacy_bler", 0),
        ("legacy-production", "legacy_bler", 4),
    )
    for case_name, model, max_rtx in cases:
        command = [
            "python3",
            "tools/run_phy_v2_profiles.py",
            "--seed",
            str(args.seed),
            "--duration-s",
            str(args.duration_s),
            "--profiles",
            profile_list,
            "--harq-model",
            model,
            "--max-rtx",
            str(max_rtx),
            "--batch-id",
            f"{args.batch_id_prefix}-{case_name}-seed{args.seed}",
            "--output",
            str((args.output / case_name).resolve()),
        ]
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
