# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The other two transports: the injection rule, and open-loop datagrams."""
import os
import sys

from fikore_transport.backend import (BackendConfig, DownloadCompleted,
                                      TransportBackend)
from fikore_transport.cc import Cubic
from fikore_transport.ideal import IdealReceiver, IdealSender, SharedWindow
from fikore_transport.link import LoopbackConfig, LoopbackLink
from fikore_transport.runner import Flow, Runner
from fikore_transport.udp import udp_flow

MSS = 1500


def link(rate_bps=20e6, queue=64 * 1024, owd=10, **kw):
    return LoopbackLink(LoopbackConfig(rate_bps=rate_bps, owd_ttis=owd, mss=MSS,
                                       queue_bytes=queue, **kw))


# -- the injection rule ------------------------------------------------------------

def test_ideal_transfer_completes_and_respects_the_window():
    lk = link()
    runner = Runner(lk)
    window = SharedWindow(128 * 1024)
    sender = IdealSender(1, runner.clock, runner.sched, MSS, window)
    runner.add_flow(Flow(sender, IdealReceiver(1, MSS, sender)))
    sender.app_write(400 * 1024)

    peak = 0
    for _ in range(3000):
        runner.tick()
        peak = max(peak, window.in_flight)
        if sender.complete():
            break
    assert sender.delivered == 400 * 1024
    assert peak <= 128 * 1024, f"the window was exceeded: {peak}"


def test_the_window_is_shared_between_objects_of_one_ue():
    lk = link()
    runner = Runner(lk)
    window = SharedWindow(128 * 1024)
    senders = []
    for i in range(1, 4):
        sender = IdealSender(i, runner.clock, runner.sched, MSS, window)
        runner.add_flow(Flow(sender, IdealReceiver(i, MSS, sender)))
        sender.app_write(100 * 1024)
        senders.append(sender)

    peak = 0
    for _ in range(3000):
        runner.tick()
        peak = max(peak, window.in_flight)
        if all(s.complete() for s in senders):
            break
    assert all(s.delivered == 100 * 1024 for s in senders)
    assert peak <= 128 * 1024, f"three objects together exceeded the window: {peak}"


def test_ideal_slot_share_is_fixed_and_not_iteration_ordered():
    runner = Runner(link())
    window = SharedWindow(12_000)
    senders = [IdealSender(i, runner.clock, runner.sched, MSS, window)
               for i in range(1, 4)]
    for sender in senders:
        sender.app_write(100_000)
    sent = [sum(item.size for item in sender.send_window()) for sender in senders]
    assert sorted(sent) == [3000, 4500, 4500], sent
    assert sum(sent) == window.limit


def test_ideal_window_rotates_when_fewer_packet_slots_than_flows():
    window = SharedWindow(3000)
    for flow in (1, 2, 3):
        window.register(flow)
    first = [window.share(0, MSS, flow) for flow in (1, 2, 3)]
    second = [window.share(1, MSS, flow) for flow in (1, 2, 3)]
    assert first == [1500, 1500, 0]
    assert second == [1500, 0, 1500]


def test_ideal_window_smaller_than_mss_sends_a_short_fragment():
    runner = Runner(link())
    window = SharedWindow(1000)
    sender = IdealSender(1, runner.clock, runner.sched, MSS, window)
    sender.app_write(500)
    assert [item.size for item in sender.send_window()] == [500]


def test_without_recovery_a_lossy_transfer_never_completes():
    lk = link(queue=16 * 1024)
    runner = Runner(lk)
    sender = IdealSender(1, runner.clock, runner.sched, MSS,
                         SharedWindow(128 * 1024), recover=False)
    runner.add_flow(Flow(sender, IdealReceiver(1, MSS, sender)))
    sender.app_write(300 * 1024)
    for _ in range(2000):
        runner.tick()
    assert lk.dropped > 0
    assert sender.delivered < 300 * 1024, "without recovery the bytes are gone"


def test_ideal_beats_tcp_by_wasting_the_radio():
    """The comparison the ideal transport exists for."""
    results = {}
    for transport in ("tcp", "ideal"):
        lk = link()
        backend = TransportBackend(lk, BackendConfig(
            mss=MSS, transport=transport, retain_request_history=True))
        backend.submit_request(0, "seg", 300 * 1024)
        finished = None
        for _ in range(400):
            step = backend.advance()
            done = [e for e in step.events if isinstance(e, DownloadCompleted)]
            if done:
                finished = done[0].time_s
                break
        sender = backend.requests[(0, "seg")].flow.sender
        results[transport] = (finished, lk.dropped, sender.stats.retransmits)

    assert all(r[0] is not None for r in results.values()), results
    assert results["ideal"][0] <= results["tcp"][0], "the injection rule is optimistic"
    assert results["ideal"][1] > results["tcp"][1] * 5, (
        "and it pays for it in wasted transmissions", results)


def test_cubic_curve_is_computed_in_packets_not_bytes():
    cubic = Cubic(mss=MSS, cwnd=100 * MSS, ssthresh=0)
    cubic._update(1.0)
    cubic._update(2.0)       # target = 100 + C * 1^3 = 100.4 packets
    assert 200 < cubic.cnt < 300, cubic.cnt

    below_last_max = Cubic(mss=MSS, cwnd=80 * MSS, ssthresh=0)
    below_last_max.w_last_max = 100
    below_last_max._update(1.0)
    assert below_last_max.origin_point == 100


# -- open loop ---------------------------------------------------------------------

def test_udp_rate_is_what_was_asked_for():
    lk = link(rate_bps=50e6)
    runner = Runner(lk)
    flow, source, sink = udp_flow(1, runner.clock, runner.sched, rate_mbps=10.0,
                                  duration_ttis=1000)
    runner.add_flow(flow)
    runner.run_until(1100)
    sent_mbps = source.stats.bytes_sent * 8 / 1.0 / 1e6
    assert 9.5 <= sent_mbps <= 10.5, sent_mbps
    assert sink.lost == 0, "an uncongested link should lose nothing"
    assert sink.received == source.stats.datagrams_sent


def test_udp_measures_delay_and_jitter():
    lk = link(rate_bps=50e6, owd=20)
    runner = Runner(lk)
    flow, _, sink = udp_flow(1, runner.clock, runner.sched, rate_mbps=5.0,
                             duration_ttis=500)
    runner.add_flow(flow)
    runner.run_until(600)
    report = sink.report()
    assert 20.0 <= report["owd_ms_median"] <= 25.0, report
    assert report["jitter_ms"] < 2.0, report


def test_udp_over_the_capacity_loses_and_reports_it():
    lk = link(rate_bps=5e6, queue=16 * 1024)
    runner = Runner(lk)
    flow, source, sink = udp_flow(1, runner.clock, runner.sched, rate_mbps=20.0,
                                  duration_ttis=500)
    runner.add_flow(flow)
    runner.run_until(700)
    assert sink.lost > 0
    assert 0.6 < sink.loss_ratio < 0.85, sink.report()
    assert sink.received + sink.lost == source.stats.datagrams_sent


def test_udp_counts_trailing_and_all_packet_losses():
    lk = link(rate_bps=5e6, queue=0)
    runner = Runner(lk)
    flow, source, sink = udp_flow(1, runner.clock, runner.sched, rate_mbps=1.0,
                                  duration_ttis=20)
    runner.add_flow(flow)
    runner.run_until(30)
    assert source.stats.datagrams_sent > 0
    assert sink.received == 0
    assert sink.lost == source.stats.datagrams_sent
    assert sink.loss_ratio == 1.0


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
