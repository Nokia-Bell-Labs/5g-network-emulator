# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Network telemetry sampled from the link, beside the byte path rather than in it.

The transport moves bytes; what the network looks like is a separate question with
its own cadence. A link that can answer it implements `network_counters()`, which
returns cumulative per-UE counters, and `NetworkTelemetrySampler` turns successive
readings into rates at a fixed interval of simulated time. Any link can do this:
FikoRE reports the counters its state snapshots carry, the loopback link counts its
own queue, and a netem-style single queue would only need the same method.

RTT and retransmitted bytes are not network measurements: they come from the
transport's own estimators and are passed in through `transport_stats`. They travel
with the network fields for now because the consumers expect them there.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import (TYPE_CHECKING, Callable, Iterable, Mapping, Protocol,
                    runtime_checkable)

if TYPE_CHECKING:
    from .clock import Clock
    from .link import Direction


@dataclass(frozen=True)
class NetworkTelemetry:
    throughput_mbps: float | None = None
    rtt_ms: float | None = None
    ip_latency_ms: float | None = None
    pdcp_latency_ms: float | None = None
    ce_rate: float | None = None
    drop_rate: float | None = None
    retransmitted_bytes: int | None = None
    sinr_db: float | None = None
    queue_bytes: int | None = None


@dataclass(frozen=True)
class NetworkTelemetryReceived:
    ue_id: int
    time_s: float
    fields: NetworkTelemetry


@dataclass(frozen=True)
class LinkCounters:
    """Cumulative counters for one UE and direction, as the link sees them.

    Byte and packet counters only ever grow. The other fields are instantaneous.
    """
    delivered_bytes: float = 0.0
    lost_bytes: float = 0.0
    ce_packets: float = 0.0
    queue_bytes: int = 0
    ip_latency_ms: float | None = None
    pdcp_latency_ms: float | None = None
    sinr_db: float | None = None


@dataclass(frozen=True)
class TransportStats:
    rtt_ms: float | None = None
    retransmitted_bytes: int | None = None


@runtime_checkable
class ObservableLink(Protocol):
    def network_counters(self, direction: Direction = "dl") -> Mapping[int, LinkCounters]:
        ...


def counters_from_fikore_state(state: Mapping) -> LinkCounters:
    """Read one entry of FikoRE's `ue_state` (one UE, one direction)."""
    return LinkCounters(
        delivered_bytes=float(state.get("delivered_bytes_total", 0.0)),
        lost_bytes=(float(state.get("dropped_bytes_total", 0.0))
                    + float(state.get("expired_bytes_total", 0.0))),
        ce_packets=float(state.get("ce_packets_total", 0.0)),
        queue_bytes=int(state.get("pending_bytes", 0)),
        pdcp_latency_ms=float(state.get("latency_s", 0.0)) * 1000.0 or None,
        sinr_db=state.get("sinr_db"),
    )


def telemetry_from_counters(now: LinkCounters, base: LinkCounters | None, *,
                            elapsed_s: float, mss: int,
                            stats: TransportStats | None = None) -> NetworkTelemetry:
    """Rates over the interval between two readings of the same counters."""
    base = base or LinkCounters()
    stats = stats or TransportStats()

    def delta(field: str) -> float:
        return max(getattr(now, field) - getattr(base, field), 0.0)

    delivered = delta("delivered_bytes")
    lost = delta("lost_bytes")
    received_pkts = delivered / mss if mss else 0.0
    return NetworkTelemetry(
        throughput_mbps=delivered * 8 / elapsed_s / 1e6,
        rtt_ms=stats.rtt_ms,
        ip_latency_ms=now.ip_latency_ms,
        pdcp_latency_ms=now.pdcp_latency_ms,
        ce_rate=delta("ce_packets") / received_pkts if received_pkts else None,
        drop_rate=lost / (delivered + lost) if delivered + lost else 0.0,
        retransmitted_bytes=stats.retransmitted_bytes,
        sinr_db=now.sinr_db,
        queue_bytes=now.queue_bytes,
    )


class NetworkTelemetrySampler:
    """Per-UE network telemetry, one sample per UE every `interval_s`.

    Samples fall on multiples of the interval, so a 1 s sampler driven in 10 ms
    steps reports at 1.0 s, 2.0 s and so on. `poll(force=True)` closes a shorter
    final interval, measured over its actual duration. A link without
    `network_counters()` still yields RTT and retransmissions if `transport_stats`
    is given.
    """

    def __init__(self, link: object, clock: Clock, *, ue_ids: Iterable[int],
                 mss: int | None = None, interval_s: float = 1.0,
                 direction: Direction = "dl",
                 transport_stats: Callable[[int], TransportStats] | None = None) -> None:
        interval_ttis = round(interval_s * 1000)
        if interval_ttis < 1:
            raise ValueError(f"telemetry interval must be at least 1 ms: {interval_s}")
        self.link = link
        self.clock = clock
        self.ue_ids = sorted(set(ue_ids))
        self.mss = mss if mss is not None else getattr(link, "mss", 1500)
        self.interval_ttis = interval_ttis
        self.direction = direction
        self.transport_stats = transport_stats
        self._last_tti = clock.tti
        self._next_tti = (clock.tti // interval_ttis + 1) * interval_ttis
        self._baseline: dict[int, LinkCounters] = {}

    def poll(self, *, force: bool = False) -> list[NetworkTelemetryReceived]:
        tti = self.clock.tti
        if tti < self._next_tti and not (force and tti > self._last_tti):
            return []
        elapsed_s = (tti - self._last_tti) / 1000.0
        self._last_tti = tti
        self._next_tti = (tti // self.interval_ttis + 1) * self.interval_ttis

        counters = (self.link.network_counters(self.direction)
                    if isinstance(self.link, ObservableLink) else None)
        samples = []
        for ue_id in self.ue_ids:
            stats = (self.transport_stats(ue_id) if self.transport_stats is not None
                     else TransportStats())
            if counters is None:
                fields = NetworkTelemetry(rtt_ms=stats.rtt_ms,
                                          retransmitted_bytes=stats.retransmitted_bytes)
            else:
                now = counters.get(ue_id, LinkCounters())
                fields = telemetry_from_counters(now, self._baseline.get(ue_id),
                                                 elapsed_s=elapsed_s, mss=self.mss,
                                                 stats=stats)
                self._baseline[ue_id] = now
            samples.append(NetworkTelemetryReceived(ue_id, tti / 1000.0, fields))
        return samples
