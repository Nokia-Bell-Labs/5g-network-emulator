#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""One TCP transfer over the real emulator, in lockstep, with no emulator changes.

Answers the only question that matters before building anything else: does a
transport model in Python, reacting one slot at a time, actually run against
FikoRE, and at what cost in wall-clock time.
"""
import os
import time
from pathlib import Path

from fikore_transport.cc import Cubic, Reno
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink
from fikore_transport.runner import Flow, Runner
from fikore_transport.tcp import TcpReceiver, TcpSender

EMU = os.environ.get("FIKORE_DIR", str(Path(__file__).resolve().parents[2]))


def run(size_bytes=1_000_000, duration_s=4.0, cc_cls=Cubic, ack_over_link=False,
        delay_budget_s=30.0, rwnd=4 * 1024 * 1024, label="", use_events=True):
    cfg = EmulatorConfig(
        binary=os.path.join(EMU, "bin/fikore"),
        base_ini=os.path.join(EMU, "config/control_demo.ini"),
        socket_path="/tmp/fikore-transport.sock",
        duration_s=duration_s,
        work_dir=EMU,
        n_ues=1,
        delay_budget_s=delay_budget_s,
        log_path="/tmp/fikore-transport-emu.log",
    )
    emulator = Emulator(cfg)
    link = FikoreLink(emulator, flow_to_ue={1: 0}, use_events=use_events)
    runner = Runner(link)
    cc = cc_cls(mss=link.mss, cwnd=10 * link.mss)
    sender = TcpSender(1, cc, runner.clock, runner.sched, link.mss, rwnd=rwnd)
    receiver = TcpReceiver(1, link.mss)
    runner.add_flow(Flow(sender, receiver, ack_over_link=ack_over_link))
    if size_bytes is None:
        sender.set_unlimited()
    else:
        sender.app_write(size_bytes)

    t0 = time.perf_counter()
    try:
        runner.run_until(int(duration_s * 1000) - 2)
    finally:
        wall = time.perf_counter() - t0
        link.close()

    done = sender.finished_at_tti
    lost = sum(1 for a in runner.arrivals if a.fate != "delivered")
    if done:
        rate = f"{receiver.rcv_nxt * 8 / (done / 1000.0) / 1e6:6.2f} Mbps in {done} ms"
    else:
        rate = f"{receiver.rcv_nxt * 8 / duration_s / 1e6:6.2f} Mbps sustained"
    print(f"{label or cc_cls.__name__:22} acks={'link' if ack_over_link else 'model'} "
          f"-> {rate} | srtt={(sender.srtt_us or 0)/1000:5.1f} ms "
          f"cwnd={cc.cwnd/link.mss:6.1f} seg rtx={sender.stats.retransmits:4d} "
          f"lost={lost:4d} rto={sender.stats.rto_events:2d} | "
          f"{link.round_trips} slots in {wall:5.2f} s "
          f"({wall/max(link.round_trips,1)*1e6:5.0f} us/slot), "
          f"{link.received_bytes/1e6:.2f} MB replies")
    # The byte account, which is the only thing that says the run was real rather
    # than merely fast: what went in came out delivered, lost with a reason, or is
    # still inside the emulator.
    terminal = link.terminal_bytes
    settled = sum(terminal.values()) + link.in_flight_bytes
    causes = " ".join(f"{k}={v}" for k, v in link.lost_bytes_by_cause.items() if v)
    print(f"{'':22} bytes: submitted={link.submitted_bytes} "
          f"delivered={terminal['delivered']} dropped={terminal['dropped']} "
          f"expired={terminal['expired']} in-flight={link.in_flight_bytes} "
          f"-> {'conserved' if settled == link.submitted_bytes else 'LOST ' + str(link.submitted_bytes - settled)}"
          f"{' | ' + causes if causes else ''}")
    return sender


if __name__ == "__main__":
    # An object on an unloaded cell: it never leaves slow start, which is the
    # honest answer for a 1 MB video segment with nothing to compete against.
    for cc in (Reno, Cubic):
        run(cc_cls=cc, label=f"{cc.__name__} 1 MB object")
    run(cc_cls=Cubic, ack_over_link=True, label="CUBIC, acks over the air")
    # A bulk flow with a realistic delay budget and a bounded receive window, which
    # is where loss, recovery and the congestion response actually happen.
    for cc in (Reno, Cubic):
        run(cc_cls=cc, size_bytes=None, duration_s=3.0, delay_budget_s=0.02,
            rwnd=256 * 1024, label=f"{cc.__name__} bulk, 20 ms budget")
