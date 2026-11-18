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


def test_rfc7661_app_limited_flow_does_not_inflate_cwnd_and_decays_on_idle():
    cfg = LoopbackConfig(rate_bps=50e6, owd_ttis=10, queue_bytes=256 * 1024, mss=MSS)
    runner, sender, _, _ = build(cc_cls=Cubic, cfg=cfg)
    # App-limited trickle: 1 MSS every 25 ms (well below IW10 = 15 KB per 21 ms RTT)
    for tti in range(500):
        if tti % 25 == 0:
            sender.app_write(MSS)
        runner.tick()
    assert sender.cc.cwnd == 10 * MSS, f"app-limited cwnd inflated to {sender.cc.cwnd}"

    # RFC 7661 Section 4.4: short ABR-like idle pause (< 5 min NVP) preserves cwnd,
    # whereas NVP expiry after 5 min caps cwnd without ever raising a small cwnd < IW10.
    sender.app_write(250 * MSS)
    for _ in range(400):
        runner.tick()
    grown_cwnd = sender.cc.cwnd
    assert grown_cwnd > 20 * MSS
    for _ in range(500):  # 0.5 s ABR chunk pause: cwnd preserved in NVP!
        runner.tick()
    sender.app_write(MSS)
    for _ in range(30):
        runner.tick()
    assert sender.cc.cwnd == grown_cwnd, "short ABR pause must preserve cwnd in NVP"

    # Simulate expiry of the 5-minute Non-Validated Period
    sender._nvp_start_us = sender.clock.us - 300_000_000
    sender._check_nvp_expiry()
    assert sender.cc.cwnd == max(grown_cwnd / 2.0, 10.0 * MSS)

    # Never increase cwnd when cwnd < IW10 (RFC 7661 Section 4.4)
    sender.cc.cwnd = 4.0 * MSS
    sender.cc.on_nvp_expired()
    assert sender.cc.cwnd == 4.0 * MSS


def test_slow_start_doubles_cwnd_per_rtt_when_cwnd_limited():
    cfg = LoopbackConfig(rate_bps=1000e6, owd_ttis=10, queue_bytes=8 * 1024 * 1024, mss=MSS)
    link = LoopbackLink(cfg)
    runner = Runner(link)
    cc = Reno(mss=MSS, cwnd=10 * MSS, hystart=False)
    sender = TcpSender(1, cc, runner.clock, runner.sched, MSS, pacing="off", rwnd=8 * 1024 * 1024)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))
    sender.set_unlimited()
    runner.run_until(115)  # ~5 RTTs at ~21 ms RTT: 10 -> 20 -> 40 -> 80 -> 160 -> 320 MSS
    assert sender.cc.cwnd >= 160 * MSS, f"slow start failed to double per RTT: {sender.cc.cwnd / MSS}"


def test_tlp_multi_segment_tail_loss_recovers_via_rack_without_rto():
    class DropTailBurstOnceLink(LoopbackLink):
        def __init__(self, drop_seqs: set[int]):
            super().__init__(LoopbackConfig(rate_bps=50e6, owd_ttis=10, queue_bytes=256 * 1024, mss=MSS))
            self.drop_seqs = set(drop_seqs)

        def submit(self, items, at_tti: int):
            kept = []
            for item in items:
                if item.kind == "data" and item.seq in self.drop_seqs:
                    self.drop_seqs.remove(item.seq)
                    self.dropped += 1
                    continue
                kept.append(item)
            if kept:
                super().submit(kept, at_tti)

    # First 5 segments establish SRTT; next 5 tail segments (seqs 5..9) are ALL dropped!
    # TLP probes seq 9, whose SACK advances RACK.xmit_ts (RFC 8985 §6.2 Step 2 & §7.4.1)
    # and triggers fast recovery of seqs 5..8 without waiting for RTO!
    lk = DropTailBurstOnceLink({i * MSS for i in range(5, 10)})
    runner = Runner(lk)
    sender = TcpSender(1, Cubic(mss=MSS, cwnd=10 * MSS), runner.clock, runner.sched, MSS)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))
    sender.app_write(5 * MSS)
    runner.run_until(30)
    sender.app_write(5 * MSS)
    runner.run_until(160)

    assert sender.snd_una == 10 * MSS
    assert sender.stats.tlp_probes >= 1
    assert sender.stats.rto_events == 0, "TLP SACK + RACK must recover tail burst before RTO"


