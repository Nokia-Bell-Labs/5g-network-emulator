# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""TCP and UDP bulk sessions on the common lockstep Runner."""
from __future__ import annotations

from dataclasses import dataclass
import time
from typing import Any

from .cc import Cubic, Reno
from .iperf_config import IperfConfig
from .iperf_metrics import FlowView, IntervalRecorder
from .runner import Flow, Runner
from .tcp import TcpReceiver, TcpSender
from .udp import udp_flow


@dataclass
class SessionResult:
    protocol: str
    active_ttis: int
    end_ttis: int
    wall_seconds: float
    intervals: list[dict]
    totals: dict
    conservation: dict
    drained: bool


class IperfSession:
    def __init__(self, cfg: IperfConfig, link: Any,
                 ue_mapping: dict[str, int] | None = None) -> None:
        self.cfg = cfg
        self.link = link
        self.runner = Runner(link, retain_arrivals=False)
        self.ue_mapping = ue_mapping or {
            ue_id: index for index, ue_id in enumerate(cfg.ues)}
        self.flows: list[Flow] = []
        self.views: list[FlowView] = []
        self._build_flows()
        self.recorder = IntervalRecorder(cfg.protocol, self.views)

    def _build_flows(self) -> None:
        next_flow = 1
        register = getattr(self.link, "register_flow", None)
        for ue_id in self.cfg.ues:
            if ue_id not in self.ue_mapping:
                raise ValueError(f"no physical mapping for UE {ue_id!r}")
            for _ in range(self.cfg.parallel_per_ue):
                flow_id = next_flow
                next_flow += 1
                if register is not None:
                    register(flow_id, self.ue_mapping[ue_id])
                if self.cfg.protocol == "tcp":
                    cc = self._controller()
                    sender = TcpSender(
                        flow_id, cc, self.runner.clock, self.runner.sched,
                        self.cfg.mss, direction=self.cfg.direction,
                        ecn=self.cfg.ecn, rwnd=self.cfg.rwnd_bytes)
                    receiver = TcpReceiver(flow_id, self.cfg.mss)
                    flow = Flow(sender, receiver,
                                ack_over_link=self.cfg.ack_over_link)
                    if self.cfg.bytes_total is None:
                        sender.set_unlimited()
                    else:
                        sender.app_write(self.cfg.bytes_total)
                else:
                    flow, sender, receiver = udp_flow(
                        flow_id, self.runner.clock, self.runner.sched,
                        rate_mbps=self.cfg.bitrate_bps / 1e6,
                        datagram_bytes=self.cfg.length_bytes,
                        direction=self.cfg.direction, ecn=self.cfg.ecn,
                        duration_ttis=int(round(self.cfg.duration_s * 1000)))
                self.runner.add_flow(flow)
                self.flows.append(flow)
                self.views.append(FlowView(flow_id, ue_id, sender, receiver))

    def _controller(self):
        if self.cfg.congestion == "reno":
            return Reno(mss=self.cfg.mss, cwnd=10 * self.cfg.mss)
        if self.cfg.congestion == "cubic":
            return Cubic(mss=self.cfg.mss, cwnd=10 * self.cfg.mss)
        from .cc_prague import Prague
        return Prague(mss=self.cfg.mss, cwnd=10 * self.cfg.mss)

    def run(self) -> SessionResult:
        interval = max(int(round(self.cfg.interval_s * 1000)), 1)
        next_report = interval
        wall_start = time.perf_counter()
        if self.cfg.bytes_total is not None:
            # A byte-limited iperf transfer has no duration phase: run until all
            # requested bytes and recovery traffic settle, with a separate safety
            # deadline that does not redefine the requested workload.
            hard_limit = int(round(self.cfg.timeout_s * 1000))
            while not self._settled() and self.runner.clock.tti < hard_limit:
                self.runner.tick()
                if self.runner.clock.tti >= next_report:
                    self.recorder.sample(self.runner.clock.tti)
                    next_report += interval
            active_end = self.runner.clock.tti
            drained = self._settled()
        else:
            active_limit = int(round(self.cfg.duration_s * 1000))
            while self.runner.clock.tti < active_limit:
                self.runner.tick()
                if self.runner.clock.tti >= next_report:
                    self.recorder.sample(self.runner.clock.tti)
                    next_report += interval
            active_end = self.runner.clock.tti
            if self.cfg.protocol == "tcp":
                for view in self.views:
                    view.sender.finish_writes()
            drain_limit = active_end + int(round(self.cfg.drain_timeout_s * 1000))
            while not self._settled() and self.runner.clock.tti < drain_limit:
                self.runner.tick()
            drained = self._settled()

        end_tti = self.runner.clock.tti
        if self.recorder.previous_tti < end_tti:
            final = self.recorder.sample(end_tti)
            if end_tti > active_end:
                final["drain"] = True
                final["sum"]["drain"] = True
                for stream in final["streams"]:
                    stream["drain"] = True
        totals = self.recorder.totals(end_tti)
        conservation = self._conservation()
        return SessionResult(
            protocol=self.cfg.protocol,
            active_ttis=active_end,
            end_ttis=end_tti,
            wall_seconds=time.perf_counter() - wall_start,
            intervals=self.recorder.intervals,
            totals=totals,
            conservation=conservation,
            drained=drained,
        )

    def close(self) -> None:
        unregister = getattr(self.link, "unregister_flow", None)
        if unregister is not None:
            for view in self.views:
                unregister(view.flow_id)
        self.runner.close()
        self.link.close()

    def _settled(self) -> bool:
        return all(
            view.sender.complete() and flow.in_network == 0
            and flow.acks_in_network == 0
            for flow, view in zip(self.flows, self.views))

    def _conservation(self) -> dict:
        if not hasattr(self.link, "submitted_bytes"):
            return {"available": False, "bytes_conserved": None}
        terminal = dict(self.link.terminal_bytes)
        submitted = int(self.link.submitted_bytes)
        in_flight = int(self.link.in_flight_bytes)
        accounted = sum(terminal.values()) + in_flight
        return {
            "available": True,
            "submitted_bytes": submitted,
            "terminal_bytes": terminal,
            "in_flight_bytes": in_flight,
            "lost_bytes_by_cause": dict(self.link.lost_bytes_by_cause),
            "bytes_conserved": submitted == accounted,
        }
