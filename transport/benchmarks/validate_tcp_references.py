#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Compare the transport controllers with independent public references."""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

from fikore_transport.cc import AckInfo, Cubic, Reno
from fikore_transport.cc_prague import Prague
from fikore_transport.clock import Clock, Scheduler
from fikore_transport.tcp import TcpSender

MSS = 1500
ROOT = Path(__file__).resolve().parents[2]


def close(a: float, b: float, tolerance: float = 1e-9) -> None:
    if not math.isclose(a, b, rel_tol=tolerance, abs_tol=tolerance):
        raise AssertionError(f"{a} != {b}")


def validate_ns_py(ns_root: Path) -> dict:
    sys.path.insert(0, str(ns_root))
    from ns.flow.cc import TCPReno
    from ns.flow.cubic import TCPCubic

    ours_reno = Reno(mss=MSS, cwnd=10 * MSS, ssthresh=10 * MSS)
    ref_reno = TCPReno(mss=MSS, cwnd=10 * MSS, ssthresh=10 * MSS)
    for index in range(500):
        ours_reno.on_ack(AckInfo(MSS, 50_000, (index + 1) * 1000))
        ref_reno.ack_received(0.05, (index + 1) / 1000)
        close(ours_reno.cwnd, ref_reno.cwnd)
    ours_reno.on_dupacks(3)
    ref_reno.consecutive_dupacks_received()
    close(ours_reno.cwnd, ref_reno.cwnd)
    close(ours_reno.ssthresh, ref_reno.ssthresh)
    ours_reno.on_recovered()
    ref_reno.dupack_over()
    close(ours_reno.cwnd, ref_reno.cwnd)
    ours_reno.on_rto()
    ref_reno.timer_expired()
    close(ours_reno.cwnd, ref_reno.cwnd)
    close(ours_reno.ssthresh, ref_reno.ssthresh)

    ours_cubic = Cubic(mss=MSS, cwnd=100 * MSS, ssthresh=0, beta=0.3)
    ref_cubic = TCPCubic(mss=MSS, cwnd=100 * MSS, ssthresh=0, beta=0.3)
    max_cwnd_error = 0.0
    for index in range(1000):
        now_s = 1.0 + index / 1000.0
        ours_cubic.on_ack(AckInfo(MSS, 50_000, int(now_s * 1e6)))
        ref_cubic.ack_received(0.05, now_s)
        max_cwnd_error = max(max_cwnd_error,
                             abs(ours_cubic.cwnd - ref_cubic.cwnd))
    if max_cwnd_error > MSS:
        raise AssertionError(f"CUBIC differs from ns.py by {max_cwnd_error} bytes")

    ours_cubic.on_dupacks(3)
    ref_cubic.consecutive_dupacks_received()
    close(ours_cubic.cwnd, ref_cubic.cwnd)
    close(ours_cubic.ssthresh, ref_cubic.ssthresh)
    ours_cubic.on_rto()
    ref_cubic.timer_expired()
    close(ours_cubic.cwnd, ref_cubic.cwnd)
    close(ours_cubic.ssthresh, ref_cubic.ssthresh)
    return {
        "revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ns_root, text=True).strip(),
        "reno_ack_steps": 500,
        "cubic_ack_steps": 1000,
        "cubic_max_cwnd_error_bytes": max_cwnd_error,
    }


def validate_rfc_vectors() -> dict:
    cubic = Cubic(mss=MSS, cwnd=100 * MSS, ssthresh=0,
                  beta=0.3, tcp_friendliness=False)
    cubic.d_min_s = 0.05
    cubic._update(1.0)
    cubic._update(2.0)
    expected_target = 100 + 0.4 * 1.05 ** 3
    expected_cnt = 100 / (expected_target - 100)
    close(cubic.cnt, expected_cnt)

    loss = Cubic(mss=MSS, cwnd=100 * MSS, ssthresh=0, beta=0.3)
    loss.on_dupacks(3)
    close(loss.ssthresh, 70 * MSS)
    close(loss.cwnd, 73 * MSS)
    loss.on_rto()
    close(loss.cwnd, MSS)
    close(loss.ssthresh, 51.1 * MSS)

    clock = Clock()
    sender = TcpSender(1, Reno(mss=MSS), clock, Scheduler(clock), MSS)
    sender._update_rtt(100_000)
    if (sender.srtt_us, sender.rttvar_us, sender.rto_us) != (
            100_000.0, 50_000.0, 300_000):
        raise AssertionError("RFC 6298 first-sample vector failed")
    sender._update_rtt(120_000)
    close(sender.srtt_us, 102_500.0)
    close(sender.rttvar_us, 42_500.0)
    if sender.rto_us != 272_500:
        raise AssertionError("RFC 6298 second-sample vector failed")
    return {
        "cubic_rfc": "RFC 8312/9438, windows in MSS, C=0.4, beta=0.7",
        "reno_rfc": "RFC 5681",
        "rto_rfc": "RFC 6298 with Linux 200 ms floor",
        "cubic_target_packets_at_1_05_s": expected_target,
        "cubic_ack_count": expected_cnt,
    }


def validate_prague() -> dict:
    controller = Prague(mss=MSS, cwnd=10 * MSS)
    stats = controller.stats()
    return {
        "reference": "L4STeam/udp_prague prague_cc.cpp through C ABI shim",
        "packet_size": stats["packet_size"],
        "initial_cwnd": stats["cwnd"],
        "state": stats["state"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ns-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = {
        "artifact_kind": "generated_result",
        "source_runner": "transport/benchmarks/validate_tcp_references.py",
        "regeneration_command": (
            "PYTHONPATH=transport python3 "
            "transport/benchmarks/validate_tcp_references.py "
            "--ns-root <NS_PY_CHECKOUT> --output <OUTPUT_JSON>"),
        "fikore_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "ns_py": validate_ns_py(args.ns_root.resolve()),
        "standards": validate_rfc_vectors(),
        "prague": validate_prague(),
    }
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
