#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Drive the generic SFV-VQEG player bridge through FikoRE's TransportBackend."""

import argparse
import collections
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from fikore_transport.backend import BackendConfig, TransportBackend, UeControl
from fikore_transport.cc import Cubic
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink

FIKORE_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = FIKORE_ROOT.parent


def revision(path: Path) -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=path, text=True).strip()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sfv-vqeg-root", type=Path,
        default=WORKSPACE / "SFV-VQEG-CAP-CSP-Collaboration-Experiments")
    parser.add_argument(
        "--sfv-core-root", type=Path,
        default=WORKSPACE / "SFV-Reference-Implementation")
    parser.add_argument("--config", type=Path)
    parser.add_argument("--duration", type=float)
    parser.add_argument("--rmax-mbps", type=float)
    parser.add_argument("--tcp-connection-mode", choices=("persistent", "fresh"),
                        default="persistent")
    parser.add_argument(
        "--output", type=Path,
        default=FIKORE_ROOT / "transport" / "benchmarks" / "results" / "sfv-fikore")
    args = parser.parse_args()

    vqeg_root = args.sfv_vqeg_root.resolve()
    core_root = args.sfv_core_root.resolve()
    config_path = (args.config or
                   vqeg_root / "examples" / "network-backend-mock.json").resolve()
    config = json.loads(config_path.read_text())
    if args.duration is not None:
        if args.duration <= 0:
            raise ValueError("duration must be positive")
        config["duration_s"] = args.duration
    config["dataset"] = json.loads(
        (core_root / "fixtures" / "content" / "content.json").read_text())
    config["run_id"] = f"{config.get('run_id', 'sfv-network')}-fikore"

    # The SFV-VQEG bridge is deliberately a script module rather than an installable
    # package. Keep this path adaptation in the optional benchmark, not in the
    # transport library.
    sys.path.insert(0, str(vqeg_root / "scripts"))
    from generic_player_bridge import NodePlayerBridge, run_network_experiment

    player_config = {
        key: config[key]
        for key in (
            "dataset", "sessions", "duration_s", "timingDefaults", "run_id",
            "signaling_level", "condition", "swipe_profile",
        )
        if key in config
    }
    ue_ids = {session["ue_id"] for session in config["sessions"]}
    ue_id_map = {external: physical
                 for physical, external in enumerate(sorted(ue_ids))}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="sfv-fikore-") as tmp:
        emulator = Emulator(EmulatorConfig(
            binary=str(FIKORE_ROOT / "bin" / "fikore"),
            base_ini=str(FIKORE_ROOT / "config" / "control_demo.ini"),
            socket_path=str(Path(tmp) / "control.sock"),
            duration_s=float(config["duration_s"]) + 1.0,
            work_dir=str(FIKORE_ROOT),
            n_ues=len(ue_ids),
            delay_budget_s=30.0,
            log_path=str(Path(tmp) / "emulator.log"),
        ))
        link = FikoreLink(emulator, flow_to_ue={}, ue_id_map=ue_id_map)
        backend_config = BackendConfig(
            window_ttis=10,
            horizon_ttis=int(float(config["duration_s"]) * 1000),
            rwnd=128 * 1024,
            cc_factory=Cubic,
            telemetry_every_windows=10,
            tcp_connection_mode=args.tcp_connection_mode,
        )
        backend = TransportBackend(link, backend_config)
        if args.rmax_mbps is not None:
            if args.rmax_mbps < 0:
                raise ValueError("rmax-mbps must be non-negative")
            for ue_id in ue_ids:
                backend.set_ue_control(
                    ue_id, UeControl(rmax_mbps=args.rmax_mbps))
        trace: list[dict] = []
        wall0 = time.perf_counter()
        with NodePlayerBridge(player_config) as bridge:
            result = run_network_experiment(
                backend, bridge, ue_ids=ue_ids, trace=trace)
        wall_s = time.perf_counter() - wall0

    sessions = []
    for session in result["sessions"]:
        payload = session["result"]
        requests = payload["requests"]
        record = payload["sessionRecord"]
        sessions.append({
            "ue_id": session["ue_id"],
            "player_behavior": record["player_behavior"],
            "termination_reason": payload["terminationReason"],
            "request_count": len(requests),
            "request_states": dict(collections.Counter(
                request["state"] for request in requests)),
            "abort_requests": sum(
                request["abortRequestedAt"] is not None for request in requests),
            "bytes_total": sum(request["bytesTotal"] for request in requests),
            "bytes_delivered": record["session_summary"]["bytes_delivered"],
            "bytes_data_wastage": record["session_summary"]["bytes_data_wastage"],
            "bytes_unresolved_at_cutoff": (
                record["session_summary"]["bytes_unresolved_at_cutoff"]),
        })

    accounted = sum(link.terminal_bytes.values()) + link.in_flight_bytes
    manifest = {
        "artifact_kind": "generated_result",
        "source_runner": "transport/benchmarks/validate_sfv.py",
        "regeneration_command": (
            "PYTHONPATH=transport python3 "
            "transport/benchmarks/validate_sfv.py "
            "--sfv-vqeg-root <SFV_VQEG_CHECKOUT> "
            "--sfv-core-root <SFV_CORE_CHECKOUT> "
            f"--tcp-connection-mode {args.tcp_connection_mode}"),
        "status": "completed",
        "config": str(config_path),
        "duration_s": config["duration_s"],
        "rmax_mbps": args.rmax_mbps,
        "transport": backend_config.transport,
        "tcp_connection_mode": backend_config.tcp_connection_mode,
        "wall_s": wall_s,
        "network_steps": len(trace),
        "fikore_revision": revision(FIKORE_ROOT),
        "sfv_vqeg_revision": revision(vqeg_root),
        "sfv_core_revision": revision(core_root),
        "submitted_bytes": link.submitted_bytes,
        "terminal_bytes": link.terminal_bytes,
        "in_flight_bytes": link.in_flight_bytes,
        "bytes_conserved": accounted == link.submitted_bytes,
        "reply_bytes": link.received_bytes,
        "max_events_per_reply": link.max_events_per_reply,
        "sessions": sessions,
    }
    write_json(output / "run-result.json", result)
    write_json(output / "network-step-transcript.json", trace)
    write_json(output / "run-manifest.json", manifest)
    for session in result["sessions"]:
        write_json(output / f"ue-{session['ue_id']}-session.json",
                   session["result"])
    print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0 if manifest["bytes_conserved"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
