#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Reproducible full-duration validation for the offline transport path."""

import argparse
import json
import os
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

from fikore_transport.backend import (BackendConfig, DownloadCompleted,
                                      NetworkTelemetryReceived, TransportBackend)
from fikore_transport.cc import Cubic
from fikore_transport.cc_prague import Prague, PragueUnavailable
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink

REPO = Path(os.environ.get("FIKORE_DIR", Path(__file__).resolve().parents[2]))


def jain(values: list[float]) -> float:
    total = sum(values)
    squared = sum(v * v for v in values)
    return total * total / (len(values) * squared) if squared else 1.0


def make_backend(work: Path, duration_s: float, n_ues: int,
                 delay_budget_s: float, rwnd: int, cc_factory=Cubic,
                 ecn: str = "not-ect",
                 extra: dict[str, str] | None = None
                 ) -> tuple[FikoreLink, TransportBackend]:
    cfg = EmulatorConfig(
        binary=str(REPO / "bin" / "fikore"),
        base_ini=str(REPO / "config" / "control_demo.ini"),
        socket_path=str(work / "control.sock"),
        duration_s=duration_s + 1,
        work_dir=str(REPO),
        n_ues=n_ues,
        delay_budget_s=delay_budget_s,
        log_path=str(work / "emulator.log"),
        extra=extra or {},
    )
    link = FikoreLink(Emulator(cfg), flow_to_ue={})
    backend = TransportBackend(link, BackendConfig(
        window_ttis=10,
        horizon_ttis=int(duration_s * 1000),
        rwnd=rwnd,
        cc_factory=cc_factory,
        ecn=ecn,
        retain_request_history=True,
        telemetry_every_windows=100,
    ))
    return link, backend


def common_result(kind: str, duration_s: float, wall_s: float,
                  link: FikoreLink, backend: TransportBackend) -> dict:
    accounted = sum(link.terminal_bytes.values()) + link.in_flight_bytes
    return {
        "artifact_kind": "generated_result",
        "source_runner": "transport/benchmarks/validate_scale.py",
        "regeneration_command": (
            "PYTHONPATH=transport python3 "
            "transport/benchmarks/validate_scale.py "
            f"--duration {duration_s:g} --mode {kind}"),
        "kind": kind,
        "fikore_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
        "simulated_s": duration_s,
        "wall_s": wall_s,
        "slots": link.round_trips,
        "us_per_slot": wall_s / max(link.round_trips, 1) * 1e6,
        "reply_bytes": link.received_bytes,
        "max_events_per_reply": link.max_events_per_reply,
        "submitted_bytes": link.submitted_bytes,
        "terminal_bytes": dict(link.terminal_bytes),
        "in_flight_bytes": link.in_flight_bytes,
        "bytes_conserved": accounted == link.submitted_bytes,
        "lost_bytes_by_cause": dict(link.lost_bytes_by_cause),
        "transport_retransmits": sum(
            request.flow.sender.stats.retransmits
            for request in backend.requests.values()),
        "transport_rto_events": sum(
            request.flow.sender.stats.rto_events
            for request in backend.requests.values()),
    }


