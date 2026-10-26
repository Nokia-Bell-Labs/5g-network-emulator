# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Transfer with no transport protocol, for comparison against the modelled one.

Bytes are handed to the network up to a fixed window per UE, shared across the
objects in flight, and whatever the network loses is simply handed over again. No
congestion window, no acknowledgements, no round trip, no timers.

This exists to be compared against, not to be believed. It is strictly more
optimistic than any real sender in two ways that matter: it is told about a loss
the instant the network reports it, where a sender needs a round trip to suspect
one, and it re-sends everything at once, where a sender is limited by a window it
has just reduced. Running the same condition with this and with `tcp` is what
turns that difference from an argument into a measurement.
"""
from __future__ import annotations

from dataclasses import dataclass

from .clock import Clock, Scheduler
from .link import Arrival, Direction, Ecn, Transmit
from .tcp import SenderStats


class SharedWindow:
    """Bytes in flight allowed per UE, shared by every object on it.

    The window is per UE rather than per object, so the objects of a UE compete
    for it. Each sender may take an equal share of what is free in a slot, which
    is round robin without needing a rotation to be maintained.
    """

    def __init__(self, limit_bytes: int) -> None:
        self.limit = limit_bytes
        self.in_flight = 0
        self.active: set[int] = set()
        self._share_tti = -1
        self._shares: dict[int, int] = {}
        self._rotation = 0

    def register(self, flow: int) -> None:
        self.active.add(flow)

    def unregister(self, flow: int) -> None:
        self.active.discard(flow)

    def share(self, tti: int, mss: int, flow: int) -> int:
        # Allocate packet slots centrally at slot start. Recomputing from shrinking
        # free space made the result depend on Runner's flow iteration order; rounding
        # every equal share down could leave all flows at zero forever.
        if tti != self._share_tti:
            free = max(self.limit - self.in_flight, 0)
            flows = sorted(self.active)
            self._shares = {item: 0 for item in flows}
            if flows:
                packet_slots, residual_bytes = divmod(free, mss)
                slots, remainder = divmod(packet_slots, len(flows))
                for item in flows:
                    self._shares[item] = slots * mss
                start = self._rotation % len(flows)
                for offset in range(remainder):
                    self._shares[flows[(start + offset) % len(flows)]] += mss
                if residual_bytes:
                    residual_owner = (start + remainder) % len(flows)
                    self._shares[flows[residual_owner]] += residual_bytes
                    self._rotation = (residual_owner + 1) % len(flows)
                else:
                    self._rotation = (start + max(remainder, 1)) % len(flows)
            self._share_tti = tti
        return self._shares.get(flow, 0)

    def take(self, nbytes: int) -> None:
        self.in_flight += nbytes

    def release(self, nbytes: int) -> None:
        self.in_flight = max(self.in_flight - nbytes, 0)


class IdealSender:
    """Injects up to its share of the window and re-offers whatever is lost."""

    def __init__(self, flow: int, clock: Clock, sched: Scheduler, mss: int,
                 window: SharedWindow, direction: Direction = "dl",
                 ecn: Ecn = "not-ect", recover: bool = True) -> None:
        self.flow = flow
        self.clock = clock
        self.sched = sched
        self.mss = mss
        self.window = window
        self.direction = direction
        self.ecn = ecn
        self.recover = recover

        self.app_available = 0
        self.app_unlimited = False
        self.snd_nxt = 0          # bytes handed over, re-offered ones included
        self.delivered = 0
        self.outstanding = 0
        self.cancelled = False
        self.finished_at_tti: int | None = None
        self.stats = SenderStats()
        self.srtt_us: float | None = None   # no round trip is ever measured here
        self.last_rtt_us: int | None = None
        self._registered = False

    # -- application side ---------------------------------------------------------

    def app_write(self, nbytes: int) -> None:
        self.app_available += nbytes
        if not self._registered:
            self.window.register(self.flow)
            self._registered = True

    def set_unlimited(self) -> None:
        self.app_unlimited = True
        if not self._registered:
            self.window.register(self.flow)
            self._registered = True

    def app_cancel(self) -> int:
        discarded = self.app_available
        self.app_available = 0
        self.app_unlimited = False
        self.cancelled = True
        self._retire()
        return discarded

    def complete(self) -> bool:
        return (not self.app_unlimited and self.app_available == 0
                and self.outstanding == 0)

    @property
    def in_flight(self) -> int:
        return self.outstanding

    def _retire(self) -> None:
        if self._registered:
            self.window.unregister(self.flow)
            self._registered = False

    # -- sending ------------------------------------------------------------------

    def send_window(self) -> list[Transmit]:
        if self.cancelled:
            return []
        budget = self.window.share(self.clock.tti, self.mss, self.flow)
        out: list[Transmit] = []
        while budget > 0:
            available = self.mss if self.app_unlimited else self.app_available
            size = min(self.mss, available, budget)
            if size <= 0:
                break
            seq = self.snd_nxt
            self.snd_nxt += size
            if not self.app_unlimited:
                self.app_available -= size
            self.outstanding += size
            self.window.take(size)
            budget -= size
            self.stats.bytes_sent += size
            self.stats.segments_sent += 1
            out.append(Transmit(self.flow, seq, size, self.direction, self.ecn,
                                ts_us=self.clock.us))
        if self.complete() and self.finished_at_tti is None:
            self._retire()
        return out

    # -- feedback -----------------------------------------------------------------

    def on_outcome(self, arrival: Arrival) -> None:
        self.outstanding -= arrival.size
        self.window.release(arrival.size)
        if arrival.fate == "delivered":
            self.delivered += arrival.size
            self.stats.bytes_acked += arrival.size
            return
        if self.recover:
            # Recovery without a transport: whatever the network reports as lost
            # is offered again, with no delay and no penalty to the sending rate.
            self.app_available += arrival.size
            self.stats.retransmits += 1
        if self.complete():
            self._retire()


class IdealReceiver:
    """Counts what arrives and hands the outcome straight back to the sender."""

    def __init__(self, flow: int, mss: int, sender: IdealSender) -> None:
        self.flow = flow
        self.mss = mss
        self.sender = sender
        self.rcv_nxt = 0          # unique bytes delivered; nothing is ever resent
        self.pkts_received = 0
        self.pkts_ce = 0

    def on_arrival(self, arrival: Arrival):
        if arrival.fate == "delivered":
            self.rcv_nxt += arrival.size
            self.pkts_received += 1
            self.pkts_ce += 1 if arrival.ce else 0
        self.sender.on_outcome(arrival)
        return None               # there is no acknowledgement path to model
