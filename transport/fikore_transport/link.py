# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The network boundary: what the transport model hands over, and what comes back.

Everything below this interface is the network's business (queueing, scheduling,
delay, loss, ECN marking). Everything above it is transport and application. The
two implementations are `LoopbackLink`, a deterministic Python bottleneck used to
develop and test without an emulator, and `FikoreLink`, which drives FikoRE.

The interface deliberately reports what happened to each segment and nothing else.
It never tells the sender that a segment was lost: only the receiver sees arrivals,
so the sender discovers loss the way TCP does, from acknowledgements and timers.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Literal, Protocol

Direction = Literal["dl", "ul"]
Ecn = Literal["not-ect", "ect0", "ect1"]
Fate = Literal["delivered", "dropped", "expired"]
# Why a segment was lost, when the network knows. "queue" is a buffer or an AQM
# asking the sender to slow down, "radio" is the link itself failing, "budget" is
# the packet outliving its delay budget. A sender that wants to tell "send less"
# from "the radio is bad" reads this; one that does not can ignore it.
Cause = Literal["", "queue", "radio", "budget"]


@dataclass(frozen=True)
class Transmit:
    """One segment handed to the network."""

    flow: int
    seq: int
    size: int
    direction: Direction = "dl"
    ecn: Ecn = "not-ect"
    # Travels with the segment and comes back in the acknowledgement, as the TCP
    # timestamp option does. It is what removes the retransmission ambiguity from
    # the RTT samples.
    ts_us: int = 0
    kind: Literal["data", "ack"] = "data"


@dataclass(frozen=True)
class Arrival:
    """Terminal outcome of one segment, stamped with the slot it happened in."""

    tti: int
    flow: int
    seq: int
    size: int
    fate: Fate
    ce: bool = False
    delivered_bytes: int = 0  # below `size` when part of the segment was lost
    ts_us: int = 0
    kind: Literal["data", "ack"] = "data"
    cause: Cause = ""         # empty when the segment was delivered


class Link(Protocol):
    mss: int

    def submit(self, items: list[Transmit], at_tti: int) -> None: ...

    def step(self, until_tti: int) -> list[Arrival]: ...

    def close(self) -> None: ...


@dataclass
class LoopbackConfig:
    rate_bps: float = 10e6
    owd_ttis: int = 10               # one-way propagation, each direction
    queue_bytes: int = 64 * 1024     # tail drop beyond this
    mss: int = 1500
    delay_budget_ttis: int | None = None
    ce_threshold_ttis: int | None = None  # mark ECT(1) traffic above this queue delay


class LoopbackLink:
    """A single FIFO bottleneck per direction, served at a fixed rate.

    Not a radio model and not trying to be one. Its job is to make the transport
    layer testable and its dynamics predictable: a known bandwidth-delay product, a
    known queue limit, and losses that happen exactly when the queue overflows.
    """

    def __init__(self, cfg: LoopbackConfig | None = None) -> None:
        self.cfg = cfg or LoopbackConfig()
        self.mss = self.cfg.mss
        self._now = 0
        self._pending: dict[int, list[Transmit]] = {}
        self._queue: dict[Direction, list[tuple[int, Transmit]]] = {"dl": [], "ul": []}
        self._queued_bytes: dict[Direction, int] = {"dl": 0, "ul": 0}
        self._in_flight: list[Arrival] = []
        self._credit: dict[Direction, float] = {"dl": 0.0, "ul": 0.0}
        self.dropped = 0

    def submit(self, items: list[Transmit], at_tti: int) -> None:
        if items:
            self._pending.setdefault(at_tti, []).extend(items)

    def step(self, until_tti: int) -> list[Arrival]:
        out: list[Arrival] = []
        while self._now <= until_tti:
            tti = self._now
            for t in self._pending.pop(tti, []):
                self._enqueue(tti, t)
            self._serve(tti)
            due_now = [a for a in self._in_flight if a.tti <= tti]
            if due_now:
                self._in_flight = [a for a in self._in_flight if a.tti > tti]
                out.extend(due_now)
            self._now += 1
        return out

    def close(self) -> None:
        pass

    def _enqueue(self, tti: int, t: Transmit) -> None:
        q = self._queue[t.direction]
        if self._queued_bytes[t.direction] + t.size > self.cfg.queue_bytes:
            self.dropped += 1
            self._in_flight.append(Arrival(tti, t.flow, t.seq, t.size, "dropped",
                                           ts_us=t.ts_us, kind=t.kind,
                                           cause="queue"))
            return
        q.append((tti, t))
        self._queued_bytes[t.direction] += t.size

    def _serve(self, tti: int) -> None:
        for direction in ("dl", "ul"):
            self._credit[direction] += self.cfg.rate_bps / 8.0 / 1000.0
            q = self._queue[direction]
            while q and self._credit[direction] >= q[0][1].size:
                enqueued_at, t = q.pop(0)
                self._credit[direction] -= t.size
                self._queued_bytes[direction] -= t.size
                waited = tti - enqueued_at
                budget = self.cfg.delay_budget_ttis
                if budget is not None and waited > budget:
                    self._in_flight.append(Arrival(tti, t.flow, t.seq, t.size, "expired",
                                                   ts_us=t.ts_us, kind=t.kind,
                                                   cause="budget"))
                    continue
                ce = (self.cfg.ce_threshold_ttis is not None
                      and t.ecn == "ect1"
                      and waited > self.cfg.ce_threshold_ttis)
                self._in_flight.append(Arrival(tti + self.cfg.owd_ttis, t.flow, t.seq,
                                               t.size, "delivered", ce=ce,
                                               delivered_bytes=t.size, ts_us=t.ts_us,
                                               kind=t.kind))