def run_objects(duration_s: float, output: Path) -> dict:
    n_ues = 4
    segment_bytes = 375_000
    with tempfile.TemporaryDirectory(prefix="fikore-scale-objects-") as tmp:
        link, backend = make_backend(Path(tmp), duration_s, n_ues, 30.0,
                                     256 * 1024)
        started: dict[tuple[int, str], float] = {}
        counters = [0] * n_ues
        durations: list[float] = []
        rtts: dict[int, list[float]] = {ue: [] for ue in range(n_ues)}
        for ue in range(n_ues):
            counters[ue] += 1
            name = f"seg-{counters[ue]}"
            started[(ue, name)] = 0.0
            backend.submit_request(ue, name, segment_bytes)

        wall0 = time.perf_counter()
        while True:
            step = backend.advance()
            for event in step.events:
                if isinstance(event, DownloadCompleted):
                    durations.append(event.time_s -
                                     started[(event.ue_id, event.request_id)])
                    if not step.is_final:
                        counters[event.ue_id] += 1
                        name = f"seg-{counters[event.ue_id]}"
                        started[(event.ue_id, name)] = event.time_s
                        backend.submit_request(event.ue_id, name, segment_bytes)
                elif (isinstance(event, NetworkTelemetryReceived)
                      and event.fields.rtt_ms is not None):
                    rtts[event.ue_id].append(event.fields.rtt_ms)
            backend.runner.arrivals.clear()
            if step.is_final:
                break
        wall = time.perf_counter() - wall0

        delivered_by_ue = [
            sum(request.flow.receiver.rcv_nxt for request in backend.requests.values()
                if request.ue_id == ue)
            for ue in range(n_ues)
        ]
        result = common_result("objects", duration_s, wall, link, backend)
        result.update({
            "n_ues": n_ues,
            "segment_bytes": segment_bytes,
            "objects_completed": len(durations),
            "median_object_ms": statistics.median(durations) * 1000,
            "delivered_bytes_by_ue": delivered_by_ue,
            "throughput_mbps_by_ue": [
                value * 8 / duration_s / 1e6 for value in delivered_by_ue],
            "throughput_jain": jain(delivered_by_ue),
            "mean_rtt_ms_by_ue": [
                statistics.mean(rtts[ue]) if rtts[ue] else None
                for ue in range(n_ues)],
        })
        backend.close()
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def run_loss(duration_s: float, output: Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="fikore-scale-loss-") as tmp:
        link, backend = make_backend(Path(tmp), duration_s, 1, 0.020,
                                     512 * 1024)
        # Intentionally larger than the run can deliver: a continuous bulk transfer.
        backend.submit_request(0, "bulk", 4_000_000_000)
        wall0 = time.perf_counter()
        while True:
            step = backend.advance()
            backend.runner.arrivals.clear()
            if step.is_final:
                break
        wall = time.perf_counter() - wall0

        request = backend.requests[(0, "bulk")]
        result = common_result("loss", duration_s, wall, link, backend)
        result.update({
            "n_ues": 1,
            "delay_budget_s": 0.020,
            "receive_window_bytes": 512 * 1024,
            "delivered_bytes": request.flow.receiver.rcv_nxt,
            "goodput_mbps": request.flow.receiver.rcv_nxt * 8 / duration_s / 1e6,
            "srtt_ms": ((request.flow.sender.srtt_us or 0) / 1000.0),
        })
        backend.close()
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def run_prague(duration_s: float, output: Path) -> dict:
    try:
        Prague()
    except PragueUnavailable as exc:
        result = {"kind": "prague", "skipped": True, "reason": str(exc)}
        output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        return result

    with tempfile.TemporaryDirectory(prefix="fikore-scale-prague-") as tmp:
        link, backend = make_backend(
            Path(tmp), duration_s, 1, 0.3, 4 * 1024 * 1024,
            cc_factory=Prague, ecn="ect1",
            extra={"l4s_dual_queue": "true", "l4s_target_ms": "5.0"})
        backend.submit_request(0, "bulk", 4_000_000_000)
        wall0 = time.perf_counter()
        while True:
            step = backend.advance()
            backend.runner.arrivals.clear()
            if step.is_final:
                break
        wall = time.perf_counter() - wall0

        request = backend.requests[(0, "bulk")]
        sender = request.flow.sender
        receiver = request.flow.receiver
        result = common_result("prague", duration_s, wall, link, backend)
        result.update({
            "n_ues": 1,
            "ecn": "ect1",
            "l4s_target_ms": 5.0,
            "delivered_bytes": receiver.rcv_nxt,
            "goodput_mbps": receiver.rcv_nxt * 8 / duration_s / 1e6,
            "srtt_ms": (sender.srtt_us or 0) / 1000.0,
            "ce_segments": receiver.pkts_ce,
            "lost_segments": receiver.pkts_lost,
            "prague": sender.cc.stats(),
        })
        backend.close()
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=float, default=300.0)
    parser.add_argument("--output-dir", type=Path,
                        default=Path("transport/benchmarks/results"))
    parser.add_argument("--mode", choices=("objects", "loss", "prague", "all"),
                        default="all")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    if args.mode in ("objects", "all"):
        results.append(run_objects(
            args.duration, args.output_dir / "scale-objects.json"))
    if args.mode in ("loss", "all"):
        results.append(run_loss(
            args.duration, args.output_dir / "scale-loss.json"))
    if args.mode in ("prague", "all"):
        results.append(run_prague(
            args.duration, args.output_dir / "scale-prague.json"))
    for result in results:
        print(json.dumps(result, sort_keys=True))
    return 0 if all(result.get("skipped") or result["bytes_conserved"]
                    for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
