#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The request and delivery interface, driven against a real FikoRE run.

Two UEs each fetch a queue of video-segment-sized objects, one after another, the
way a player does. What is printed is what the harness would see: completions,
their duration, and the telemetry of the window they finished in.
"""
import os
import time
from pathlib import Path

from fikore_transport.backend import (BackendConfig, DownloadCompleted,
                                      NetworkTelemetryReceived, TransportBackend)
from fikore_transport.cc import Cubic
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink

EMU = os.environ.get("FIKORE_DIR", str(Path(__file__).resolve().parents[2]))
SEGMENT_BYTES = 375_000     # about 3 Mbps of video at 1 s segments
N_UES = 2


def main(duration_s=6.0, delay_budget_s=0.1):
    cfg = EmulatorConfig(
        binary=os.path.join(EMU, "bin/fikore"),
        base_ini=os.path.join(EMU, "config/control_demo.ini"),
        socket_path="/tmp/fikore-backend.sock",
        duration_s=duration_s,
        work_dir=EMU,
        n_ues=N_UES,
        delay_budget_s=delay_budget_s,
        log_path="/tmp/fikore-backend-emu.log",
    )
    link = FikoreLink(Emulator(cfg), flow_to_ue={})
    backend = TransportBackend(link, BackendConfig(
        window_ttis=10, horizon_ttis=int(duration_s * 1000) - 20,
        rwnd=256 * 1024, cc_factory=Cubic, telemetry_every_windows=50))

    started = {}
    counters = {ue: 0 for ue in range(N_UES)}
    for ue in range(N_UES):
        counters[ue] += 1
        started[(ue, f"seg-{counters[ue]}")] = 0.0
        backend.submit_request(ue, f"seg-{counters[ue]}", SEGMENT_BYTES)

    wall0 = time.perf_counter()
    durations = []
    while True:
        step = backend.advance()
        for event in step.events:
            if isinstance(event, DownloadCompleted):
                took = event.time_s - started[(event.ue_id, event.request_id)]
                durations.append(took)
                print(f"  t={event.time_s:5.2f}s ue={event.ue_id} "
                      f"{event.request_id:8} done in {took*1000:6.1f} ms "
                      f"({SEGMENT_BYTES*8/took/1e6:5.1f} Mbps)")
                counters[event.ue_id] += 1
                nxt = f"seg-{counters[event.ue_id]}"
                started[(event.ue_id, nxt)] = event.time_s
                backend.submit_request(event.ue_id, nxt, SEGMENT_BYTES)
            elif isinstance(event, NetworkTelemetryReceived):
                f = event.fields
                print(f"  t={event.time_s:5.2f}s ue={event.ue_id} telemetry: "
                      f"{f.throughput_mbps:5.1f} Mbps rtt={f.rtt_ms or 0:5.1f} ms "
                      f"drop={f.drop_rate or 0:.3f} queue={(f.queue_bytes or 0)//1024:4d} KB "
                      f"sinr={f.sinr_db or 0:5.1f} dB rtx={f.retransmitted_bytes//1024} KB")
        if step.is_final:
            break

    wall = time.perf_counter() - wall0
    backend.close()
    print(f"\n{len(durations)} objects, median {sorted(durations)[len(durations)//2]*1000:.0f} ms, "
          f"{duration_s:.0f} s simulated in {wall:.1f} s of wall clock "
          f"({link.round_trips} slots, {wall/link.round_trips*1e6:.0f} us/slot)")


if __name__ == "__main__":
    main()