def test_spurious_tlp_on_delayed_tail_does_not_reduce_cwnd():
    class DelayTailOnceLink(LoopbackLink):
        def __init__(self, target_seq: int):
            super().__init__(LoopbackConfig(rate_bps=40e6, owd_ttis=10, queue_bytes=256 * 1024, mss=MSS))
            self.target_seq = target_seq
            self.delayed = None
            self.done = False

        def step(self, until_tti: int):
            arrivals = super().step(until_tti)
            out = []
            for arr in arrivals:
                if arr.kind == "data" and arr.seq == self.target_seq and not self.done:
                    self.done = True
                    self.delayed = (until_tti + 60, arr)  # delayed 60 ms > PTO (~42 ms)
                else:
                    out.append(arr)
            if self.delayed and until_tti >= self.delayed[0]:
                out.append(self.delayed[1])
                self.delayed = None
            return out

    lk = DelayTailOnceLink(target_seq=5 * MSS)
    runner = Runner(lk)
    sender = TcpSender(1, Reno(mss=MSS, cwnd=10 * MSS, hystart=False), runner.clock, runner.sched, MSS)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))
    sender.app_write(6 * MSS)
    runner.run_until(160)

    assert sender.snd_una == 6 * MSS
    assert sender.stats.tlp_probes == 1
    assert sender.cc.ssthresh == float("inf"), "spurious TLP must not cut cwnd/ssthresh"
    assert not sender.reordering_seen, "DSACK alone must not set reordering_seen (Step 3)"


def test_rack_tolerates_reordering_after_detection_without_spurious_cuts():
    class ReorderingLink(LoopbackLink):
        def __init__(self):
            super().__init__(LoopbackConfig(rate_bps=40e6, owd_ttis=15, queue_bytes=512 * 1024, mss=MSS))
            self._held = []

        def step(self, until_tti: int):
            arrivals = super().step(until_tti)
            out = []
            for arr in arrivals:
                # Delay every 12th data segment by 4 ms (less than min_rtt/4 = ~7.5 ms)
                if arr.kind == "data" and arr.fate == "delivered" and (arr.seq // MSS) % 12 == 5:
                    self._held.append((until_tti + 4, arr))
                else:
                    out.append(arr)
            still_held = []
            for due_tti, arr in self._held:
                if until_tti >= due_tti:
                    out.append(arr)
                else:
                    still_held.append((due_tti, arr))
            self._held = still_held
            return out

    lk = ReorderingLink()
    runner = Runner(lk)
    sender = TcpSender(1, Cubic(mss=MSS, cwnd=10 * MSS), runner.clock, runner.sched, MSS)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))
    sender.app_write(300 * MSS)
    runner.run_until(2500)

    assert sender.snd_una == 300 * MSS
    assert sender.reordering_seen or sender.reo_wnd_persist > 0
    # At most 2 pre-learning retransmits (in flight before the first DSACK arrives)
    # out of 25 reordering events across the 300-segment transfer; zero after learning!
    assert sender.stats.retransmits <= 2, f"spurious retransmits under RACK: {sender.stats.retransmits}"


def test_tlp_recovers_dropped_tail_segment_without_rto_collapse():
    class DropLastSegmentOnceLink(LoopbackLink):
        def __init__(self, target_seq: int):
            super().__init__(LoopbackConfig(rate_bps=40e6, owd_ttis=10, queue_bytes=256 * 1024, mss=MSS))
            self.target_seq = target_seq
            self.dropped_once = False

        def submit(self, items, at_tti: int):
            kept = []
            for item in items:
                if item.kind == "data" and item.seq == self.target_seq and not self.dropped_once:
                    self.dropped_once = True
                    self.dropped += 1
                    continue
                kept.append(item)
            if kept:
                super().submit(kept, at_tti)

    total_pkts = 10
    lk = DropLastSegmentOnceLink(target_seq=(total_pkts - 1) * MSS)
    runner = Runner(lk)
    sender = TcpSender(1, Cubic(mss=MSS, cwnd=10 * MSS), runner.clock, runner.sched, MSS)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))
    sender.app_write(total_pkts * MSS)
    runner.run_until(400)

    assert sender.snd_una == total_pkts * MSS
    assert sender.stats.tlp_probes == 1
    assert sender.stats.rto_events == 0, "TLP should repair tail drop before RTO fires"
    assert sender.finished_at_tti is not None and sender.finished_at_tti < 150


