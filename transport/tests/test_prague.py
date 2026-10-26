# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Prague over the binding to the L4S reference implementation.

Skipped, loudly, when `prague/libpraguesim.so` is not built: a silent skip on the
one controller the L4S work is about would be worse than a failure.
"""
import os
import sys

from fikore_transport.backend import (BackendConfig, DownloadCompleted,
                                      TransportBackend)
from fikore_transport.cc import Cubic
from fikore_transport.cc_prague import Prague, PragueUnavailable
from fikore_transport.link import LoopbackConfig, LoopbackLink
from fikore_transport.runner import Flow, Runner
from fikore_transport.tcp import TcpReceiver, TcpSender

MSS = 1500


def bulk(cc_factory, ecn, ttis=4000, ce_threshold=2, rate_bps=50e6,
         queue_bytes=512 * 1024):
    """One saturating flow over one bottleneck, and what the receiver saw."""
    link = LoopbackLink(LoopbackConfig(rate_bps=rate_bps, owd_ttis=10, mss=MSS,
                                       queue_bytes=queue_bytes,
                                       ce_threshold_ttis=ce_threshold))
    runner = Runner(link)
    cc = cc_factory(mss=MSS)
    sender = TcpSender(1, cc, runner.clock, runner.sched, MSS, ecn=ecn,
                       rwnd=4 * 1024 * 1024)
    receiver = TcpReceiver(1, MSS)
    runner.add_flow(Flow(sender, receiver))
    sender.set_unlimited()
    runner.run_until(ttis)
    return {
        "mbps": receiver.bytes_received * 8 / (ttis / 1000) / 1e6,
        "srtt_ms": (sender.srtt_us or 0) / 1000.0,
        "ce": receiver.pkts_ce,
        "lost": receiver.pkts_lost,
        "retransmits": sender.stats.retransmits,
        "cc": cc,
    }


def test_the_binding_loads_and_starts_where_it_was_told_to():
    cc = Prague(mss=MSS)
    stats = cc.stats()
    assert stats["state"] == "init"
    assert cc.cwnd >= 10 * MSS, cc.cwnd
    # Ten segments over the reference RTT, not the reference's 100 kbps default.
    assert 4e6 < cc.pacing_rate_bps() < 6e6, cc.pacing_rate_bps()


def test_prague_answers_marks_with_the_window_and_not_with_losses():
    prague = bulk(Prague, "ect1")
    assert prague["ce"] > 0, "nothing was marked, so nothing was being tested"
    assert prague["lost"] == 0, prague
    assert prague["retransmits"] == 0, prague
    assert prague["cc"].stats()["alpha"] > 0, "the mark fraction never moved"


def test_prague_holds_the_queue_where_cubic_fills_it():
    """The L4S result, which is the reason any of this exists."""
    prague = bulk(Prague, "ect1")
    cubic = bulk(Cubic, "not-ect")

    # Same bottleneck, so the same throughput. What differs is the cost of it.
    assert prague["mbps"] > cubic["mbps"] * 0.95, (prague["mbps"], cubic["mbps"])
    assert prague["srtt_ms"] < cubic["srtt_ms"] / 2, (prague["srtt_ms"], cubic["srtt_ms"])
    assert cubic["retransmits"] > 100, "the classic flow was supposed to overflow"


def test_a_marked_path_and_an_unmarked_one_differ():
    """Prague without ECT(1) is a loss-driven flow, and should behave like one."""
    marked = bulk(Prague, "ect1")
    bleached = bulk(Prague, "not-ect")
    assert bleached["ce"] == 0
    assert bleached["srtt_ms"] > marked["srtt_ms"], (bleached, marked)


def test_prague_recovers_the_pipe_after_a_timeout():
    cc = Prague(mss=MSS)
    before = cc.cwnd
    cc.on_rto()
    assert cc.cwnd <= before
    assert cc.stats()["state"] in ("init", "in_loss", "cong_avoid")


def test_prague_drives_the_backend_unchanged():
    link = LoopbackLink(LoopbackConfig(rate_bps=50e6, owd_ttis=10, mss=MSS,
                                       queue_bytes=512 * 1024, ce_threshold_ttis=2))
    backend = TransportBackend(link, BackendConfig(
        mss=MSS, cc_factory=Prague, ecn="ect1",
        retain_request_history=True))
    backend.submit_request(0, "seg-1", 512 * 1024)
    completed = None
    for _ in range(600):
        step = backend.advance()
        done = [e for e in step.events if isinstance(e, DownloadCompleted)]
        if done:
            completed = done[0]
            break
    assert completed is not None, "the object never finished"
    assert completed.request_id == "seg-1"
    assert backend.requests[(0, "seg-1")].flow.receiver.rcv_nxt == 512 * 1024


def test_the_binding_is_deterministic():
    first = bulk(Prague, "ect1", ttis=1500)
    second = bulk(Prague, "ect1", ttis=1500)
    assert first["mbps"] == second["mbps"]
    assert first["ce"] == second["ce"]
    assert first["cc"].stats()["alpha"] == second["cc"].stats()["alpha"]


if __name__ == "__main__":
    try:
        Prague(mss=MSS)
    except PragueUnavailable as exc:
        print(f"SKIP all: {exc}")
        sys.exit(0)
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
