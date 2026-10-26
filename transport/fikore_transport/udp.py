# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Open-loop traffic: datagrams on a schedule, and a sink that measures what arrives.

There is no window, no acknowledgement and no reaction, which is the point. A UDP
flow measures what the network does to traffic that does not adapt, and it is the
baseline every closed-loop result should be read against. It is also the cheapest
way to put a background load of an exact rate on a cell.

The sink reports what a receiver can actually observe: delivery, loss inferred
from gaps in the sequence, one-way delay, interarrival jitter and congestion
marks. Nothing here reads the network's own counters.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .clock import Clock, Scheduler
from .link import Arrival, Direction, Ecn, Transmit
from .runner import Flow


@dataclass
class UdpStats:
    datagrams_sent: int = 0
    bytes_sent: int = 0


class UdpSource:
    """A paced datagram generator: `iperf3 -u -b <rate> -l <size> -t <duration>`."""

    def __init__(self, flow: int, clock: Clock, sched: Scheduler, rate_bps: float,
                 datagram_bytes: int = 1200, direction: Direction = "dl",
                 ecn: Ecn = "not-ect", start_tti: int = 0,
                 duration_ttis: int | None = None) -> None:
        self.flow = flow
        self.clock = clock
        self.sched = sched
        self.rate_bps = rate_bps
        self.datagram_bytes = datagram_bytes
        self.direction = direction
        self.ecn = ecn
        self.start_tti = start_tti
        self.stop_tti = None if duration_ttis is None else start_tti + duration_ttis

        self.seq = 0
        self.outstanding = 0
        self.credit_bytes = 0.0
        self.cancelled = False
        self.finished_at_tti: int | None = None
        self.stats = UdpStats()
        # The backend reads these from any sender; an open-loop flow measures
        # neither, and saying so is better than reporting a zero.
        self.srtt_us: float | None = None
        self.last_rtt_us: int | None = None

    # -- the sender interface the runner expects ----------------------------------

    def app_write(self, nbytes: int) -> None:
        """Length in bytes, converted to a duration at the configured rate."""
        if self.rate_bps > 0:
            self.stop_tti = self.clock.tti + int(nbytes * 8 / self.rate_bps * 1000)

    def set_unlimited(self) -> None:
        self.stop_tti = None

    def app_cancel(self) -> int:
        self.cancelled = True
        self.stop_tti = self.clock.tti
        return 0

    def complete(self) -> bool:
        return (self.stop_tti is not None and self.clock.tti >= self.stop_tti
                and self.outstanding == 0)

    @property
    def in_flight(self) -> int:
        return self.outstanding

    def send_window(self) -> list[Transmit]:
        tti = self.clock.tti
        if tti < self.start_tti or self.cancelled:
            return []
        if self.stop_tti is not None and tti >= self.stop_tti:
            return []
        self.credit_bytes += self.rate_bps / 8.0 / 1000.0
        out: list[Transmit] = []
        while self.credit_bytes >= self.datagram_bytes:
            self.credit_bytes -= self.datagram_bytes
            out.append(Transmit(self.flow, self.seq, self.datagram_bytes,
                                self.direction, self.ecn, ts_us=self.clock.us))
            self.seq += 1
            self.outstanding += 1
            self.stats.datagrams_sent += 1
            self.stats.bytes_sent += self.datagram_bytes
        return out

    def on_outcome(self) -> None:
        self.outstanding -= 1


class UdpSink:
    def __init__(self, flow: int, source: UdpSource) -> None:
        self.flow = flow
        self.source = source
        self.received = 0
        self.bytes_received = 0
        self.ce_marks = 0
        self.lost_outcomes = 0
        self.highest_seq = -1
        self.owd_us: list[int] = []
        self.jitter_us = 0.0
        self._last_transit_us: int | None = None
        # The backend reads progress from `rcv_nxt`, which for a datagram flow is
        # simply the bytes that arrived.
        self.rcv_nxt = 0

    @property
    def lost(self) -> int:
        """Terminal non-delivery outcomes reported by the link."""
        return self.lost_outcomes

    @property
    def loss_ratio(self) -> float:
        sent = self.source.stats.datagrams_sent
        return self.lost / sent if sent > 0 else 0.0

    def on_arrival(self, arrival: Arrival):
        self.source.on_outcome()
        if arrival.fate != "delivered":
            self.lost_outcomes += 1
            return None
        self.received += 1
        self.bytes_received += arrival.size
        self.rcv_nxt += arrival.size
        self.ce_marks += 1 if arrival.ce else 0
        self.highest_seq = max(self.highest_seq, arrival.seq)

        transit = arrival.tti * 1000 - arrival.ts_us
        self.owd_us.append(transit)
        if self._last_transit_us is not None:
            d = abs(transit - self._last_transit_us)
            self.jitter_us += (d - self.jitter_us) / 16.0   # RFC 3550
        self._last_transit_us = transit
        return None

    def report(self) -> dict:
        owd = sorted(self.owd_us)
        return {
            "datagrams_sent": self.source.stats.datagrams_sent,
            "datagrams_received": self.received,
            "lost": self.lost,
            "loss_ratio": self.loss_ratio,
            "bytes_received": self.bytes_received,
            "owd_ms_min": owd[0] / 1000.0 if owd else None,
            "owd_ms_median": owd[len(owd) // 2] / 1000.0 if owd else None,
            "owd_ms_max": owd[-1] / 1000.0 if owd else None,
            "jitter_ms": self.jitter_us / 1000.0,
            "ce_marks": self.ce_marks,
        }


def udp_flow(flow_id: int, clock: Clock, sched: Scheduler, rate_mbps: float,
             datagram_bytes: int = 1200, direction: Direction = "dl",
             ecn: Ecn = "not-ect", duration_ttis: int | None = None,
             start_tti: int = 0) -> tuple[Flow, UdpSource, UdpSink]:
    source = UdpSource(flow_id, clock, sched, rate_mbps * 1e6, datagram_bytes,
                       direction, ecn, start_tti, duration_ttis)
    sink = UdpSink(flow_id, source)
    return Flow(source, sink), source, sink
