# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""iperf3-shaped TCP/UDP validation over FikoRE offline co-simulation."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from .emulator import Emulator, EmulatorConfig
from .fikore_link import FikoreLink
from .iperf_config import (IperfConfig, add_override, load_config, parse_bytes,
                           parse_rate)
from .iperf_report import document, dumps, human
from .iperf_session import IperfSession
from .scenario import ScenarioDocument


REPO = Path(__file__).resolve().parents[2]


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(
        prog="fikore-iperf3", description=__doc__,
        epilog="This is an offline transport model, not a kernel iperf3 server.")
    value.add_argument("-c", "--scenario", type=Path,
                       help="FikoRE INI scenario (the offline equivalent of host)")
    value.add_argument("--config", type=Path, help="versioned JSON run configuration")
    value.add_argument("--binary", type=Path, help="path to bin/fikore")
    value.add_argument("--ue", action="append", default=[],
                       help="UE block or concrete textual UE target; repeatable")
    value.add_argument("--ues", help="comma-separated UE selectors")
    value.add_argument("--offline", action="store_true",
                       help="materialize the selected UEs as simulated UEs")
    value.add_argument("-t", "--time", type=float, dest="duration_s")
    value.add_argument("-n", "--bytes", dest="bytes_total")
    value.add_argument("-P", "--parallel", type=int, dest="parallel_per_ue")
    value.add_argument("-R", "--reverse", action="store_true", default=None)
    value.add_argument("-u", "--udp", action="store_true", default=None)
    value.add_argument("-b", "--bitrate")
    value.add_argument("-l", "--length", dest="length_bytes")
    value.add_argument("-i", "--interval", type=float, dest="interval_s")
    value.add_argument("-M", "--set-mss", dest="mss")
    value.add_argument("-w", "--window", dest="rwnd_bytes")
    value.add_argument("-C", "--congestion")
    value.add_argument("--ack-over-link", action="store_true", default=None)
    value.add_argument("--ecn", choices=("not-ect", "ect0", "ect1"))
    value.add_argument("--drain-timeout", type=float, dest="drain_timeout_s")
    value.add_argument("--timeout", type=float, dest="timeout_s",
                       help="safety timeout for finite-byte TCP runs")
    value.add_argument("-J", "--json", action="store_true", dest="json_output",
                       default=None)
    value.add_argument("--json-file", type=Path)
    value.add_argument("--logfile", type=Path)
    value.add_argument("--output-dir", type=Path)
    value.add_argument("--quiet", action="store_true", default=None)
    value.add_argument("--set", action="append", default=[], metavar="SECTION.KEY=VALUE")
    value.add_argument("--set-ue", action="append", default=[],
                       metavar="UE_ID.KEY=VALUE")
    value.add_argument("--seed", help="override Global.seed")
    value.add_argument("--dry-run", action="store_true",
                       help="validate and print the effective INI without starting FikoRE")
    return value


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        cfg = _configuration(args)
        scenario = ScenarioDocument.read(str(cfg.scenario))
        concrete, blocks, physical = _resolve_ues(scenario, cfg.ues)
        cfg.ues = concrete
        cfg.validate()
        work_context = (
            _PersistentDirectory(cfg.output_dir)
            if cfg.output_dir is not None
            else tempfile.TemporaryDirectory(prefix="fikore-iperf-"))
        with work_context as work_value:
            work = Path(work_value)
            work.mkdir(parents=True, exist_ok=True)
            emulator_cfg = EmulatorConfig(
                binary=str(cfg.binary), base_ini=str(cfg.scenario),
                socket_path=str(work / "control.sock"),
                duration_s=((cfg.timeout_s if cfg.bytes_total is not None
                             else cfg.duration_s + cfg.drain_timeout_s) + 2.0),
                work_dir=str(REPO), n_ues=None, pkt_size_bits=cfg.mss * 8,
                delay_budget_s=30.0, log_path=str(work / "emulator.log"),
                study_ues=blocks, section_overrides=cfg.section_overrides,
                ue_overrides=cfg.ue_overrides)
            if args.dry_run:
                effective = emulator_cfg.render(str(work / "effective-config.ini"))
                sys.stdout.write(Path(effective).read_text())
                return 0
            if not cfg.binary.is_file():
                raise ValueError(f"FikoRE binary does not exist: {cfg.binary}")
            emulator = None
            link = None
            session = None
            try:
                emulator = Emulator(emulator_cfg)
                link = FikoreLink(
                    emulator, flow_to_ue={},
                    ue_target_map={physical[target]: target for target in concrete})
                session = IperfSession(
                    cfg, link,
                    ue_mapping={target: physical[target] for target in concrete})
                result = session.run()
                metadata = _metadata(
                    emulator_cfg, work, persistent=cfg.output_dir is not None)
                machine = document(cfg, result, metadata)
                text = human(result) + "\n"
            finally:
                if session is not None:
                    session.close()
                elif link is not None:
                    link.close()
                elif emulator is not None:
                    emulator.close()
            if not cfg.quiet:
                sys.stdout.write(dumps(machine) if cfg.json_output else text)
            if args.json_file is not None:
                args.json_file.write_text(dumps(machine))
            if args.logfile is not None:
                args.logfile.write_text(text)
            if cfg.output_dir is not None:
                (work / "run-result.json").write_text(dumps(machine))
            conserved = result.conservation.get("bytes_conserved")
            return 0 if result.drained and conserved is not False else 1
    except (OSError, RuntimeError, ValueError) as exc:
        if args.json_output:
            sys.stdout.write(json.dumps({"error": str(exc)}, indent=2) + "\n")
        else:
            print(f"fikore-iperf3: error: {exc}", file=sys.stderr)
        return 2


