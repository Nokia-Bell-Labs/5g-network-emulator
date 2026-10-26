#!/usr/bin/env python3
"""Run the five deterministic PHY Model V2 reference profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    "offline_umi_n40_npn",
    "offline_uma_n78_pedestrian",
    "offline_rural_n78_vehicular",
    "offline_indoor_hotspot_n78_pedestrian",
    "offline_umi_n258_fwa",
)
EXPECTED_MAPS = {
    "offline_umi_n40_npn":
        "macroscopic_fading_map_URBAN_MICROCELL_2.38.json",
    "offline_uma_n78_pedestrian":
        "macroscopic_fading_map_URBAN_MACROCELL_3.5.json",
    "offline_rural_n78_vehicular":
        "macroscopic_fading_map_RURAL_MACROCELL_3.5.json",
    "offline_indoor_hotspot_n78_pedestrian":
        "macroscopic_fading_map_INDOOR_OPEN_OFFICE_3.5.json",
    "offline_umi_n258_fwa":
        "macroscopic_fading_map_URBAN_MICROCELL_26.json",
    "offline_umi_n258_fwa_high_loss":
        "macroscopic_fading_map_URBAN_MICROCELL_26.json",
}
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    return subprocess.check_output(
        ["git", *args], cwd=ROOT, text=True).strip()


def render_config(
    source: Path,
    seed: int,
    run_id: str,
    duration_s: int,
    map_file: Path | None = None,
    harq_model: str | None = None,
    max_rtx: int | None = None,
) -> str:
    lines = source.read_text().splitlines()
    rendered = []
    inserted = False
    for line in lines:
        rendered.append(line)
        if line.strip() == "[Global]":
            rendered.extend(
                [
                    f"seed: {seed}",
                    f"run_id: {run_id}",
                    "progress_log_period_s: -1",
                ]
            )
            inserted = True
        elif line.strip() == "[Scenario]" and map_file is not None:
            rendered.append(f"map_file: {map_file}")
        elif line.strip().startswith("duration:"):
            rendered[-1] = f"duration: {duration_s}"
        elif (
            harq_model is not None
            and line.strip().startswith("harq_model:")
        ):
            rendered[-1] = f"harq_model: {harq_model}"
        elif (
            max_rtx is not None
            and line.strip().startswith(("max_rtx_ul:", "max_rtx_dl:"))
        ):
            key = line.split(":", 1)[0].strip()
            rendered[-1] = f"{key}: {max_rtx}"
    if not inserted:
        raise ValueError(f"{source}: [Global] section is missing")
    return "\n".join(rendered) + "\n"


def selected_map(stdout: str) -> str | None:
    clean = ANSI.sub("", stdout)
    fallback = None
    for line in clean.splitlines():
        for marker in ("Selected explicit map ", "Selected map "):
            if marker in line:
                return line.rsplit(marker, 1)[1].strip()
        if "Map path:" in line:
            return line.rsplit("Map path:", 1)[1].strip()
        if "map_file =" in line:
            fallback = line.rsplit("=", 1)[1].strip().removesuffix(" added.")
    return fallback


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=20260927)
    parser.add_argument("--duration-s", type=int, default=180)
    parser.add_argument("--batch-id")
    parser.add_argument(
        "--harq-model",
        choices=("legacy_bler", "disabled"),
        help="override the profile HARQ model",
    )
    parser.add_argument(
        "--max-rtx",
        type=int,
        help="override both UL and DL retry limits",
    )
    parser.add_argument(
        "--profiles",
        help="comma-separated profile names; defaults to the five references",
    )
    parser.add_argument(
        "--map-dir",
        type=Path,
        help="explicit map catalog directory for paired catalog comparisons",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "results" / "phy-v2-production-maps",
    )
    args = parser.parse_args()
    if args.max_rtx is not None and args.max_rtx < 0:
        raise SystemExit("--max-rtx must be non-negative")

    binary = ROOT / "bin" / "fikore"
    if not binary.is_file():
        raise SystemExit(f"build FikoRE first: {binary}")
    source_sha = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    if dirty:
        raise SystemExit(
            "profile evidence requires a clean worktree; commit tooling first")
    batch_id = args.batch_id or (
        f"phy-v2-production-{source_sha[:8]}-seed{args.seed}"
    )
    profiles = (
        tuple(args.profiles.split(","))
        if args.profiles
        else PROFILES
    )
    unknown = sorted(set(profiles) - EXPECTED_MAPS.keys())
    if unknown:
        raise SystemExit(f"unknown profiles: {unknown}")
    output = args.output.resolve()
    try:
        output.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        raise SystemExit(
            f"refusing to reuse evidence output directory: {output}")
    input_dir = output / "inputs"
    stdout_dir = output / "stdout"
    input_dir.mkdir()
    stdout_dir.mkdir()

    production_manifest = json.loads(
        (ROOT / "include" / "maps_scenarios" / "MANIFEST.json").read_text())
    map_by_name = {
        entry["file"]: entry for entry in production_manifest["maps"]
    }
    explicit_map_dir = (
        args.map_dir.resolve() if args.map_dir is not None else None)
    runs = []
    for profile in profiles:
        source = ROOT / "config" / f"{profile}.ini"
        run_id = f"{batch_id}-{profile}"
        log_dir = ROOT / "logs" / run_id
        try:
            log_dir.mkdir(parents=True, exist_ok=False)
        except FileExistsError:
            raise SystemExit(
                f"refusing to append to existing run log directory: {log_dir}")
        expected_map = EXPECTED_MAPS[profile]
        if explicit_map_dir is not None:
            explicit_map = explicit_map_dir / expected_map
            if (
                profile == "offline_umi_n40_npn"
                and not explicit_map.is_file()
            ):
                expected_map = (
                    "macroscopic_fading_map_URBAN_MICROCELL_3.5.json"
                )
                explicit_map = explicit_map_dir / expected_map
            if not explicit_map.is_file():
                raise SystemExit(
                    f"{profile}: explicit map is missing: {explicit_map}")
        else:
            explicit_map = None
        rendered = render_config(
            source,
            args.seed,
            run_id,
            args.duration_s,
            explicit_map,
            args.harq_model,
            args.max_rtx,
        )
        input_path = input_dir / source.name
        input_path.write_text(rendered)

        started = time.perf_counter()
        completed = subprocess.run(
            [str(binary), str(input_path)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "LC_ALL": "C"},
        )
        elapsed = time.perf_counter() - started
        stdout_path = stdout_dir / f"{profile}.log"
        stdout_path.write_text(completed.stdout)
        observed_map = selected_map(completed.stdout)
        if completed.returncode != 0:
            raise SystemExit(
                f"{profile} failed with return code {completed.returncode}; "
                f"see {stdout_path}")
        if observed_map is not None and not observed_map.endswith(expected_map):
            raise SystemExit(
                f"{profile}: selected {observed_map}, expected {expected_map}")
        if not log_dir.is_dir():
            raise SystemExit(f"{profile}: expected log directory {log_dir}")
        selected_map_path = (
            explicit_map
            if explicit_map is not None
            else ROOT / "include" / "maps_scenarios" / expected_map
        )
        if explicit_map is not None:
            selected_payload = json.loads(selected_map_path.read_text())
            realization_id = selected_payload.get(
                "metadata", {}).get("realization_id")
        else:
            realization_id = map_by_name[expected_map]["realization_id"]
        runs.append(
            {
                "profile": profile,
                "source_config": str(source.relative_to(ROOT)),
                "rendered_config": str(input_path),
                "rendered_config_sha256": sha256(input_path),
                "run_id": run_id,
                "return_code": completed.returncode,
                "elapsed_seconds": elapsed,
                "log_dir": str(log_dir),
                "stdout": str(stdout_path),
                "map_file": expected_map,
                "map_sha256": sha256(selected_map_path),
                "map_realization_id": realization_id,
                "harq_model_override": args.harq_model,
                "max_rtx_override": args.max_rtx,
            }
        )
        print(f"{profile}: {elapsed:.3f} s")

    manifest = {
        "schema_version": 1,
        "batch_id": batch_id,
        "source_sha": source_sha,
        "source_dirty": dirty,
        "seed": args.seed,
        "duration_s": args.duration_s,
        "environment": {
            "hostname": socket.gethostname(),
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "production_map_manifest_sha256": sha256(
            ROOT / "include" / "maps_scenarios" / "MANIFEST.json"),
        "runs": runs,
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(output / "manifest.json")


if __name__ == "__main__":
    main()
