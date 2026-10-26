#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Prague against the real emulator, over its dual queue.

The experiment the whole library is for: the same saturating transfer over the same
cell, once with CUBIC on not-ECT traffic and once with Prague on ECT(1), so that the
difference between filling a queue and being told about it is measured rather than
argued. Needs an emulator that accepts `ecn` on inject and reports `ce_bytes` per
object, and the Prague binding built with `make -C prague`.
"""
import os
import sys
import time
from pathlib import Path

from fikore_transport.cc import Cubic
from fikore_transport.cc_prague import Prague, PragueUnavailable
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink
from fikore_transport.runner import Flow, Runner
from fikore_transport.tcp import TcpReceiver, TcpSender

EMU = os.environ.get("FIKORE_DIR", str(Path(__file__).resolve().parents[2]))


def run(cc_cls, ecn, duration_s=3.0, dual_queue=True, delay_budget_s=0.3):
    cfg = EmulatorConfig(
        binary=os.path.join(EMU, "bin/fikore"),
        base_ini=os.path.join(EMU, "config/control_demo.ini"),
        socket_path="/tmp/fikore-prague.sock",
        duration_s=duration_s,
        work_dir=EMU,
        n_ues=1,
        delay_budget_s=delay_budget_s,
        log_path="/tmp/fikore-prague-emu.log",
        extra={"l4s_dual_queue": "true" if dual_queue else "false",
               "l4s_target_ms": "5.0"},
    )
    emulator = Emulator(cfg)
    link = FikoreLink(emulator, flow_to_ue={1: 0})
    runner = Runner(link)
    cc = cc_cls(mss=link.mss, cwnd=10 * link.mss)
    sender = TcpSender(1, cc, runner.clock, runner.sched, link.mss, ecn=ecn,
                       rwnd=4 * 1024 * 1024)
    receiver = TcpReceiver(1, link.mss)
    runner.add_flow(Flow(sender, receiver))
    sender.set_unlimited()

    ttis = int(duration_s * 1000) - 50
    started = time.time()
    try:
        runner.run_until(ttis)
    finally:
        wall = time.time() - started
        state = dict(link.last_state.get((0, "dl"), {}))
        link.close()

    return {
        "cc": cc_cls.__name__,
        "ecn": ecn,
        "mbps": receiver.bytes_received * 8 / (ttis / 1000) / 1e6,
        "srtt_ms": (sender.srtt_us or 0) / 1000.0,
        "ce_segments": receiver.pkts_ce,
        "lost_segments": receiver.pkts_lost,
        "retransmits": sender.stats.retransmits,
        "queue_delay_ms": float(state.get("latency_ms", 0.0)),
        "wall_s": wall,
        "prague": cc.stats() if hasattr(cc, "stats") else None,
    }


def main() -> int:
    try:
        Prague()
    except PragueUnavailable as exc:
        print(f"Prague is not built: {exc}")
        return 1

    for cc_cls, ecn in ((Cubic, "not-ect"), (Prague, "ect1")):
        r = run(cc_cls, ecn)
        print(f"{r['cc']:7} ecn={r['ecn']:8} {r['mbps']:6.2f} Mbps  "
              f"srtt={r['srtt_ms']:6.1f} ms  ce={r['ce_segments']:5d}  "
              f"lost={r['lost_segments']:5d}  rtx={r['retransmits']:5d}  "
              f"queue={r['queue_delay_ms']:5.1f} ms  ({r['wall_s']:.1f}s wall)")
        if r["prague"]:
            p = r["prague"]
            print(f"        prague: state={p['state']} alpha={p['alpha']:.4f} "
                  f"rate={p['pacing_rate_bps'] / 1e6:.1f} Mbps cwnd={p['cwnd'] / 1024:.1f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