def _configuration(args: argparse.Namespace) -> IperfConfig:
    if args.duration_s is not None and args.bytes_total is not None:
        raise ValueError("--time and --bytes are mutually exclusive")
    scenario = (args.scenario or REPO / "config" / "control_demo.ini").resolve()
    defaults = IperfConfig(scenario=scenario, binary=REPO / "bin" / "fikore")
    overrides = {
        "scenario": args.scenario.resolve() if args.scenario else None,
        "binary": args.binary.resolve() if args.binary else None,
        "duration_s": args.duration_s,
        "bytes_total": parse_bytes(args.bytes_total) if args.bytes_total else None,
        "parallel_per_ue": args.parallel_per_ue,
        "direction": "ul" if args.reverse else None,
        "protocol": "udp" if args.udp else None,
        "bitrate_bps": parse_rate(args.bitrate) if args.bitrate else None,
        "length_bytes": parse_bytes(args.length_bytes) if args.length_bytes else None,
        "interval_s": args.interval_s,
        "mss": parse_bytes(args.mss) if args.mss else None,
        "rwnd_bytes": parse_bytes(args.rwnd_bytes) if args.rwnd_bytes else None,
        "congestion": args.congestion.lower() if args.congestion else None,
        "ack_over_link": args.ack_over_link,
        "ecn": args.ecn,
        "drain_timeout_s": args.drain_timeout_s,
        "timeout_s": args.timeout_s,
        "json_output": args.json_output,
        "quiet": args.quiet,
        "output_dir": args.output_dir.resolve() if args.output_dir else None,
    }
    cfg = load_config(args.config, defaults, overrides)
    if args.duration_s is not None:
        # CLI mode selection overrides a byte-limited JSON configuration.
        cfg.bytes_total = None
    selected = list(args.ue)
    if args.ues:
        selected.extend(item.strip() for item in args.ues.split(",") if item.strip())
    if selected:
        cfg.ues = selected
    for expression in args.set:
        add_override(cfg, expression)
    for expression in args.set_ue:
        add_override(cfg, expression, ue=True)
    if args.seed is not None:
        cfg.section_overrides[("Global", "seed")] = str(args.seed)
    if cfg.congestion == "prague" and cfg.ecn == "not-ect":
        cfg.ecn = "ect1"
    return cfg


def _resolve_ues(doc: ScenarioDocument, selectors: list[str]
                 ) -> tuple[list[str], list[str], dict[str, int]]:
    blocks = doc.ue_ids()
    if not selectors:
        selectors = [blocks[0]] if blocks else []
    concrete: list[str] = []
    selected_blocks: list[str] = []
    physical: dict[str, int] = {}
    offset = 0
    for block in blocks:
        count = doc.ue_count(block)
        targets = [block] if count == 1 else [
            f"{block}_{index}" for index in range(count)]
        for index, target in enumerate(targets):
            physical[target] = offset + index
        offset += count
        for selector in selectors:
            if selector == block:
                concrete.extend(targets)
                selected_blocks.append(block)
            elif selector in targets:
                concrete.append(selector)
                selected_blocks.append(block)
    missing = [selector for selector in selectors
               if selector not in blocks and selector not in physical]
    if missing:
        raise ValueError(
            f"unknown UE selectors: {', '.join(missing)}; blocks: {', '.join(blocks)}")
    concrete = list(dict.fromkeys(concrete))
    selected_blocks = list(dict.fromkeys(selected_blocks))
    if not concrete:
        raise ValueError("the scenario has no selectable UEs")
    return concrete, selected_blocks, physical


def _metadata(cfg: EmulatorConfig, work: Path, persistent: bool) -> dict:
    try:
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        revision = None
    effective = work / "cosim-run.ini"
    effective_sha256 = hashlib.sha256(effective.read_bytes()).hexdigest()
    try:
        dirty = bool(subprocess.check_output(
            ["git", "status", "--porcelain"], cwd=REPO, text=True).strip())
    except (OSError, subprocess.CalledProcessError):
        dirty = None
    value = {
        "fikore_revision": revision,
        "fikore_worktree_dirty": dirty,
        "effective_config_sha256": effective_sha256,
        "source_scenario": str(cfg.base_ini),
        "artifacts_persistent": persistent,
        "overrides": [asdict(override) for override in cfg.last_overrides],
    }
    if persistent:
        value["effective_config"] = str(effective)
        value["emulator_log"] = str(work / "emulator.log")
    return value


class _PersistentDirectory:
    def __init__(self, path: Path) -> None:
        self.path = path

    def __enter__(self) -> str:
        self.path.mkdir(parents=True, exist_ok=True)
        return str(self.path)

    def __exit__(self, *_args) -> None:
        return None


if __name__ == "__main__":
    raise SystemExit(main())
