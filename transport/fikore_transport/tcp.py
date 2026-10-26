# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The transport state machine: segmentation, acknowledgements, RTT, RTO, retransmission.

The sender never learns that a segment was lost. It learns that acknowledgements
stopped arriving, or that three of them repeated, which is the same information a
real sender has and the reason the dynamics come out right.
"""
from __future__ import annotations

import bisect
from dataclasses import dataclass

from .cc import AckInfo, CongestionControl
from .clock import Clock, Scheduler
from .link import Arrival, Direction, Ecn, Transmit

MIN_RTO_US = 200_000      # Linux floor, not RFC 6298's 1 s: 1 s hides everything at 5G RTTs
MAX_RTO_US = 60_000_000
INITIAL_RTO_US = 1_000_000


@dataclass(frozen=True)
class Ack:
    flow: int
    ack_seq: int          # next byte expected, cumulative
    ts_echo_us: int       # send instant of the segment that triggered it
    pkts_received: int = 0
    pkts_ce: int = 0
    pkts_lost: int = 0
    # Selective acknowledgement blocks, RFC 2018. Without them a burst of losses
    # costs one round trip per lost segment, which no current stack pays.
    sacks: tuple[tuple[int, int], ...] = ()


@dataclass
class _Sent:
    seq: int
    size: int
    sent_us: int
    rtx: int = 0
    sacked: bool = False
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
        self.rtx_pending: list[int] = []

        self.srtt_us: float | None = None
        self.rttvar_us: float = 0.0
        self.rto_us: int = INITIAL_RTO_US
        self.last_rtt_us: int | None = None
        self.dupacks = 0
        self.recovery_point: int | None = None   # snd_high when recovery started
        self.episode = 0                         # recovery episode, to resend once each
        self._rto_timer = None
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
        Scheduler.cancel(self._rto_timer)
        self._rto_timer = None
        return discarded

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
        """RFC 6675's pipe: sent, not acknowledged and not selectively acknowledged.

        Discounting the selectively acknowledged bytes is what opens room in the
        window during recovery, so retransmissions and new data can flow while the
        holes are being repaired. Maintained incrementally: a window of thousands of
        segments is normal on a fat pipe, and recomputing this per acknowledgement
        is what turns the model quadratic.
        """
        return max(self.snd_nxt - self.snd_una - self.sacked_bytes, 0)

    def complete(self) -> bool:
        return (not self.app_unlimited
                and self.app_available == 0
                and not self.unacked
                and self.snd_una == self.snd_nxt)

    # -- sending ------------------------------------------------------------------

    def send_window(self) -> list[Transmit]:
        """Everything this flow puts on the wire in this slot.

        Retransmissions come first and new data only once the rewound part of the
        stream has been resent, which is the ordering a sender without SACK has.
        """
        if self.cancelled:
            return []
        out: list[Transmit] = []
        budget = self._budget()
        # One retransmission always fits, even with no room left in the window: it
        # replaces bytes already counted as in flight rather than adding new ones,
        # and holding it back until the window opens is what deadlocks a recovery.
        if self.rtx_pending or self.snd_nxt < self.snd_high:
            budget = max(budget, float(self.mss))
        while budget > 0:
            nxt = self._next_to_send()
            if nxt is None:
                break
            seq, size, is_rtx = nxt
            if budget < size:
                break
            if is_rtx:
                sent = self.unacked[seq]
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

        if out:
            self._arm_rto()
        return out

    def _next_to_send(self) -> tuple[int, int, bool] | None:
        while self.rtx_pending:
            seq = self.rtx_pending.pop(0)
            sent = self.unacked.get(seq)
            if sent is not None and not sent.sacked:
                return (seq, sent.size, True)
        if self.snd_nxt < self.snd_high:
            sent = self.unacked.get(self.snd_nxt)
            if sent is None:
                at = bisect.bisect_right(self._order, self.snd_nxt)
                self.snd_nxt = self._order[at] if at < len(self._order) else self.snd_high
                return self._next_to_send()
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
        receive_room = max(self.snd_una + self.rwnd - self.snd_high, 0.0)
        room = min(congestion_room, receive_room)
        rate = self.cc.pacing_rate_bps()
        if rate is None:
            if self.pacing == "off" or self.srtt_us is None:
                return room
            ratio = 2.0 if self.cc.cwnd <= getattr(self.cc, "ssthresh", float("inf")) else 1.25
            rate = ratio * self.cc.cwnd * 8.0 / (self.srtt_us / 1e6)
        per_slot = max(rate / 8.0 / 1000.0, 2.0 * self.mss)
        return min(room, per_slot)

    # -- feedback -----------------------------------------------------------------

    def on_ack(self, ack: Ack) -> None:
        if ack.ack_seq < self.snd_una:
            return
        self._absorb_sacks(ack.sacks)
        if ack.ack_seq == self.snd_una and self.unacked:
            self.dupacks += 1
            self.cc.on_dupacks(self.dupacks)
            if self.dupacks == 3:
                self.stats.fast_retransmits += 1
                self.recovery_point = self.snd_high
                self.episode += 1
                self._queue_sack_holes()
            elif self.dupacks > 3:
                self._queue_sack_holes()
            return

        newly = ack.ack_seq - self.snd_una
        self.snd_una = ack.ack_seq
        # A cumulative ack can jump over the point the resend walk had reached.
        self.snd_nxt = max(self.snd_nxt, self.snd_una)
        self.stats.bytes_acked += newly
        self.stats.ce_marks = ack.pkts_ce

        cut = bisect.bisect_left(self._order, ack.ack_seq)
        for seq in self._order[:cut]:
            sent = self.unacked.pop(seq, None)
            if sent is not None and sent.sacked:
                self.sacked_bytes -= sent.size
        del self._order[:cut]
        del self._sacked_order[:bisect.bisect_left(self._sacked_order, ack.ack_seq)]

        if self.recovery_point is not None:
            if ack.ack_seq < self.recovery_point:
                # Partial ack, RFC 6582: a hole was filled and the next one is now at
                # snd_una. Recovery continues, so that several losses in one window
                # do not cost one timeout each.
                self.stats.partial_acks += 1
                self._queue_sack_holes()
            else:
                self.cc.on_recovered()
                self.recovery_point = None
                self.dupacks = 0
        else:
            self.dupacks = 0

        # The echoed timestamp belongs to the segment that triggered this ack, so the
        # sample is unambiguous even when that segment was a retransmission. That is
        # what the timestamp is for, and why Karn's algorithm is not needed here.
        rtt_us = max(self.clock.us - ack.ts_echo_us, 0)
        self._update_rtt(rtt_us)

        self.cc.on_ack(AckInfo(acked_bytes=newly, rtt_us=rtt_us, now_us=self.clock.us,
                               pkts_received=ack.pkts_received, pkts_ce=ack.pkts_ce,
                               pkts_lost=ack.pkts_lost, pkts_sent=self.pkts_distinct_sent))
        self._arm_rto(restart=True)

    def _update_rtt(self, sample_us: int) -> None:
        self.last_rtt_us = sample_us
        if self.srtt_us is None:
            self.srtt_us = float(sample_us)
            self.rttvar_us = sample_us / 2.0
        else:
            self.rttvar_us = 0.75 * self.rttvar_us + 0.25 * abs(self.srtt_us - sample_us)
            self.srtt_us = 0.875 * self.srtt_us + 0.125 * sample_us
        self.rto_us = int(min(max(self.srtt_us + 4 * self.rttvar_us, MIN_RTO_US), MAX_RTO_US))

    def _queue_retransmit(self, seq: int) -> None:
        if seq in self.unacked and seq not in self.rtx_pending:
            self.rtx_pending.append(seq)

    def _absorb_sacks(self, sacks: tuple[tuple[int, int], ...]) -> None:
        for start, end in sacks:
            lo = bisect.bisect_left(self._order, start)
            hi = bisect.bisect_left(self._order, end)
            for seq in self._order[lo:hi]:
                sent = self.unacked[seq]
                if not sent.sacked and sent.seq + sent.size <= end:
                    sent.sacked = True
                    self.sacked_bytes += sent.size
                    bisect.insort(self._sacked_order, seq)

    def _queue_sack_holes(self) -> None:
        """Retransmit what the receiver has not reported below its highest block.

        Each segment is resent at most once per recovery episode: a hole still
        unacknowledged after its retransmission was itself lost, and that case
        belongs to the timer rather than to another round of the same guess.
        """
        for seq in self._order:
            sent = self.unacked[seq]
            if sent.sacked or sent.rtx_episode == self.episode:
                continue
            # RFC 6675 IsLost(): a segment counts as lost once three segments above
            # it have been selectively acknowledged. Fewer than that is reordering,
            # and resending on reordering is how a sender ends up retransmitting
            # more bytes than the network ever dropped.
            above = len(self._sacked_order) - bisect.bisect_right(self._sacked_order, seq)
            if above >= 3:
                self._queue_retransmit(seq)
            elif not self._sacked_order and seq == self.snd_una:
                self._queue_retransmit(seq)
            else:
                break

    # -- retransmission timer -----------------------------------------------------

    def _arm_rto(self, restart: bool = False) -> None:
        """RFC 6298 5.1-5.3: one timer, restarted on a new ack and on expiry.

        It measures from now, not from the send instant of the oldest segment. That
        distinction matters: measuring from the send instant makes the deadline fall
        in the past as soon as one timeout has happened, and the sender then times
        out once per slot instead of once per RTO.
        """
        if self.cancelled or not self.unacked:
            Scheduler.cancel(self._rto_timer)
            self._rto_timer = None
            return
        if self._rto_timer is not None and not restart:
            return
        Scheduler.cancel(self._rto_timer)
        fire_tti = self.clock.tti + max(1, -(-self.rto_us // 1000))
        self._rto_timer = self.sched.at(int(fire_tti), self._on_rto)

    def _on_rto(self) -> None:
        self._rto_timer = None
        if self.cancelled or not self.unacked:
            return
        self.stats.rto_events += 1
        self.rto_us = min(self.rto_us * 2, MAX_RTO_US)
        self.cc.on_rto()
        self.dupacks = 0
        # The timeout says the pipe is empty, so the estimate of what is in flight is
        # reset and the stream is resent from the first unacknowledged byte. Without
        # that rewind a window of one segment could never be sent.
        self.snd_nxt = self.snd_una
        self._arm_rto(restart=True)


class TcpReceiver:
    """Cumulative acknowledgements and the counters ECN-based control needs."""

    def __init__(self, flow: int, mss: int) -> None:
        self.flow = flow
        self.mss = mss
        self.rcv_nxt = 0
        self._blocks: list[tuple[int, int]] = []   # contiguous ranges held
        self.pkts_received = 0
        self.pkts_ce = 0
        self.bytes_received = 0
        self._highest_end = 0

    def on_arrival(self, arrival: Arrival) -> Ack | None:
        if arrival.fate != "delivered":
            return None
        self.pkts_received += 1
        self.pkts_ce += 1 if arrival.ce else 0
        self.bytes_received += arrival.size
        self._highest_end = max(self._highest_end, arrival.seq + arrival.size)
        self._absorb(arrival.seq, arrival.seq + arrival.size)
        # The ranges above the contiguous prefix are exactly the selective
        # acknowledgement, and the receiver already had to track them to know what
        # it holds. Three of them, as the option's 40 bytes allow.
        blocks = tuple(b for b in self._blocks if b[0] > self.rcv_nxt)[:3]
        return Ack(self.flow, self.rcv_nxt, arrival.ts_us,
                   self.pkts_delivered, self.pkts_ce, self.pkts_lost, sacks=blocks)

    # A window-based controller infers loss from what is missing at the sender. One
    # driven by counters needs the receiver to say it, because `sent - received -
    # lost` is how it works out what is still in flight, and a wrong answer there is
    # a sender that stalls or overshoots.
    #
    # Both counts are of distinct segments and are derived from what the receiver
    # holds right now, rather than accumulated as events. That is what makes them
    # right under both of the things that happen here: a hole filled by a
    # retransmission stops being lost, and so does one filled by a packet that was
    # merely late. Counting the gaps as they were noticed, and never taking them
    # back, overstated loss by every reordering the emulator produced.

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
        for s, e in self._blocks:
            if merged and s <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], e))
            else:
                merged.append((s, e))
        self._blocks = merged
        if self._blocks and self._blocks[0][0] <= self.rcv_nxt:
            self.rcv_nxt = self._blocks[0][1]
