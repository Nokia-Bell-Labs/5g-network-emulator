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

from dataclasses import dataclass
from typing import Iterable, TypeAlias

from .cc import CongestionControl, Cubic
from .ideal import IdealReceiver, IdealSender, SharedWindow
from .link import Direction, Ecn, Link
from .runner import Flow, Runner
from .tcp import TcpReceiver, TcpSender


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


@dataclass
class _Connection:
    ue_id: int
    flow: Flow
    reusable: bool
    active_request: tuple[int, str] | None = None
    cancelled: bool = False


@dataclass
class BackendConfig:
    window_ttis: int = 10             # one harness step
    horizon_ttis: int | None = None   # when the run ends
    mss: int = 1500
    direction: Direction = "dl"
    ecn: Ecn = "not-ect"
    rwnd: int = 256 * 1024
    ack_over_link: bool = False
    telemetry_every_windows: int = 1
    # Persistent mode models an HTTP/1.1-style pool per UE: an idle connection is
    # reused and a concurrent request opens another one. Fresh mode preserves the
    # old one-connection-per-object baseline. Ideal transport is always per object.
    tcp_connection_mode: str = "persistent"
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
        self.link = link
        self.cfg.mss = getattr(link, "mss", self.cfg.mss)
        self.runner = Runner(link, retain_arrivals=self.cfg.retain_arrivals)
        self.requests: dict[tuple[int, str], _Request] = {}
        self._seen_requests: set[tuple[int, str]] = set()
        self._active_requests: set[tuple[int, str]] = set()
        self._connections: dict[int, _Connection] = {}
        self._idle_connections: dict[int, list[int]] = {}
        self._retiring_flows: dict[int, _Connection] = {}
        self._known_ues: set[int] = set()
        self._retired_retransmitted: dict[int, int] = {}
        self._next_flow = 1
        self._windows = 0
        self._initial = True
        self._final_emitted = False
        self._closed = False
        self._telemetry_baseline: dict[int, dict] = {}
        self._telemetry_tti_baseline: dict[int, int] = {}
        self._windows_by_ue: dict[int, SharedWindow] = {}

    # -- NetworkBackend -----------------------------------------------------------

    def submit_request(self, ue_id: int, request_id: str, bytes_total: int) -> None:
        key = (ue_id, request_id)
        if key in self._seen_requests:
            raise ValueError(f"request {request_id} is already live on ue {ue_id}")
        if self.cfg.transport not in ("tcp", "ideal"):
            raise ValueError(f"unsupported transport: {self.cfg.transport}")
        if self.cfg.tcp_connection_mode not in ("persistent", "fresh"):
            raise ValueError(
                f"unsupported TCP connection mode: {self.cfg.tcp_connection_mode}")
        if self.cfg.max_idle_tcp_connections_per_ue < 0:
            raise ValueError("max idle TCP connections per UE must be non-negative")

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

    def _new_connection(self, ue_id: int, reusable: bool) -> _Connection:
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
        connection = _Connection(ue_id, flow, reusable)
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
        connection.reusable = False
        connection.cancelled = True
        request.flow.sender.app_cancel()

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
        if self._windows % self.cfg.telemetry_every_windows == 0:
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

    def _request_events(self, now_s: float) -> Iterable[NetworkEvent]:
        for key in list(self._active_requests):
            request = self.requests[key]
            delivered = min(
                max(request.flow.receiver.rcv_nxt - request.stream_start, 0),
                request.bytes_total,
            )
            if delivered != request.reported_bytes:
                request.reported_bytes = delivered
                yield DownloadProgress(request.ue_id, request.request_id, delivered,
                                       now_s)
            if request.cancelled:
                # The request stays live until the tail already in the network has
                # drained, so the reported waste is what actually travelled.
                if request.flow.in_network == 0:
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

    def _telemetry_events(self, now_s: float) -> Iterable[NetworkEvent]:
        state = getattr(self.link, "last_state", None)
        for ue_id in sorted(self._known_ues):
            yield NetworkTelemetryReceived(ue_id, now_s,
                                           self._telemetry_for(ue_id, state, now_s))

    def _telemetry_for(self, ue_id: int, state: dict | None,
                       now_s: float) -> NetworkTelemetry:
        connections = [connection for connection in self._connections.values()
                       if connection.ue_id == ue_id]
        srtts = [connection.flow.sender.srtt_us for connection in connections
                 if connection.flow.sender.srtt_us]
        rtt_ms = sum(srtts) / len(srtts) / 1000.0 if srtts else None
        retransmitted = self._retired_retransmitted.get(ue_id, 0) + sum(
            connection.flow.sender.stats.retransmits * self.cfg.mss
            for connection in connections)
        if state is None:
            return NetworkTelemetry(rtt_ms=rtt_ms, retransmitted_bytes=retransmitted)

        now = state.get((ue_id, self.cfg.direction), {})
        base = self._telemetry_baseline.get(ue_id, {})
        self._telemetry_baseline[ue_id] = dict(now)
        previous_tti = self._telemetry_tti_baseline.get(ue_id, 0)
        elapsed_ttis = max(self.runner.clock.tti - previous_tti, 1)
        self._telemetry_tti_baseline[ue_id] = self.runner.clock.tti
        window_s = elapsed_ttis / 1000.0

        def delta(key: str) -> float:
            return max(float(now.get(key, 0.0)) - float(base.get(key, 0.0)), 0.0)

        delivered = delta("delivered_bytes_total")
        lost = delta("dropped_bytes_total") + delta("expired_bytes_total")
        received_pkts = delivered / self.cfg.mss if self.cfg.mss else 0.0
        return NetworkTelemetry(
            throughput_mbps=delivered * 8 / window_s / 1e6,
            rtt_ms=rtt_ms,
            pdcp_latency_ms=float(now.get("latency_s", 0.0)) * 1000.0 or None,
            ce_rate=(delta("ce_packets_total") / received_pkts
                     if received_pkts else None),
            drop_rate=(lost / (delivered + lost) if delivered + lost else 0.0),
            retransmitted_bytes=retransmitted,
            sinr_db=now.get("sinr_db"),
            queue_bytes=int(now.get("pending_bytes", 0)),
        )
