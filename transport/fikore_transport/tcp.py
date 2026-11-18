# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The transport state machine: segmentation, acknowledgements, RTT, RTO, RACK-TLP.

The sender never learns that a segment was lost from an oracle. It learns from
acknowledgements and SACK blocks, inferring loss via RFC 8985 RACK-TLP and
RFC 6298 RTO, and validating congestion-window utilization via RFC 7661.
"""
from __future__ import annotations

import bisect
from collections import deque
from dataclasses import dataclass
from .cc import AckInfo, CongestionControl
from .clock import Clock, Scheduler
from .link import Arrival, Direction, Ecn, Transmit

MIN_RTO_US = 200_000      # Linux floor, not RFC 6298's 1 s: 1 s hides everything at 5G RTTs
MAX_RTO_US = 60_000_000
INITIAL_RTO_US = 1_000_000

# Linux floor on RFC 8985 Section 7.2 Probe Timeout (10 ms)
LINUX_MIN_PTO_US = 10_000
# RFC 8985 Section 6.2 Step 4 reordering-window persistence (16 loss recoveries)
RACK_REO_PERSIST = 16
# RFC 7661 Section 4.2 & 4.4 PipeACK Sampling Period floor (1 s) and NVP duration (5 min)
RFC7661_MIN_PSP_US = 1_000_000
RFC7661_NVP_DURATION_US = 300_000_000


def _rack_sent_after(t1: int, end_seq1: int, t2: int, end_seq2: int) -> bool:
    """RFC 8985 Section 6.1 ordering predicate breaking same-slot ties by sequence."""
    return t1 > t2 or (t1 == t2 and end_seq1 > end_seq2)


@dataclass(frozen=True)
class Ack:
    flow: int
    ack_seq: int          # next byte expected, cumulative
    ts_echo_us: int       # send instant of the segment that triggered it
    pkts_received: int = 0
    pkts_ce: int = 0
    pkts_lost: int = 0
    # Selective acknowledgement blocks, RFC 2018 (most-recently-updated block first).
    # Without them a burst of losses costs one round trip per lost segment, which no
    # current stack pays.
    sacks: tuple[tuple[int, int], ...] = ()
    # Duplicate SACK indicator and range (RFC 2883 / RFC 8985 Section 7.4.2).
    dsack: bool = False
    dsack_seq: tuple[int, int] | None = None


@dataclass
class _Sent:
    seq: int
    size: int
    sent_us: int
    rtx: int = 0
    sacked: bool = False
    lost: bool = False
    rtx_episode: int = -1


@dataclass
class SenderStats:
    bytes_sent: int = 0
    bytes_acked: int = 0
    segments_sent: int = 0
    retransmits: int = 0
    rto_events: int = 0
    fast_retransmits: int = 0
    partial_acks: int = 0
    ce_marks: int = 0
    tlp_probes: int = 0
    rack_reordering_seen: int = 0


class TcpSender:
    """One direction of one connection, driven slot by slot.

    `send_window()` is the only place segments are created: the runner calls it once
    per slot, after feedback and timers, so a segment is never emitted before the
    acknowledgements of that slot have been accounted for.
    """

    def __init__(self, flow: int, cc: CongestionControl, clock: Clock, sched: Scheduler,
                 mss: int, direction: Direction = "dl", ecn: Ecn = "not-ect",
                 pacing: str = "linux", rwnd: int = 4 * 1024 * 1024) -> None:
        self.flow = flow
        self.cc = cc
        self.clock = clock
        self.sched = sched
        self.mss = mss
        self.direction = direction
        self.ecn = ecn
        self.pacing = pacing
        # Without a receive window the window has no upper bound other than a loss,
        # and on a link with a deep buffer and no AQM that means the whole transfer
        # ends up in flight. Real senders are bounded by the peer's window and by
        # their own socket buffer, so the model is too.
        self.rwnd = rwnd

        self.snd_una = 0     # first byte not yet acknowledged
        self.snd_nxt = 0     # next byte to put on the wire; rewinds after a timeout
        self.snd_high = 0    # highest byte ever sent: where new data starts
        self.app_available = 0        # bytes the application has made available
        self.app_unlimited = False
        self.unacked: dict[int, _Sent] = {}
        self._order: list[int] = []        # unacked sequence numbers, ascending
        self._sacked_order: list[int] = []  # selectively acknowledged, ascending
        self.sacked_bytes = 0
        self.lost_bytes = 0
        self.rtx_pending: list[int] = []
        self._rtx_pending_set: set[int] = set()

        self.srtt_us: float | None = None
        self.rttvar_us: float = 0.0
        self.rto_us: int = INITIAL_RTO_US
        self.last_rtt_us: int | None = None
        self.min_rtt_us: int | None = None

        # RFC 8985 RACK-TLP state
        self.rack_xmit_ts_us: int = 0
        self.rack_end_seq: int = 0
        self.rack_rtt_us: int = 0
        self.fack_seq: int = 0
        self.reordering_seen: bool = False
        self.reo_wnd_mult: int = 1
        self.reo_wnd_persist: int = 0
        self._dsack_round_seq: int | None = None
        self._rack_reo_timer = None
        self._pto_timer = None
        self._rto_timer = None
        self._rto_deadline_tti: int | None = None
        self._tlp_in_flight: bool = False
        self._tlp_high_seq: int = 0
        self._tlp_is_retrans: bool = False
        self._tlp_sent_us: int = 0
        self._tlp_new_data_budget: int = 0
        self._rtt_sampled_since_tlp: bool = False
        self._rto_high_seq: int = 0

        # RFC 7661 cwnd validation, pipeACK sliding window, and Non-Validated Phase (NVP)
        self._cwnd_limited_until_seq: int = 0
        self._max_pipe_seen: int = 0
        self._pipe_ack_interval_start_us: int | None = None
        self._pipe_ack_accum_bytes: int = 0
        self._pipe_ack_samples: deque[tuple[int, int]] = deque()
        self._nvp_start_us: int | None = None
        self._idle_since_us: int | None = None

        self.dupacks = 0
        self.recovery_point: int | None = None   # snd_high when recovery started
        self.episode = 0                         # recovery episode, to resend once each
        self.stats = SenderStats()
        self.pkts_sent = 0
        self.finished_at_tti: int | None = None
        self.cancelled = False

    # -- application side ---------------------------------------------------------

    def app_write(self, nbytes: int) -> None:
        if nbytes > 0:
            self.finished_at_tti = None
        self.app_available += nbytes

    def set_unlimited(self) -> None:
        self.app_unlimited = True

    def finish_writes(self) -> None:
        """Stop an unlimited application without cancelling TCP recovery.

        The bytes already emitted remain a finite stream tail: ACK processing,
        retransmission timers and loss recovery continue until ``complete()``.
        """
        self.app_unlimited = False
        self._sync_timers()

    def app_cancel(self) -> int:
        """Stop offering data. Returns the bytes that will now never be sent.

        What is already in the network stays there: it has been scheduled, it
        occupies a queue, and it will be delivered or dropped on its own. That tail
        is the waste a cancellation costs, and the model knows its size exactly
        rather than estimating it.
        """
        discarded = self.app_available
        self.app_available = 0
        self.app_unlimited = False
        self.cancelled = True
        self.rtx_pending.clear()
        self._rtx_pending_set.clear()
        self._cancel_timers()
        return discarded

    def _cancel_timers(self) -> None:
        Scheduler.cancel(self._rto_timer)
        Scheduler.cancel(self._pto_timer)
        Scheduler.cancel(self._rack_reo_timer)
        self._rto_timer = None
        self._rto_deadline_tti = None
        self._pto_timer = None
        self._rack_reo_timer = None

    @property
    def pkts_distinct_sent(self) -> int:
        """Segments of the stream that have been sent at least once.

        Distinct, and not the count of transmissions, because it is subtracted from
        counts of distinct segments: the receiver reports what arrived and what is
        missing, both once per segment however many times it was sent, and the
        remainder is what is in flight.
        """
        return -(-self.snd_high // self.mss)

    @property
    def in_flight(self) -> int:
        """RFC 6675's pipe: sent, unacknowledged, not SACKed, and not marked lost.

        Discounting SACKed and RACK-lost bytes opens room in the window during
        recovery so retransmissions and new data can flow without artificial dupack
        window inflation. Maintained incrementally to stay O(1) on fat pipes.
        """
        return max(self.snd_nxt - self.snd_una - self.sacked_bytes - self.lost_bytes, 0)

    def complete(self) -> bool:
        return (not self.app_unlimited
                and self.app_available == 0
                and not self.unacked
                and self.snd_una == self.snd_nxt)

    # -- sending ------------------------------------------------------------------

    def send_window(self) -> list[Transmit]:
        """Everything this flow puts on the wire in this slot.

        Retransmissions come first and new data only once the rewound part of the
        stream has been resent.
        """
        if self.cancelled:
            return []

        budget = self._budget()
        # RFC 7661 Section 4.4.3: check whether an active 5-minute NVP has expired
        self._check_nvp_expiry()
        was_idle = not self.unacked and self._idle_since_us is not None

        out: list[Transmit] = []
        # One retransmission (or RFC 8985 Section 7.3 new-data TLP probe) always fits,
        # even with no room left in cwnd: a retransmission replaces lost pipe bytes,
        # and holding it back until the window opens would deadlock recovery.
        if self.rtx_pending or self.snd_nxt < self.snd_high:
            budget = max(budget, float(self.mss))
        if self._tlp_new_data_budget > 0:
            receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
            if receive_room > 0:
                budget = max(budget, float(min(self._tlp_new_data_budget, receive_room)))
            self._tlp_new_data_budget = 0

        while budget > 0:
            nxt = self._next_to_send()
            if nxt is None:
                break
            seq, size, is_rtx = nxt
            if budget < size:
                break
            if is_rtx:
                sent = self.unacked[seq]
                if sent.lost:
                    sent.lost = False
                    self.lost_bytes -= sent.size
                sent.rtx += 1
                sent.rtx_episode = self.episode
                sent.sent_us = self.clock.us
                self.stats.retransmits += 1
            else:
                self.unacked[seq] = _Sent(seq, size, self.clock.us)
                self._order.append(seq)
                if not self.app_unlimited:
                    self.app_available -= size
                self.stats.bytes_sent += size
                self.stats.segments_sent += 1
                self.snd_high = seq + size
            self.snd_nxt = max(self.snd_nxt, seq + size)
            self.pkts_sent += 1
            budget -= size
            out.append(Transmit(self.flow, seq, size, self.direction, self.ecn,
                                ts_us=self.clock.us))

        # RFC 7661 cwnd-limited tracking over the whole round [snd_una, snd_high]:
        # set when unsent application data remains blocked by cwnd/pacing (with rwnd
        # room open) or when in_flight fills cwnd within one MSS.
        still_has_app_data = self.app_unlimited or self.app_available > 0
        receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
        want = self.mss if self.app_unlimited else min(self.mss, self.app_available)
        if (still_has_app_data and receive_room >= max(want, 1)) or (
            self.in_flight + self.mss >= self.cc.cwnd
        ):
            self._cwnd_limited_until_seq = self.snd_high
            self._nvp_start_us = None

        self._max_pipe_seen = max(self._max_pipe_seen, self.in_flight)

        if out:
            if was_idle and self._idle_since_us is not None and self.clock.us > self._idle_since_us:
                self.cc.on_tx_start(self.clock.us - self._idle_since_us)
            self._idle_since_us = None
            if self._rto_deadline_tti is None:
                self._rto_deadline_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))
            self._sync_timers()
        return out

    def _next_to_send(self) -> tuple[int, int, bool] | None:
        while self.rtx_pending:
            seq = self.rtx_pending.pop(0)
            self._rtx_pending_set.discard(seq)
            sent = self.unacked.get(seq)
            if sent is not None and not sent.sacked:
                return (seq, sent.size, True)
        while self.snd_nxt < self.snd_high:
            sent = self.unacked.get(self.snd_nxt)
            if sent is None or sent.sacked:
                # Skip holes already retired or SACKed during post-RTO go-back-N walk
                at = bisect.bisect_right(self._order, self.snd_nxt)
                self.snd_nxt = self._order[at] if at < len(self._order) else self.snd_high
                continue
            return (self.snd_nxt, sent.size, True)
        receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
        available = self.mss if self.app_unlimited else min(self.mss, self.app_available)
        size = min(available, receive_room)
        return (self.snd_high, size, False) if size > 0 else None

    def _budget(self) -> float:
        """Bytes this slot may put into the network: window room, and pacing.

        Pacing is not a refinement here, it is a requirement of the quantised clock.
        A slot is the smallest instant that exists, so an unpaced sender hands its
        whole window over in one slot: 300 segments arriving together, which no real
        host connected to a 20 Mbps link can produce. The rate is Linux's, two times
        the window per round trip while probing and 1.25 afterwards.
        """
        congestion_room = max(self.cc.cwnd - self.in_flight, 0.0)
        # SACKed bytes leave RFC 6675's congestion-control pipe but still occupy
        # the receiver window until the cumulative acknowledgement advances.
        receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
        room = min(congestion_room, receive_room)
        rate = self.cc.pacing_rate_bps()
        if rate is None:
            if self.pacing == "off" or not self.srtt_us:
                return room
            ratio = 2.0 if self.cc.cwnd <= getattr(self.cc, "ssthresh", float("inf")) else 1.25
            rate = ratio * self.cc.cwnd * 8.0 / (self.srtt_us / 1e6)
        per_slot = max(rate / 8.0 / 1000.0, 2.0 * self.mss)
        return min(room, per_slot)

    # -- feedback -----------------------------------------------------------------

    def on_ack(self, ack: Ack) -> None:
        if ack.ack_seq < self.snd_una:
            return

        pipe_before = self.in_flight
        self._max_pipe_seen = max(self._max_pipe_seen, pipe_before)
        prev_fack = max(self.fack_seq, self.snd_una)

        newly_sacked = self._absorb_sacks(ack.sacks)
        # The echoed timestamp belongs to the segment that triggered this ack, so the
        # sample is unambiguous even when that segment was a retransmission.
        rtt_us = max(self.clock.us - ack.ts_echo_us, 0)
        if rtt_us > 0:
            self.min_rtt_us = (
                rtt_us if self.min_rtt_us is None else min(self.min_rtt_us, rtt_us)
            )
            self._rtt_sampled_since_tlp = True

        if self._dsack_round_seq is not None and ack.ack_seq >= self._dsack_round_seq:
            self._dsack_round_seq = None

        # RFC 8985 Section 6.2 Step 4: DSACK expands reordering window once per round
        # (without setting reordering_seen, which belongs strictly to Step 3).
        if ack.dsack and self._dsack_round_seq is None:
            self.reo_wnd_mult += 1
            self.reo_wnd_persist = RACK_REO_PERSIST
            self._dsack_round_seq = self.snd_nxt

        # RFC 8985 Section 7.4: TLP episode resolution & masked-loss detection
        if self._tlp_in_flight:
            if not self._tlp_is_retrans and ack.ack_seq >= self._tlp_high_seq:
                self._tlp_in_flight = False
            elif ack.dsack and ack.dsack_seq is not None and ack.dsack_seq[1] == self._tlp_high_seq:
                # Section 7.4.2: DSACK matching the TLP probe confirms spurious PTO
                self._tlp_in_flight = False
            elif ack.ack_seq > self._tlp_high_seq:
                # Section 7.4.2: cumulative ACK past TLP.high_seq without matching DSACK
                # proves the probe masked a genuine tail loss.
                self._tlp_in_flight = False
                if self.recovery_point is None:
                    self._enter_congestion_event(pipe_before)
                    self.cc.on_recovered()

        if ack.ack_seq == self.snd_una:
            if not self.unacked:
                return
            self.dupacks += 1
            self._rack_advance_from_sacks(newly_sacked, ack.ts_echo_us, rtt_us, ack.dsack, prev_fack)
            self._rack_detect_loss_and_arm_timer(pipe_before)
            self._sync_timers()
            return

        newly = ack.ack_seq - self.snd_una
        cut = bisect.bisect_left(self._order, ack.ack_seq)
        acked_entries: list[_Sent] = []
        for seq in self._order[:cut]:
            sent = self.unacked.pop(seq, None)
            if sent is not None:
                acked_entries.append(sent)
                if sent.sacked:
                    self.sacked_bytes -= sent.size
                if sent.lost:
                    self.lost_bytes -= sent.size
                if seq in self._rtx_pending_set:
                    self._rtx_pending_set.discard(seq)
        del self._order[:cut]
        del self._sacked_order[:bisect.bisect_left(self._sacked_order, ack.ack_seq)]

        was_below_rto_high = (self.snd_una < self._rto_high_seq)
        self.snd_una = ack.ack_seq
        self.snd_nxt = max(self.snd_nxt, self.snd_una)
        self.stats.bytes_acked += newly
        self.stats.ce_marks = ack.pkts_ce

        # RFC 7323 Section 4.1 RTTM rule: update SRTT/RTTVAR/RTO only when SND.UNA advances
        self._update_rtt(rtt_us)
        self._rack_advance_from_acked(
            acked_entries, newly_sacked, ack.ts_echo_us, rtt_us, ack.dsack, prev_fack
        )

        # Decrement reo_wnd_persist when RTO recovery completes (RFC 8985 Section 6.2 Step 4)
        if was_below_rto_high and self.snd_una >= self._rto_high_seq and self.reo_wnd_persist > 0:
            self.reo_wnd_persist -= 1
            if self.reo_wnd_persist == 0:
                self.reo_wnd_mult = 1

        in_loss_recovery = (self.recovery_point is not None) or (self.snd_una < self._rto_high_seq)
        if self.recovery_point is not None:
            if ack.ack_seq < self.recovery_point:
                self.stats.partial_acks += 1
                self._rack_detect_loss_and_arm_timer(pipe_before)
            else:
                self.cc.on_recovered()
                self.recovery_point = None
                self.dupacks = 0
                if self.reo_wnd_persist > 0:
                    self.reo_wnd_persist -= 1
                    if self.reo_wnd_persist == 0:
                        self.reo_wnd_mult = 1
                self._rack_detect_loss_and_arm_timer(pipe_before)
        else:
            self.dupacks = 0
            self._rack_detect_loss_and_arm_timer(pipe_before)

        # RFC 7661 Section 4.2: update pipeACK only outside loss recovery
        if not in_loss_recovery and self.recovery_point is None:
            self._record_pipe_ack(newly)

        is_cwnd_limited = (
            (self._cwnd_limited_until_seq > 0 and ack.ack_seq <= self._cwnd_limited_until_seq)
            or (pipe_before + self.mss >= self.cc.cwnd)
            or (
                self.cc.cwnd < getattr(self.cc, "ssthresh", float("inf"))
                and (self.app_unlimited or self.app_available > 0)
                and self.cc.cwnd < 2.0 * max(self._max_pipe_seen, pipe_before)
            )
        )
        self._update_nvp_state(is_cwnd_limited)

        self.cc.on_ack(AckInfo(
            acked_bytes=newly,
            rtt_us=rtt_us,
            now_us=self.clock.us,
            pkts_received=ack.pkts_received,
            pkts_ce=ack.pkts_ce,
            pkts_lost=ack.pkts_lost,
            pkts_sent=self.pkts_distinct_sent,
            is_cwnd_limited=is_cwnd_limited,
            snd_una=self.snd_una,
            snd_high=self.snd_high,
            paced=(self.pacing != "off"),
        ))

        if self.unacked:
            self._rto_deadline_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))
        else:
            self._rto_deadline_tti = None
            self._idle_since_us = self.clock.us
        self._sync_timers()

    def _update_rtt(self, sample_us: int) -> None:
        self.last_rtt_us = sample_us
        if self.srtt_us is None:
            self.srtt_us = float(sample_us)
            self.rttvar_us = sample_us / 2.0
        else:
            self.rttvar_us = 0.75 * self.rttvar_us + 0.25 * abs(self.srtt_us - sample_us)
            self.srtt_us = 0.875 * self.srtt_us + 0.125 * sample_us
        self.rto_us = int(min(max(self.srtt_us + 4 * self.rttvar_us, MIN_RTO_US), MAX_RTO_US))

    # -- RFC 7661 pipeACK and Non-Validated Phase (NVP) ---------------------------

    def _record_pipe_ack(self, newly_acked: int) -> None:
        if self.srtt_us is None or self.srtt_us <= 0:
            return
        now = self.clock.us
        if self._pipe_ack_interval_start_us is None:
            self._pipe_ack_interval_start_us = now
            self._pipe_ack_accum_bytes = newly_acked
            return
        self._pipe_ack_accum_bytes += newly_acked
        if now - self._pipe_ack_interval_start_us >= self.srtt_us:
            self._pipe_ack_samples.append((now, self._pipe_ack_accum_bytes))
            self._pipe_ack_interval_start_us = now
            self._pipe_ack_accum_bytes = 0
            psp_us = max(int(3.0 * self.srtt_us), RFC7661_MIN_PSP_US)
            while self._pipe_ack_samples and now - self._pipe_ack_samples[0][0] > psp_us:
                self._pipe_ack_samples.popleft()

    def _current_pipe_ack(self) -> int | None:
        if not self._pipe_ack_samples:
            return None
        now = self.clock.us
        srtt = int(self.srtt_us or 0)
        psp_us = max(3 * srtt, RFC7661_MIN_PSP_US)
        while self._pipe_ack_samples and now - self._pipe_ack_samples[0][0] > psp_us:
            self._pipe_ack_samples.popleft()
        if not self._pipe_ack_samples:
            return None
        return max(val for _, val in self._pipe_ack_samples)

    def _update_nvp_state(self, is_cwnd_limited: bool) -> None:
        pipe_ack = self._current_pipe_ack()
        if is_cwnd_limited or (pipe_ack is not None and pipe_ack > 0.5 * self.cc.cwnd):
            self._nvp_start_us = None
            return
        if pipe_ack is not None and pipe_ack <= 0.5 * self.cc.cwnd:
            if self._nvp_start_us is None:
                self._nvp_start_us = self.clock.us
            self._check_nvp_expiry()

    def _check_nvp_expiry(self) -> None:
        if (
            self._nvp_start_us is not None
            and self.clock.us - self._nvp_start_us >= RFC7661_NVP_DURATION_US
        ):
            self.cc.on_nvp_expired()
            self._nvp_start_us = None

    def _enter_congestion_event(self, loss_flight_bytes: int) -> None:
        self.cc.on_congestion_event(
            in_nvp=(self._nvp_start_us is not None),
            pipe_ack_bytes=self._current_pipe_ack(),
            loss_flight_bytes=loss_flight_bytes,
        )

    # -- RFC 8985 RACK helpers ----------------------------------------------------

    def _rack_eligible_sample(
        self, sent: _Sent, ts_echo_us: int, rtt_us: int, is_dsack: bool
    ) -> bool:
        """RFC 8985 Section 6.2 Step 2: accept original transmissions, or retransmitted
        segments whose echoed timestamp matches unambiguously with rtt >= min_rtt."""
        if sent.rtx == 0:
            return True
        if is_dsack or self.min_rtt_us is None:
            return False
        return sent.sent_us == ts_echo_us and rtt_us >= self.min_rtt_us

    def _rack_advance_from_sacks(
        self,
        newly_sacked: list[_Sent],
        ts_echo_us: int,
        rtt_us: int,
        is_dsack: bool,
        prev_fack: int,
    ) -> None:
        for sent in newly_sacked:
            end_seq = sent.seq + sent.size
            if sent.rtx == 0 and end_seq < prev_fack:
                if not self.reordering_seen:
                    self.stats.rack_reordering_seen += 1
                self.reordering_seen = True
            self.fack_seq = max(self.fack_seq, end_seq)
            if self._rack_eligible_sample(
                sent, ts_echo_us, rtt_us, is_dsack
            ) and _rack_sent_after(
                sent.sent_us, end_seq, self.rack_xmit_ts_us, self.rack_end_seq
            ):
                self.rack_xmit_ts_us = sent.sent_us
                self.rack_end_seq = end_seq
                self.rack_rtt_us = rtt_us

    def _rack_advance_from_acked(
        self,
        acked_entries: list[_Sent],
        newly_sacked: list[_Sent],
        ts_echo_us: int,
        rtt_us: int,
        is_dsack: bool,
        prev_fack: int,
    ) -> None:
        for sent in acked_entries:
            end_seq = sent.seq + sent.size
            if sent.rtx == 0 and not sent.sacked and end_seq <= prev_fack:
                if not self.reordering_seen:
                    self.stats.rack_reordering_seen += 1
                self.reordering_seen = True
            self.fack_seq = max(self.fack_seq, end_seq)
            if self._rack_eligible_sample(
                sent, ts_echo_us, rtt_us, is_dsack
            ) and _rack_sent_after(
                sent.sent_us, end_seq, self.rack_xmit_ts_us, self.rack_end_seq
            ):
                self.rack_xmit_ts_us = sent.sent_us
                self.rack_end_seq = end_seq
                self.rack_rtt_us = rtt_us
        self._rack_advance_from_sacks(
            newly_sacked, ts_echo_us, rtt_us, is_dsack, max(prev_fack, self.fack_seq)
        )

    def _rack_reo_wnd_us(self) -> int:
        """RFC 8985 Section 6.2 Step 4 reordering window (bounded by SRTT)."""
        if self.min_rtt_us is None:
            return 0
        srtt_cap = int(self.srtt_us) if self.srtt_us is not None else self.min_rtt_us
        if not self.reordering_seen and self.reo_wnd_persist == 0:
            if (
                self.recovery_point is not None
                or len(self._sacked_order) >= 3
                or self.dupacks >= 3
            ):
                return 0
            return min(self.min_rtt_us // 4, srtt_cap)
        return min((self.min_rtt_us // 4) * self.reo_wnd_mult, srtt_cap)

    def _rack_detect_loss_and_arm_timer(self, loss_flight_bytes: int) -> None:
        Scheduler.cancel(self._rack_reo_timer)
        self._rack_reo_timer = None
        if not self.unacked:
            return

        reo_wnd_us = self._rack_reo_wnd_us()
        # RFC 8985 Section 6.2 Step 5 arms a single reorder timer at the maximum
        # remaining wait across unacknowledged segments older than RACK.segment.
        max_wait_us: int | None = None
        newly_lost = False

        for seq in self._order:
            sent = self.unacked[seq]
            if sent.sacked or seq in self._rtx_pending_set:
                continue
            end_seq = sent.seq + sent.size
            if self.rack_rtt_us > 0 and _rack_sent_after(
                self.rack_xmit_ts_us, self.rack_end_seq, sent.sent_us, end_seq
            ):
                remaining_us = (
                    sent.sent_us + self.rack_rtt_us + reo_wnd_us - self.clock.us
                )
                if remaining_us <= 0:
                    newly_lost = True
                    self._mark_lost_and_queue(sent)
                else:
                    max_wait_us = (
                        remaining_us
                        if max_wait_us is None
                        else max(max_wait_us, remaining_us)
                    )
            elif (
                not self.reordering_seen
                and self.reo_wnd_persist == 0
                and self.dupacks >= 3
                and sent.rtx_episode != self.episode
            ):
                # RFC 8985 Section 6.2 classic dupack fallback when no reordering seen
                above = len(self._sacked_order) - bisect.bisect_right(
                    self._sacked_order, seq
                )
                if above >= 3 or (not self._sacked_order and seq == self.snd_una):
                    newly_lost = True
                    self._mark_lost_and_queue(sent)

        if newly_lost and self.recovery_point is None:
            if self.snd_una >= self._rto_high_seq:
                self.stats.fast_retransmits += 1
                self.recovery_point = self.snd_high
                self.episode += 1
                self._enter_congestion_event(loss_flight_bytes)
            else:
                self.episode += 1

        if max_wait_us is not None and not self.cancelled:
            fire_tti = self.clock.tti + max(1, -(-max_wait_us // 1000))
            self._rack_reo_timer = self.sched.at(
                int(fire_tti), self._on_rack_reo_timeout
            )

    def _on_rack_reo_timeout(self) -> None:
        self._rack_reo_timer = None
        if self.cancelled or not self.unacked:
            return
        self._rack_detect_loss_and_arm_timer(self.in_flight)
        self._sync_timers()

    def _mark_lost_and_queue(self, sent: _Sent) -> None:
        if not sent.lost and not sent.sacked:
            sent.lost = True
            self.lost_bytes += sent.size
        self._queue_retransmit(sent.seq)

    def _queue_retransmit(self, seq: int) -> None:
        if seq in self.unacked and seq not in self._rtx_pending_set:
            self.rtx_pending.append(seq)
            self._rtx_pending_set.add(seq)

    def _absorb_sacks(self, sacks: tuple[tuple[int, int], ...]) -> list[_Sent]:
        newly_sacked: list[_Sent] = []
        for start, end in sacks:
            lo = bisect.bisect_left(self._order, start)
            hi = bisect.bisect_left(self._order, end)
            for seq in self._order[lo:hi]:
                sent = self.unacked[seq]
                if not sent.sacked and sent.seq + sent.size <= end:
                    if sent.lost:
                        sent.lost = False
                        self.lost_bytes -= sent.size
                    sent.sacked = True
                    self.sacked_bytes += sent.size
                    bisect.insort(self._sacked_order, seq)
                    newly_sacked.append(sent)
        return newly_sacked

    # -- Mutually exclusive timer coordination (RFC 8985 Section 8) ---------------

    def _sync_timers(self) -> None:
        """Keep exactly one loss timer active among RACK reorder, TLP PTO, and RTO."""
        Scheduler.cancel(self._pto_timer)
        self._pto_timer = None
        Scheduler.cancel(self._rto_timer)
        self._rto_timer = None

        if self.cancelled or not self.unacked:
            Scheduler.cancel(self._rack_reo_timer)
            self._rack_reo_timer = None
            self._rto_deadline_tti = None
            return

        if self._rto_deadline_tti is None:
            self._rto_deadline_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))

        # 1. RACK reorder timer takes precedence while armed (RFC 8985 Section 8)
        if self._rack_reo_timer is not None:
            return

        # 2. TLP PTO timer when eligible (RFC 8985 Section 7.2 & 7.3)
        can_pto = (
            self.recovery_point is None
            and not self._tlp_in_flight
            and not self._sacked_order
            and self._rtt_sampled_since_tlp
            and not self.app_unlimited
        )
        if can_pto:
            # Allow PTO either when send buffer is empty or when cwnd blocks new data
            # while rwnd still has room for a 1-MSS new-data probe (Section 7.3).
            receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
            congestion_blocked = self.app_available > 0 and self.in_flight + self.mss > self.cc.cwnd and receive_room >= min(self.mss, self.app_available)
            if self.app_available == 0 or congestion_blocked:
                if self.srtt_us is not None:
                    pto_us = int(2.0 * self.srtt_us)
                    if len(self._order) <= 1:
                        pto_us += 2_000
                    pto_us = max(pto_us, LINUX_MIN_PTO_US)
                else:
                    pto_us = INITIAL_RTO_US
                fire_tti = self.clock.tti + max(1, -(-pto_us // 1000))
                # RFC 8985 Section 7.2: clamp PTO to remaining RTO rather than skipping
                fire_tti = max(self.clock.tti + 1, min(fire_tti, self._rto_deadline_tti))
                self._pto_timer = self.sched.at(int(fire_tti), self._on_pto)
                return

        # 3. Otherwise arm standard RFC 6298 RTO timer at _rto_deadline_tti
        rto_tti = max(self.clock.tti + 1, self._rto_deadline_tti)
        self._rto_timer = self.sched.at(int(rto_tti), self._on_rto)

    def _on_pto(self) -> None:
        self._pto_timer = None
        if (
            self.cancelled
            or not self.unacked
            or self.recovery_point is not None
            or self._tlp_in_flight
            or self._sacked_order
        ):
            self._sync_timers()
            return

        receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0)
        want = min(self.mss, self.app_available)
        if not self.app_unlimited and want > 0 and receive_room >= want:
            # RFC 8985 Section 7.3: transmit one new previously-unsent segment as probe
            self._tlp_new_data_budget = want
            self._tlp_in_flight = True
            self._tlp_high_seq = self.snd_high + want
            self._tlp_is_retrans = False
            self._tlp_sent_us = self.clock.us
        else:
            # RFC 8985 Section 7.3: retransmit highest-sequence sent segment
            tail_seq = self._order[-1]
            self._queue_retransmit(tail_seq)
            self._tlp_in_flight = True
            self._tlp_high_seq = tail_seq + self.unacked[tail_seq].size
            self._tlp_is_retrans = True
            self._tlp_sent_us = self.clock.us

        self._rtt_sampled_since_tlp = False
        self.stats.tlp_probes += 1
        # RFC 8985 Section 7.3: re-arm RTO from now as backup if the probe is also lost
        self._rto_deadline_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))
        self._sync_timers()

    def _on_rto(self) -> None:
        self._cancel_timers()
        self._tlp_in_flight = False
        if self.cancelled or not self.unacked:
            return
        self.stats.rto_events += 1
        self.rto_us = min(self.rto_us * 2, MAX_RTO_US)
        self.cc.on_rto()
        self._rto_high_seq = self.snd_high
        self.recovery_point = None
        self.dupacks = 0
        # RFC 6298 Section 5.4: clear stale RACK queue so earliest unacked is sent first
        self.rtx_pending.clear()
        self._rtx_pending_set.clear()
        for sent in self.unacked.values():
            if sent.lost:
                sent.lost = False
        self.lost_bytes = 0
        self.snd_nxt = self.snd_una
        self._rto_deadline_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))
        self._sync_timers()


class TcpReceiver:
    """Cumulative acknowledgements, RFC 2018 SACK ordering, DSACK, and ECN counters."""

    def __init__(self, flow: int, mss: int) -> None:
        self.flow = flow
        self.mss = mss
        self.rcv_nxt = 0
        self._blocks: list[tuple[int, int]] = []   # contiguous ranges held, sorted
        self._recent_sack: tuple[int, int] | None = None
        self.pkts_received = 0
        self.pkts_ce = 0
        self.bytes_received = 0
        self._highest_end = 0

    def on_arrival(self, arrival: Arrival) -> Ack | None:
        if arrival.fate != "delivered":
            return None
        start = arrival.seq
        end = arrival.seq + arrival.size
        is_duplicate = end <= self.rcv_nxt or any(
            s <= start and end <= e for s, e in self._blocks
        )
        dsack_seq = (start, end) if is_duplicate else None
        self.pkts_received += 1
        self.pkts_ce += 1 if arrival.ce else 0
        self.bytes_received += arrival.size
        self._highest_end = max(self._highest_end, end)
        self._absorb(start, end)
        # RFC 2018 Section 4: the block containing the segment that triggered this
        # ACK goes first, followed by remaining blocks.
        above = [b for b in self._blocks if b[0] > self.rcv_nxt]
        if self._recent_sack is not None and self._recent_sack in above:
            above.remove(self._recent_sack)
            above.insert(0, self._recent_sack)
        blocks = tuple(above[:3])
        return Ack(self.flow, self.rcv_nxt, arrival.ts_us,
                   self.pkts_delivered, self.pkts_ce, self.pkts_lost,
                   sacks=blocks, dsack=is_duplicate, dsack_seq=dsack_seq)

    @property
    def pkts_lost(self) -> int:
        """Segments below the highest one seen that are missing at this instant."""
        held_above = sum(e - s for s, e in self._blocks if s >= self.rcv_nxt)
        missing = self._highest_end - self.rcv_nxt - held_above
        return -(-missing // self.mss) if missing > 0 else 0

    @property
    def pkts_delivered(self) -> int:
        """Distinct segments that arrived, as opposed to arrivals."""
        return -(-self._highest_end // self.mss) - self.pkts_lost

    def _absorb(self, start: int, end: int) -> None:
        self._blocks.append((start, end))
        self._blocks.sort()
        merged: list[tuple[int, int]] = []
        recent: tuple[int, int] | None = None
        for s, e in self._blocks:
            if merged and s <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], e))
            else:
                merged.append((s, e))
            if merged[-1][0] <= start and end <= merged[-1][1]:
                recent = merged[-1]
        self._blocks = merged
        if self._blocks and self._blocks[0][0] <= self.rcv_nxt:
            self.rcv_nxt = self._blocks[0][1]
        self._recent_sack = recent if (recent and recent[0] > self.rcv_nxt) else None
