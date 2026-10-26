# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Simulated time and event scheduling, quantised to the radio slot.

The clock is not ours to advance: it belongs to the emulator, and the runner moves
it one slot at a time. Everything above reads it and schedules against it, so no
component here ever owns a wall clock or a sleep.
"""
from __future__ import annotations

import heapq
import itertools
from dataclasses import dataclass, field
from typing import Callable

TTI_US = 1000


class Clock:
    """Current simulated instant, in 1 ms radio slots."""

    __slots__ = ("tti",)

    def __init__(self, tti: int = 0) -> None:
        self.tti = tti

    @property
    def us(self) -> int:
        return self.tti * TTI_US

    @property
    def s(self) -> float:
        return self.tti / 1000.0


@dataclass(order=True)
class _Event:
    tti: int
    order: int
    seq: int
    callback: Callable[[], None] = field(compare=False)
    cancelled: bool = field(default=False, compare=False)


class Scheduler:
    """Events keyed by slot, executed in a deterministic order within a slot.

    `order` groups events into phases inside one slot: feedback from the network is
    processed before timers, and timers before the senders get their turn, so that a
    segment acknowledged in slot n is never retransmitted by an RTO in the same slot.
    """

    PHASE_ARRIVAL = 0
    PHASE_TIMER = 1
    PHASE_APP = 2

    def __init__(self, clock: Clock) -> None:
        self.clock = clock
        self._heap: list[_Event] = []
        self._seq = itertools.count()

    def at(self, tti: int, callback: Callable[[], None], order: int = PHASE_TIMER) -> _Event:
        if tti < self.clock.tti:
            raise ValueError(f"cannot schedule into the past: {tti} < {self.clock.tti}")
        ev = _Event(tti, order, next(self._seq), callback)
        heapq.heappush(self._heap, ev)
        return ev

    def after(self, ttis: int, callback: Callable[[], None], order: int = PHASE_TIMER) -> _Event:
        return self.at(self.clock.tti + max(ttis, 0), callback, order)

    @staticmethod
    def cancel(ev: _Event | None) -> None:
        if ev is not None:
            ev.cancelled = True

    def next_tti(self) -> int | None:
        """First slot with work pending. The runner uses it to skip idle slots."""
        while self._heap and self._heap[0].cancelled:
            heapq.heappop(self._heap)
        return self._heap[0].tti if self._heap else None

    def run_slot(self, tti: int) -> None:
        """Run everything due at `tti`. Events created for this same slot run too."""
        while self._heap and self._heap[0].tti <= tti:
            ev = heapq.heappop(self._heap)
            if not ev.cancelled:
                ev.callback()

    def clear(self) -> None:
        self._heap.clear()
