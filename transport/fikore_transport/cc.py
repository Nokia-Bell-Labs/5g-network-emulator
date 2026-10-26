# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Congestion control, as a replaceable component of the sender.

The interface is the contract the sender relies on. Reno and CUBIC follow ns.py's
algorithms; Prague is a binding to the L4S reference implementation and lives in
`cc_prague.py`, because it needs the ECN counters the other two ignore.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol


@dataclass
class AckInfo:
    """What one acknowledgement tells congestion control."""

    acked_bytes: int
    rtt_us: int | None      # None when the sample is unusable (Karn: retransmitted)
    now_us: int
    pkts_received: int = 0  # cumulative, as echoed by the receiver
    pkts_ce: int = 0
    pkts_lost: int = 0
    pkts_sent: int = 0


class CongestionControl(Protocol):
    mss: int
    cwnd: float             # bytes

    def on_ack(self, ack: AckInfo) -> None: ...

    def on_dupacks(self, count: int) -> None: ...

    def on_recovered(self) -> None: ...

    def on_rto(self) -> None: ...

    def pacing_rate_bps(self) -> float | None: ...


@dataclass
class Classic:
    """Shared state and loss response of the RFC 5681 family."""

    mss: int = 1500
    cwnd: float = 10 * 1500     # IW10, not ns.py's one segment
    ssthresh: float = float("inf")
    in_recovery: bool = False

    def on_ack(self, ack: AckInfo) -> None:
        raise NotImplementedError

    def on_dupacks(self, count: int) -> None:
        if count == 3 and not self.in_recovery:
            # Fast retransmit and fast recovery: halve, then inflate by the segments
            # that have left the network.
            self.ssthresh = max(2 * self.mss, self.cwnd / 2)
            self.cwnd = self.ssthresh + 3 * self.mss
            self.in_recovery = True
        elif count > 3:
            self.cwnd += self.mss

    def on_recovered(self) -> None:
        if self.in_recovery:
            self.cwnd = self.ssthresh
            self.in_recovery = False

    def on_rto(self) -> None:
        self.ssthresh = max(2 * self.mss, self.cwnd / 2)
        self.cwnd = self.mss
        self.in_recovery = False

    def pacing_rate_bps(self) -> float | None:
        return None


@dataclass
class Reno(Classic):
    def on_ack(self, ack: AckInfo) -> None:
        if self.cwnd < self.ssthresh:
            self.cwnd += self.mss
        else:
            self.cwnd += self.mss * self.mss / self.cwnd


@dataclass
class Cubic(Classic):
    """CUBIC with byte-valued public cwnd and packet-valued cubic state."""

    C: float = 0.4
    # Reduction amount: RFC 8312/9438 beta_cubic=0.7 means a 30% reduction.
    beta: float = 0.3
    fast_convergence: bool = True
    tcp_friendliness: bool = True
    w_last_max: float = 0.0
    epoch_start: float = 0.0
    origin_point: float = 0.0
    d_min_s: float = 0.0
    w_tcp: float = 0.0
    K: float = 0.0
    ack_cnt: float = 0.0
    cwnd_cnt: float = 0.0
    cnt: float = 0.0

    def _reset(self) -> None:
        self.epoch_start = 0.0
        self.origin_point = 0.0
        self.w_tcp = 0.0
        self.K = 0.0
        self.ack_cnt = 0.0
        self.cwnd_cnt = 0.0
        self.cnt = float("inf")

    def on_ack(self, ack: AckInfo) -> None:
        now_s = ack.now_us / 1e6
        if ack.rtt_us is not None:
            rtt_s = ack.rtt_us / 1e6
            self.d_min_s = min(self.d_min_s, rtt_s) if self.d_min_s > 0 else rtt_s
        if self.cwnd < self.ssthresh:
            self.cwnd += self.mss
            return
        self._update(now_s)
        self.cwnd_cnt += 1
        if self.cwnd_cnt >= max(1.0, self.cnt):
            self.cwnd += self.mss
            self.cwnd_cnt = 0

    def _update(self, now_s: float) -> None:
        window = self.cwnd / self.mss
        self.ack_cnt += 1
        if self.epoch_start <= 0:
            self.epoch_start = now_s
            if window < self.w_last_max:
                self.K = ((self.w_last_max - window) / self.C) ** (1.0 / 3)
                self.origin_point = self.w_last_max
            else:
                self.K = 0.0
                self.origin_point = window
            self.ack_cnt = 1
            self.w_tcp = window
        t = now_s + self.d_min_s - self.epoch_start
        target = self.origin_point + self.C * (t - self.K) ** 3
        self.cnt = window / (target - window) if target > window else 100 * window
        if self.tcp_friendliness:
            self.w_tcp += 3 * self.beta / (2 - self.beta) * (self.ack_cnt / window)
            self.ack_cnt = 0
            if self.w_tcp > window:
                self.cnt = min(self.cnt, window / (self.w_tcp - window))

    def on_dupacks(self, count: int) -> None:
        if count == 3 and not self.in_recovery:
            self.epoch_start = 0.0
            window = self.cwnd / self.mss
            self.w_last_max = (window * (2 - self.beta) / 2
                               if self.fast_convergence and window < self.w_last_max
                               else window)
            self.ssthresh = max(2 * self.mss, self.cwnd * (1 - self.beta))
            self.cwnd = self.ssthresh + 3 * self.mss
            self.in_recovery = True
        elif count > 3:
            self.cwnd += self.mss

    def on_rto(self) -> None:
        previous_window = self.cwnd / self.mss
        self.ssthresh = max(2 * self.mss, self.cwnd * (1 - self.beta))
        self.cwnd = self.mss
        self.in_recovery = False
        self.w_last_max = previous_window
        self._reset()
