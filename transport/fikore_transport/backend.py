# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""A request and delivery interface over the transport model.

A caller submits opaque byte requests and reads timestamped delivery events. The
difference from handing bytes straight to the network is that they travel over a
modelled TCP connection: the window bounds what is outstanding, losses are
repaired by retransmission, and the round-trip time is measured rather than left
empty.

Two clocks meet in `advance()`. The harness asks for one step of its own window,
usually 10 ms, and the model runs that window one slot at a time underneath. The
player sees the granularity it wants and the transport sees the granularity it
needs.
"""
from __future__ import annotations

import warnings
from dataclasses import dataclass, field
from typing import Iterable, TypeAlias

from .cc import CongestionControl, Cubic
from .ideal import IdealReceiver, IdealSender, SharedWindow
from .link import Direction, Ecn, Link
from .runner import Flow, Runner
from .tcp import TcpReceiver, TcpSender
from .telemetry import (LinkCounters, NetworkTelemetry, NetworkTelemetryReceived,
                        NetworkTelemetrySampler, TransportStats,
                        counters_from_fikore_state, telemetry_from_counters)

RFC9218_DEFAULT_URGENCY = 3
RFC9218_MIN_URGENCY = 0
RFC9218_MAX_URGENCY = 7


@dataclass(frozen=True)
class DownloadProgress:
    ue_id: int
    request_id: str
    bytes_delivered: int
    time_s: float


@dataclass(frozen=True)
class DownloadCompleted:
    ue_id: int
    request_id: str
    time_s: float


@dataclass(frozen=True)
class DownloadCancelled:
    ue_id: int
    request_id: str
    bytes_delivered: int
    time_s: float


# `NetworkTelemetryReceived` stays in the union only for the deprecated in-step
# telemetry; new callers read it from `NetworkTelemetrySampler` instead.
NetworkEvent: TypeAlias = (DownloadProgress | DownloadCompleted | DownloadCancelled
                           | NetworkTelemetryReceived)


@dataclass(frozen=True)
class NetworkStep:
    time_s: float
    events: list[NetworkEvent]
    is_final: bool


@dataclass(frozen=True)
class UeControl:
    priority: float | None = None
    rmax_mbps: float | None = None


@dataclass
class _Request:
    ue_id: int
    request_id: str
    bytes_total: int
    flow: Flow
    stream_start: int = 0
    reported_bytes: int = 0
    cancelled: bool = False
    closed: bool = False
    # RFC 9218 multiplexed-mode fields (unused in persistent/fresh modes)
    multiplexed: bool = False
    urgency: int = RFC9218_DEFAULT_URGENCY
    incremental: bool = False
    admission_seq: int = 0
    bytes_fed: int = 0
    slices: list[tuple[int, int]] = field(default_factory=list)


@dataclass
class _Connection:
    ue_id: int
    flow: Flow
    reusable: bool
    active_request: tuple[int, str] | None = None
    cancelled: bool = False
    multiplexed: bool = False
    stream_cursor: int = 0
    rr_cursors: dict[int, int] = field(default_factory=dict)
    mux_streams: list[tuple[int, str]] = field(default_factory=list)


@dataclass
class BackendConfig:
    window_ttis: int = 10             # one harness step
    horizon_ttis: int | None = None   # when the run ends
    mss: int = 1500
    direction: Direction = "dl"
    ecn: Ecn = "not-ect"
    rwnd: int = 256 * 1024
    ack_over_link: bool = False
    # Deprecated: emits NetworkTelemetryReceived inside NetworkStep every N windows.
    # Set to None and sample with NetworkTelemetrySampler instead.
    telemetry_every_windows: int | None = 1
    # "persistent": HTTP/1.1-style connection pool per UE (default).
    # "fresh": one connection per object.
    # "multiplexed": HTTP/2-like single shared TCP connection per UE scheduled via
    # RFC 9218 priorities (urgency 0..7, non-incremental FIFO by default).
    tcp_connection_mode: str = "persistent"
    mux_default_urgency: int = RFC9218_DEFAULT_URGENCY
    mux_default_incremental: bool = False
    max_idle_tcp_connections_per_ue: int = 6
    cc_factory: type[CongestionControl] = Cubic
    # "tcp" is the transport model; "ideal" is bare injection, a fixed
    # window per UE and instant recovery of whatever the network reports as lost.
    # Everything else being equal, the two differ only in this.
    transport: str = "tcp"
    ideal_window_bytes: int = 128 * 1024
    ideal_recover: bool = True
    retain_request_history: bool = False
    retain_arrivals: bool = False


class TransportBackend:
    """`NetworkBackend` over a `Link`. Works with the loopback link or with FikoRE."""

    def __init__(self, link: Link, cfg: BackendConfig | None = None) -> None:
        self.cfg = cfg or BackendConfig()
        every = self.cfg.telemetry_every_windows
        if every is not None:
            if every < 1:
                raise ValueError(
                    f"telemetry_every_windows must be None or at least 1: {every}")
            warnings.warn(
                "telemetry inside NetworkStep is deprecated: set "
                "telemetry_every_windows=None and use NetworkTelemetrySampler",
                DeprecationWarning, stacklevel=2)
        if not (RFC9218_MIN_URGENCY <= self.cfg.mux_default_urgency <= RFC9218_MAX_URGENCY):
            raise ValueError(
                f"mux_default_urgency must be in {RFC9218_MIN_URGENCY}..{RFC9218_MAX_URGENCY}"
            )
        if self.cfg.transport == "ideal" and self.cfg.tcp_connection_mode == "multiplexed":
            raise ValueError("multiplexed connection mode requires transport='tcp'")
        self.link = link
        self.cfg.mss = getattr(link, "mss", self.cfg.mss)
        self.runner = Runner(link, retain_arrivals=self.cfg.retain_arrivals)
        self.requests: dict[tuple[int, str], _Request] = {}
        self._seen_requests: set[tuple[int, str],] = set()
        self._active_requests: set[tuple[int, str]] = set()
        self._connections: dict[int, _Connection] = {}
        self._idle_connections: dict[int, list[int]] = {}
        self._mux_connections: dict[int, _Connection] = {}
        self._retiring_flows: dict[int, _Connection] = {}
        self._known_ues: set[int] = set()
        self._retired_retransmitted: dict[int, int] = {}
        self._next_flow = 1
        self._next_admission_seq = 1
        self._windows = 0
        self._initial = True
        self._final_emitted = False
        self._closed = False
        self._telemetry_baseline: dict[int, LinkCounters] = {}
        self._telemetry_tti_baseline: dict[int, int] = {}
        self._windows_by_ue: dict[int, SharedWindow] = {}

    # -- NetworkBackend -----------------------------------------------------------

    def submit_request(
        self,
        ue_id: int,
        request_id: str,
        bytes_total: int,
        *,
        urgency: int | None = None,
        incremental: bool | None = None,
    ) -> None:
        key = (ue_id, request_id)
        if key in self._seen_requests:
            raise ValueError(f"request {request_id} is already live on ue {ue_id}")
        if bytes_total <= 0:
            raise ValueError("bytes_total must be positive")
        if self.cfg.transport not in ("tcp", "ideal"):
            raise ValueError(f"unsupported transport: {self.cfg.transport}")
        if self.cfg.tcp_connection_mode not in ("persistent", "fresh", "multiplexed"):
            raise ValueError(
                f"unsupported TCP connection mode: {self.cfg.tcp_connection_mode}")
        if self.cfg.transport == "ideal" and self.cfg.tcp_connection_mode == "multiplexed":
            raise ValueError("multiplexed connection mode requires transport='tcp'")
        if self.cfg.max_idle_tcp_connections_per_ue < 0:
            raise ValueError("max idle TCP connections per UE must be non-negative")

        eff_urgency = self.cfg.mux_default_urgency if urgency is None else int(urgency)
        if not (RFC9218_MIN_URGENCY <= eff_urgency <= RFC9218_MAX_URGENCY):
            raise ValueError(
                f"RFC 9218 urgency must be in {RFC9218_MIN_URGENCY}..{RFC9218_MAX_URGENCY}"
            )
        eff_incremental = (
            self.cfg.mux_default_incremental
            if incremental is None
            else bool(incremental)
        )

        if self.cfg.tcp_connection_mode == "multiplexed":
            connection = self._mux_connections.get(ue_id)
            if connection is None:
                connection = self._new_connection(ue_id, reusable=True, multiplexed=True)
                self._mux_connections[ue_id] = connection
            adm_seq = self._next_admission_seq
            self._next_admission_seq += 1
            request = _Request(
                ue_id=ue_id,
                request_id=request_id,
                bytes_total=bytes_total,
                flow=connection.flow,
                multiplexed=True,
                urgency=eff_urgency,
                incremental=eff_incremental,
                admission_seq=adm_seq,
            )
            connection.mux_streams.append(key)
            self.requests[key] = request
            self._seen_requests.add(key)
            self._active_requests.add(key)
            self._known_ues.add(ue_id)
            return

        reusable = (self.cfg.transport == "tcp"
                    and self.cfg.tcp_connection_mode == "persistent")
        connection = self._acquire_connection(ue_id) if reusable else None
        if connection is None:
            connection = self._new_connection(ue_id, reusable)
        flow = connection.flow
        sender = flow.sender
        stream_start = getattr(sender, "snd_high", 0)
        request = _Request(ue_id, request_id, bytes_total, flow,
                           stream_start=stream_start)
        connection.active_request = key
        sender.app_write(bytes_total)
        self.requests[key] = request
        self._seen_requests.add(key)
        self._active_requests.add(key)
        self._known_ues.add(ue_id)

    def _new_connection(
        self, ue_id: int, reusable: bool, *, multiplexed: bool = False
    ) -> _Connection:
        flow_id = self._next_flow
        self._next_flow += 1
        register = getattr(self.link, "register_flow", None)
        if register is not None:
            register(flow_id, ue_id)
        if self.cfg.transport == "ideal":
            window = self._windows_by_ue.setdefault(
                ue_id, SharedWindow(self.cfg.ideal_window_bytes))
            sender = IdealSender(flow_id, self.runner.clock, self.runner.sched,
                                 self.cfg.mss, window, direction=self.cfg.direction,
                                 ecn=self.cfg.ecn, recover=self.cfg.ideal_recover)
            receiver = IdealReceiver(flow_id, self.cfg.mss, sender)
        elif self.cfg.transport == "tcp":
            cc = self.cfg.cc_factory(mss=self.cfg.mss, cwnd=10 * self.cfg.mss)
            sender = TcpSender(flow_id, cc, self.runner.clock, self.runner.sched,
                               self.cfg.mss, direction=self.cfg.direction,
                               ecn=self.cfg.ecn, rwnd=self.cfg.rwnd)
            receiver = TcpReceiver(flow_id, self.cfg.mss)
        flow = Flow(sender, receiver, ack_over_link=self.cfg.ack_over_link)
        self.runner.add_flow(flow)
        connection = _Connection(ue_id, flow, reusable, multiplexed=multiplexed)
        if multiplexed and isinstance(sender, TcpSender):
            sender.pull_fn = lambda want, conn=connection: self._pull_mux_frames(conn, want)
        self._connections[flow_id] = connection
        return connection

    def _acquire_connection(self, ue_id: int) -> _Connection | None:
        idle = self._idle_connections.get(ue_id, [])
        while idle:
            connection = self._connections.get(idle.pop())
            if (connection is not None and connection.reusable
                    and connection.active_request is None):
                return connection
        return None

    def cancel_request(self, ue_id: int, request_id: str) -> None:
        request = self.requests.get((ue_id, request_id))
        if request is None or request.closed or request.cancelled:
            return
        request.cancelled = True
        connection = self._connections[request.flow.sender.flow]
        if request.multiplexed:
            self._cancel_multiplexed_stream(connection)
            return
        connection.reusable = False
        connection.cancelled = True
        request.flow.sender.app_cancel()

    def _cancel_multiplexed_stream(self, connection: _Connection) -> None:
        """HTTP/2 RST_STREAM: retract unsent bytes above snd_high without closing the flow."""
        sender = connection.flow.sender
        wire_end = int(sender.snd_high)
        if sender.app_available > 0:
            sender.app_retract_unsent(sender.app_available)
            for stream_key in connection.mux_streams:
                req = self.requests.get(stream_key)
                if req is None or not req.slices:
                    continue
                clipped: list[tuple[int, int]] = []
                for start, end in req.slices:
                    if end <= wire_end:
                        clipped.append((start, end))
                    elif start < wire_end:
                        clipped.append((start, wire_end))
                req.slices = clipped
                req.bytes_fed = sum(end - start for start, end in clipped)
            connection.stream_cursor = wire_end

    def _pull_mux_frames(self, connection: _Connection, target_avail: int) -> None:
        """Called inside `TcpSender.send_window()` after ACKs update cwnd and pipe."""
        sender = connection.flow.sender
        if sender.cancelled:
            return
        frame_size = self.cfg.mss
        while sender.app_available < target_avail:
            nxt_req = self._select_rfc9218_stream(connection)
            if nxt_req is None:
                break
            remaining = nxt_req.bytes_total - nxt_req.bytes_fed
            chunk = min(frame_size, remaining)
            start = connection.stream_cursor
            end = start + chunk
            if nxt_req.slices and nxt_req.slices[-1][1] == start:
                nxt_req.slices[-1] = (nxt_req.slices[-1][0], end)
            else:
                nxt_req.slices.append((start, end))
            nxt_req.bytes_fed += chunk
            connection.stream_cursor = end
            sender.app_write(chunk)

    def _select_rfc9218_stream(self, connection: _Connection) -> _Request | None:
        """RFC 9218 Section 4 & 10 scheduler: lowest urgency number first; within an
        urgency bucket ordered by admission, a leading non-incremental stream runs
        exclusively while a leading run of incremental streams shares frames round-robin."""
        eligible: list[_Request] = []
        for key in connection.mux_streams:
            req = self.requests.get(key)
            if (
                req is not None
                and not req.cancelled
                and not req.closed
                and req.bytes_fed < req.bytes_total
            ):
                eligible.append(req)
        if not eligible:
            return None
        best_urgency = min(req.urgency for req in eligible)
        bucket = sorted(
            (req for req in eligible if req.urgency == best_urgency),
            key=lambda req: req.admission_seq,
        )
        if not bucket[0].incremental:
            return bucket[0]
        # Leading contiguous run of incremental streams ahead of any later non-incremental stream
        inc_run: list[_Request] = []
        for req in bucket:
            if not req.incremental:
                break
            inc_run.append(req)
        cursor = connection.rr_cursors.get(best_urgency, 0)
        idx = cursor % len(inc_run)
        connection.rr_cursors[best_urgency] = idx + 1
        return inc_run[idx]

    def advance(self) -> NetworkStep:
        if self._closed:
            raise RuntimeError("the backend is closed")
        if self._final_emitted:
            raise RuntimeError("backend already emitted its final NetworkStep")
        if self._initial:
            self._initial = False
            return NetworkStep(time_s=0.0, events=[], is_final=False)
        horizon = self.cfg.horizon_ttis
        remaining = (self.cfg.window_ttis if horizon is None
                     else max(horizon - self.runner.clock.tti, 0))
        steps = min(self.cfg.window_ttis, remaining)
        for _ in range(steps):
            if horizon is not None and self.runner.clock.tti + 1 >= horizon:
                request_state = getattr(self.link, "request_state_next_step", None)
                if request_state is not None:
                    request_state()
            self.runner.tick()
        self._windows += 1

        now_s = self.runner.clock.s
        events: list[NetworkEvent] = list(self._request_events(now_s))
        self._cleanup_retiring_flows()
        every = self.cfg.telemetry_every_windows
        if every is not None and self._windows % every == 0:
            events.extend(self._telemetry_events(now_s))

        is_final = horizon is not None and self.runner.clock.tti >= horizon
        self._final_emitted = is_final
        return NetworkStep(time_s=now_s, events=events, is_final=is_final)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        unregister = getattr(self.link, "unregister_flow", None)
        for flow_id in list(self._connections):
            if unregister is not None:
                unregister(flow_id)
        self.runner.close()
        self._connections.clear()
        self._idle_connections.clear()
        self._mux_connections.clear()
        self._retiring_flows.clear()
        self._active_requests.clear()
        if not self.cfg.retain_request_history:
            self.requests.clear()
        self.link.close()

    def set_ue_control(self, ue_id: int, control: UeControl) -> None:
        setter = getattr(self.link, "set_params", None)
        if setter is None:
            return
        params: dict[str, float] = {}
        if control.priority is not None:
            params["priority"] = control.priority
        if control.rmax_mbps is not None:
            params[f"{self.cfg.direction}.rmax_mbps"] = control.rmax_mbps
        if params:
            setter(ue_id, params)

    # -- events -------------------------------------------------------------------

    @staticmethod
    def _delivered_bytes(request: _Request) -> int:
        rcv_nxt = request.flow.receiver.rcv_nxt
        if request.multiplexed:
            return sum(
                min(max(rcv_nxt - start, 0), end - start)
                for start, end in request.slices
            )
        return min(
            max(rcv_nxt - request.stream_start, 0),
            request.bytes_total,
        )

    def _request_events(self, now_s: float) -> Iterable[NetworkEvent]:
        # Sorted, because the order of a set of strings changes with the hash seed
        for key in sorted(self._active_requests):
            request = self.requests[key]
            delivered = self._delivered_bytes(request)
            if delivered != request.reported_bytes:
                request.reported_bytes = delivered
                yield DownloadProgress(request.ue_id, request.request_id, delivered,
                                       now_s)
            if request.cancelled:
                if request.multiplexed:
                    tail_end = request.slices[-1][1] if request.slices else 0
                    drained = request.flow.receiver.rcv_nxt >= tail_end
                else:
                    drained = request.flow.in_network == 0
                if drained:
                    request.closed = True
                    yield DownloadCancelled(request.ue_id, request.request_id,
                                            delivered, now_s)
                    self._retire_request(key, request)
            elif delivered >= request.bytes_total:
                request.closed = True
                yield DownloadCompleted(request.ue_id, request.request_id, now_s)
                self._retire_request(key, request)

    def _retire_request(self, key: tuple[int, str], request: _Request) -> None:
        self._active_requests.discard(key)
        flow_id = request.flow.sender.flow
        connection = self._connections[flow_id]
        if request.multiplexed:
            if key in connection.mux_streams:
                connection.mux_streams.remove(key)
            if not self.cfg.retain_request_history:
                self.requests.pop(key, None)
            return
        connection.active_request = None
        if connection.reusable and not request.cancelled:
            idle = self._idle_connections.setdefault(request.ue_id, [])
            if len(idle) < self.cfg.max_idle_tcp_connections_per_ue:
                idle.append(flow_id)
            else:
                connection.reusable = False
        if not connection.reusable or request.cancelled:
            self._retiring_flows[flow_id] = connection
        if not self.cfg.retain_request_history:
            self.requests.pop(key, None)

    def _cleanup_retiring_flows(self) -> None:
        for flow_id, connection in list(self._retiring_flows.items()):
            flow = connection.flow
            if flow.in_network != 0 or flow.acks_in_network != 0:
                continue
            if not connection.cancelled and not flow.sender.complete():
                continue
            self.runner.remove_flow(flow_id)
            unregister = getattr(self.link, "unregister_flow", None)
            if unregister is not None:
                unregister(flow_id)
            retransmitted = flow.sender.stats.retransmits * self.cfg.mss
            self._retired_retransmitted[connection.ue_id] = (
                self._retired_retransmitted.get(connection.ue_id, 0)
                + retransmitted)
            self._connections.pop(flow_id, None)
            del self._retiring_flows[flow_id]

    # -- transport measurements ---------------------------------------------------

    def transport_stats(self, ue_id: int) -> TransportStats:
        """What the transport itself measures for a UE: the mean smoothed RTT of
        its open connections and the bytes it has retransmitted so far."""
        connections = [connection for connection in self._connections.values()
                       if connection.ue_id == ue_id]
        srtts = [connection.flow.sender.srtt_us for connection in connections
                 if connection.flow.sender.srtt_us]
        rtt_ms = sum(srtts) / len(srtts) / 1000.0 if srtts else None
        retransmitted = self._retired_retransmitted.get(ue_id, 0) + sum(
            connection.flow.sender.stats.retransmits * self.cfg.mss
            for connection in connections)
        return TransportStats(rtt_ms=rtt_ms, retransmitted_bytes=retransmitted)

    def telemetry_sampler(self, ue_ids: Iterable[int], *,
                          interval_s: float = 1.0) -> NetworkTelemetrySampler:
        """A sampler over this backend's link and clock, with its transport stats."""
        return NetworkTelemetrySampler(
            self.link, self.runner.clock, ue_ids=ue_ids, mss=self.cfg.mss,
            interval_s=interval_s, direction=self.cfg.direction,
            transport_stats=self.transport_stats)

    # -- deprecated in-step telemetry ----------------------------------------------

    def _telemetry_events(self, now_s: float) -> Iterable[NetworkEvent]:
        state = getattr(self.link, "last_state", None)
        for ue_id in sorted(self._known_ues):
            yield NetworkTelemetryReceived(ue_id, now_s,
                                           self._telemetry_for(ue_id, state, now_s))

    def _telemetry_for(self, ue_id: int, state: dict | None,
                       now_s: float) -> NetworkTelemetry:
        """Deprecated: kept only for `telemetry_every_windows`."""
        stats = self.transport_stats(ue_id)
        if state is None:
            return NetworkTelemetry(rtt_ms=stats.rtt_ms,
                                    retransmitted_bytes=stats.retransmitted_bytes)

        now = counters_from_fikore_state(state.get((ue_id, self.cfg.direction), {}))
        base = self._telemetry_baseline.get(ue_id)
        self._telemetry_baseline[ue_id] = now
        previous_tti = self._telemetry_tti_baseline.get(ue_id, 0)
        elapsed_ttis = max(self.runner.clock.tti - previous_tti, 1)
        self._telemetry_tti_baseline[ue_id] = self.runner.clock.tti
        return telemetry_from_counters(now, base, elapsed_s=elapsed_ttis / 1000.0,
                                       mss=self.cfg.mss, stats=stats)