def test_hystart_plus_plus_exits_slow_start_without_packet_loss():
    cfg = LoopbackConfig(rate_bps=16e6, owd_ttis=15, queue_bytes=640 * 1024, mss=MSS)
    runner, sender, _, lk = build(cc_cls=Cubic, size=1200 * 1024, cfg=cfg)
    runner.run_until(5000)

    assert sender.snd_una == 1200 * 1024
    assert lk.dropped == 0, f"HyStart++ should exit before overflowing deep buffer, dropped={lk.dropped}"
    assert sender.cc.ssthresh < float("inf"), "HyStart++ must set finite ssthresh"


def test_post_rto_slow_start_caps_abc_and_never_overshoots_ssthresh():
    clock = Clock()
    cc = Cubic(mss=MSS, cwnd=4 * MSS, beta=0.3)
    sender = TcpSender(1, cc, clock, Scheduler(clock), MSS)
    sender.app_write(10 * MSS)
    sender.send_window()  # sends 4 segments [0, 6000)
    clock.tti = 20
    # Force an RTO from open state: ssthresh = 0.7 * 6000 = 4200 B (2.8 MSS), cwnd = 1 MSS (1500 B)
    sender._on_rto()
    assert abs(cc.ssthresh - 2.8 * MSS) < 1e-6 and cc.cwnd == 1 * MSS
    sender.send_window()  # retransmits seq 0
    clock.tti = 40
    # Cumulative ACK for all 4 segments (6000 B) arrives at once:
    # 1) ABC L=2*MSS caps raw growth to +2 MSS (3.0 MSS = 4500 B), not +4 MSS, AND
    # 2) min(cwnd + step, ssthresh) clamps cwnd to ssthresh (2.8 MSS = 4200 B) without overshoot!
    sender.on_ack(Ack(1, 4 * MSS, 20_000))
    assert abs(cc.cwnd - 2.8 * MSS) < 1e-6
    assert cc.cwnd <= cc.ssthresh

    # Verify ABC L=2*MSS cap directly when ssthresh is higher (7 MSS):
    cc2 = Cubic(mss=MSS, cwnd=10 * MSS, beta=0.3)
    sender2 = TcpSender(2, cc2, clock, Scheduler(clock), MSS)
    sender2.app_write(10 * MSS)
    sender2.send_window()
    clock.tti = 60
    sender2._on_rto()
    assert abs(cc2.ssthresh - 7 * MSS) < 1e-6 and cc2.cwnd == 1 * MSS
    sender2.send_window()
    clock.tti = 80
    sender2.on_ack(Ack(2, 10 * MSS, 60_000))
    assert abs(cc2.cwnd - 3 * MSS) < 1e-6


def test_cubic_excludes_idle_time_from_epoch_on_tx_start():
    cfg = LoopbackConfig(rate_bps=50e6, owd_ttis=10, queue_bytes=512 * 1024, mss=MSS)
    link = LoopbackLink(cfg)
    runner = Runner(link)
    cc = Cubic(mss=MSS, cwnd=25 * MSS, ssthresh=25 * MSS)
    sender = TcpSender(1, cc, runner.clock, runner.sched, MSS)
    runner.add_flow(Flow(sender, TcpReceiver(1, MSS)))

    sender.app_write(50 * MSS)
    runner.run_until(200)
    cwnd_before_idle = cc.cwnd
    # Sit completely idle for 5 seconds (5000 TTIs), then send 25 MSS over 100 ms
    runner.run_until(5200)
    sender.app_write(25 * MSS)
    runner.run_until(5300)
    # Without on_tx_start epoch shift, W_cubic(t=5.1s) would jump cwnd by ~20+ MSS!
    assert cc.cwnd < cwnd_before_idle + 3 * MSS, (cwnd_before_idle, cc.cwnd)


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
