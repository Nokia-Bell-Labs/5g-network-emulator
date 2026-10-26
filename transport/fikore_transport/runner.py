# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The lockstep loop: one slot, one pass, in a fixed order.

Order inside slot n:

  1. the network reports what became terminal up to n
  2. receivers turn arrivals into acknowledgements
  3. acknowledgements that are due at n reach their sender, and timers fire
  4. senders are asked for their segments, which are handed over for slot n+1
  5. the network is allowed to run slot n+1

Nothing in step 4 can depend on anything the network does after step 1, which is
the property that makes the run causally correct: a segment sent as a reaction to
an acknowledgement always leaves at least one slot after the arrival that caused it.
"""
from __future__ import annotations

from dataclasses import dataclass

from .clock import Clock, Scheduler
from .link import Arrival, Link, Transmit
from .tcp import Ack, TcpReceiver, TcpSender


@dataclass
class Flow:
    sender: TcpSender
    receiver: TcpReceiver
    ack_delay_ttis: int = 1       # host turnaround when acks do not cross the link
    ack_over_link: bool = False   # inject acks as real bytes on the return direction
    ack_bytes: int = 40
    # Segments handed to the network whose outcome is not known yet. A cancelled
    # transfer is finished when this reaches zero, not when the application stops.
    in_network: int = 0
    acks_in_network: int = 0


class Runner:
    def __init__(self, link: Link, clock: Clock | None = None,
                 retain_arrivals: bool = True) -> None:
        self.clock = clock or Clock()
        self.sched = Scheduler(self.clock)
        self.link = link
        self.flows: dict[int, Flow] = {}
        self._ack_registry: dict[int, Ack] = {}
        self._next_ack_id = 1
        self.arrivals: list[Arrival] = []
        self.retain_arrivals = retain_arrivals

    def add_flow(self, flow: Flow) -> None:
        self.flows[flow.sender.flow] = flow

    def remove_flow(self, flow_id: int) -> None:
        """Stop visiting a terminal flow on every future TTI.

        Already scheduled acknowledgement/timer callbacks retain the objects they
        need; the runner registry exists only for per-slot dispatch and arrivals.
        """
        self.flows.pop(flow_id, None)

    def close(self) -> None:
        for flow in self.flows.values():
            flow.sender.app_cancel()
        self.flows.clear()
        self._ack_registry.clear()
        self.sched.clear()

    def run_until(self, last_tti: int) -> None:
        while self.clock.tti <= last_tti:
            self.tick()

    def tick(self) -> None:
        tti = self.clock.tti
        for arrival in self.link.step(tti):
            if self.retain_arrivals:
                self.arrivals.append(arrival)
            self._on_arrival(arrival)

        self.sched.run_slot(tti)

        outbox: list[Transmit] = []
        for flow in self.flows.values():
            sent = flow.sender.send_window()
            flow.in_network += len(sent)
            outbox.extend(sent)
            if flow.sender.finished_at_tti is None and flow.sender.complete():
                flow.sender.finished_at_tti = tti
        if outbox:
            self.link.submit(outbox, at_tti=tti + 1)

        self.clock.tti = tti + 1

    # -- feedback paths -----------------------------------------------------------

    def _on_arrival(self, arrival: Arrival) -> None:
        flow = self.flows.get(arrival.flow)
        if flow is None:
            return
        if arrival.kind == "ack":
            flow.acks_in_network -= 1
            if arrival.fate == "delivered":
                ack = self._ack_registry.pop(arrival.seq, None)
                if ack is not None:
                    flow.sender.on_ack(ack)
            else:
                self._ack_registry.pop(arrival.seq, None)
            return

        flow.in_network -= 1
        ack = flow.receiver.on_arrival(arrival)
        if ack is None:
            return
        if flow.ack_over_link:
            ack_id = self._next_ack_id
            self._next_ack_id += 1
            self._ack_registry[ack_id] = ack
            flow.acks_in_network += 1
            back = "ul" if flow.sender.direction == "dl" else "dl"
            self.link.submit([Transmit(flow.sender.flow, ack_id, flow.ack_bytes,
                                       back, "not-ect", ts_us=ack.ts_echo_us,
                                       kind="ack")],
                             at_tti=self.clock.tti + 1)
        else:
            self.sched.at(self.clock.tti + flow.ack_delay_ttis,
                          lambda a=ack, f=flow: f.sender.on_ack(a),
                          Scheduler.PHASE_ARRIVAL)
