# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The transport layer, exercised against the deterministic Python bottleneck.

These tests do not need an emulator. They are the ones that say whether the state
machine is right; the emulator tests say whether the integration is right.
"""
import os
import sys

from fikore_transport.cc import Cubic, Reno
from fikore_transport.clock import Clock, Scheduler
from fikore_transport.link import LoopbackConfig, LoopbackLink
from fikore_transport.runner import Flow, Runner
from fikore_transport.tcp import Ack, TcpReceiver, TcpSender

MSS = 1500


def build(cc_cls=Reno, size=None, unlimited=False, cfg=None, ack_over_link=False):
    link = LoopbackLink(cfg or LoopbackConfig(rate_bps=10e6, owd_ttis=10, mss=MSS))
    runner = Runner(link)
    cc = cc_cls(mss=MSS, cwnd=10 * MSS)
    sender = TcpSender(1, cc, runner.clock, runner.sched, MSS)
    receiver = TcpReceiver(1, MSS)
    runner.add_flow(Flow(sender, receiver, ack_over_link=ack_over_link))
    if unlimited:
        sender.set_unlimited()
    elif size:
        sender.app_write(size)
    return runner, sender, receiver, link


def test_transfer_completes_and_conserves_bytes():
    size = 500 * 1024
    runner, sender, receiver, link = build(size=size)
    runner.run_until(4000)
    assert sender.snd_una == size, f"only {sender.snd_una} of {size} acknowledged"
    assert receiver.rcv_nxt == size, "the receiver must have a contiguous stream"
    assert sender.finished_at_tti is not None
    unique = {a.seq: a.size for a in runner.arrivals
              if a.fate == "delivered" and a.kind == "data"}
    assert sum(unique.values()) == size, "every byte must arrive exactly once"


def test_receive_window_counts_sacked_bytes_until_cumulative_ack():
    clock = Clock()
    sender = TcpSender(1, Reno(mss=MSS), clock, Scheduler(clock), MSS, rwnd=3000)
    sender.app_write(6000)
    first = sender.send_window()
    assert sum(segment.size for segment in first) == 3000
    sender.on_ack(Ack(1, 0, 0, sacks=((1500, 3000),)))
    assert sender.in_flight == 1500          # congestion pipe excludes SACKed data
    assert sender.send_window() == []        # receive window still contains both


def test_receive_window_smaller_than_mss_splits_the_segment():
    clock = Clock()
    sender = TcpSender(1, Reno(mss=MSS), clock, Scheduler(clock), MSS, rwnd=1000)
    sender.app_write(1500)
    first = sender.send_window()
    assert [segment.size for segment in first] == [1000]


def test_rto_retransmission_is_not_blocked_by_a_full_receive_window():
    clock = Clock()
    sender = TcpSender(1, Reno(mss=MSS), clock, Scheduler(clock), MSS, rwnd=3000)
    sender.app_write(6000)
    assert sum(segment.size for segment in sender.send_window()) == 3000
    sender._on_rto()
    retransmission = sender.send_window()
    assert retransmission and retransmission[0].seq == 0


def test_throughput_approaches_the_bottleneck():
    size = 2 * 1024 * 1024
    cfg = LoopbackConfig(rate_bps=20e6, owd_ttis=10, queue_bytes=256 * 1024, mss=MSS)
    runner, sender, _, _ = build(size=size, cfg=cfg)
    runner.run_until(6000)
    assert sender.finished_at_tti is not None, "transfer did not finish"
    mbps = size * 8 / (sender.finished_at_tti / 1000.0) / 1e6
    assert 12.0 < mbps < 20.5, f"{mbps:.1f} Mbps is not close to the 20 Mbps link"


def test_rtt_estimate_matches_the_configured_delay():
    runner, sender, _, _ = build(size=200 * 1024)
    runner.run_until(3000)
    # 10 slots of propagation plus one slot of host turnaround, and whatever the
    # segment waited in the queue, which is why the upper bound is loose.
    assert sender.srtt_us is not None
    assert 11_000 <= sender.srtt_us <= 60_000, sender.srtt_us


def test_queue_overflow_causes_loss_recovery_not_a_stall():
    # A queue far smaller than the window forces drops, which the sender must
    # discover on its own and recover from.
    cfg = LoopbackConfig(rate_bps=5e6, owd_ttis=10, queue_bytes=16 * 1024, mss=MSS)
    runner, sender, receiver, link = build(size=300 * 1024, cfg=cfg)
    runner.run_until(20000)
    assert link.dropped > 0, "the test did not manage to drop anything"
    assert sender.stats.retransmits > 0
    assert sender.snd_una == 300 * 1024, "the transfer must complete despite losses"


def test_cubic_and_reno_both_complete():
    for cc in (Reno, Cubic):
        runner, sender, _, _ = build(cc_cls=cc, size=400 * 1024)
        runner.run_until(6000)
        assert sender.snd_una == 400 * 1024, cc.__name__


def test_acks_over_the_return_path():
    runner, sender, _, _ = build(size=200 * 1024, ack_over_link=True)
    runner.run_until(4000)
    assert sender.snd_una == 200 * 1024
    assert sender.srtt_us is not None and sender.srtt_us >= 20_000


def test_run_is_deterministic():
    traces = []
    for _ in range(2):
        runner, sender, _, _ = build(size=300 * 1024)
        runner.run_until(4000)
        traces.append([(a.tti, a.seq, a.fate) for a in runner.arrivals])
    assert traces[0] == traces[1]


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(list(globals().items())):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except AssertionError as exc:
                failed += 1
                print(f"FAIL {name}: {exc}")
    sys.exit(1 if failed else 0)
