# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Congestion control, as a replaceable component of the sender.

The interface is the contract the sender relies on. Reno and CUBIC follow ns.py's
algorithms extended with RFC 7661 application-limited window validation, RFC 9438
app-limited epoch shifting, and RFC 9406 HyStart++ initial slow-start exit;
Prague is a binding to the L4S reference implementation in `cc_prague.py`.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Protocol

# RFC 9406 Section 4.3 HyStart++ constants (microseconds and packet counts)
HYSTART_MIN_RTT_THRESH_US = 4_000
HYSTART_MAX_RTT_THRESH_US = 16_000
HYSTART_MIN_RTT_DIVISOR = 8
HYSTART_N_RTT_SAMPLE = 8
HYSTART_CSS_GROWTH_DIVISOR = 4
HYSTART_CSS_ROUNDS = 5
# Linux TCP_HYSTART low-window guard (documented non-RFC 9406 floor avoiding premature exit at IW10)
HYSTART_LOW_CWND_PKTS = 16


@dataclass
class AckInfo:
    """What one acknowledgement tells congestion control."""

    acked_bytes: int
    rtt_us: int | None      # None when the sample is unusable
    now_us: int
    pkts_received: int = 0  # cumulative, as echoed by the receiver
    pkts_ce: int = 0
    pkts_lost: int = 0
    pkts_sent: int = 0
    # RFC 7661: defaults to True so standalone controller vector tests behave as
    # unlimited bulk transfers while TcpSender supplies actual pipe utilization.
    is_cwnd_limited: bool = True
    # Monotonic sequence tracking for RFC 9406 HyStart++ rounds (0 disables round tracking).
    snd_una: int = 0
    snd_high: int = 0
    paced: bool = True


class CongestionControl(Protocol):
    mss: int
    cwnd: float             # bytes

    def on_ack(self, ack: AckInfo) -> None: ...

    def on_dupacks(self, count: int) -> None: ...

    def on_congestion_event(
        self,
        *,
        in_nvp: bool = False,
        pipe_ack_bytes: int | None = None,
        loss_flight_bytes: int | None = None,
    ) -> None: ...

    def on_recovered(self) -> None: ...

    def on_rto(self) -> None: ...

    def on_nvp_expired(self) -> None: ...

    def on_tx_start(self, idle_us: int) -> None: ...

    def pacing_rate_bps(self) -> float | None: ...


@dataclass
class Classic:
    """Shared state, RFC 9406 HyStart++, RFC 7661 NVP, and RFC 5681 loss response."""

    mss: int = 1500
    cwnd: float = 10 * 1500     # IW10, not ns.py's one segment
    ssthresh: float = float("inf")
    in_recovery: bool = False
    hystart: bool = True

    # RFC 9406 Section 4.3: HyStart++ SHOULD only be used in the initial slow start.
    initial_slow_start: bool = True
    window_end: int | None = None
    last_round_min_rtt_us: int | None = None
    curr_round_min_rtt_us: int | None = None
    rtt_sample_count: int = 0
    in_css: bool = False
    css_baseline_min_rtt_us: int | None = None
    css_rounds: int = 0

    def on_ack(self, ack: AckInfo) -> None:
        raise NotImplementedError

    def _disable_hystart(self) -> None:
        self.initial_slow_start = False
        self.window_end = None
        self.last_round_min_rtt_us = None
        self.curr_round_min_rtt_us = None
        self.rtt_sample_count = 0
        self.in_css = False
        self.css_baseline_min_rtt_us = None
        self.css_rounds = 0

    def _slow_start_step(self, ack: AckInfo) -> bool:
        """Handle slow start and RFC 9406 HyStart++ when cwnd < ssthresh."""
        if self.cwnd >= self.ssthresh:
            return False
        if ack.acked_bytes <= 0:
            return True

        use_hystart = self.hystart and self.initial_slow_start and ack.snd_high > 0
        if use_hystart:
            if self.window_end is None:
                self.window_end = ack.snd_high
            elif ack.snd_una >= self.window_end:
                self.window_end = ack.snd_high
                self.last_round_min_rtt_us = self.curr_round_min_rtt_us
                self.curr_round_min_rtt_us = None
                self.rtt_sample_count = 0
                if self.in_css:
                    self.css_rounds += 1
                    if self.css_rounds >= HYSTART_CSS_ROUNDS:
                        self.ssthresh = self.cwnd
                        self._disable_hystart()
                        return False

            if ack.rtt_us is not None and ack.rtt_us > 0:
                if self.curr_round_min_rtt_us is None:
                    self.curr_round_min_rtt_us = ack.rtt_us
                else:
                    self.curr_round_min_rtt_us = min(
                        self.curr_round_min_rtt_us, ack.rtt_us
                    )
                self.rtt_sample_count += 1

        # RFC 7661 Section 4.1: do not grow cwnd if not cwnd-limited.
        # RFC 9406 Section 4.2 allows L = inf during paced HyStart++; post-RTO / classic
        # slow start follows RFC 3465 L = 2*SMSS and never overshoots ssthresh.
        if ack.is_cwnd_limited:
            limit = float("inf") if (ack.paced and use_hystart) else float(2 * self.mss)
            inc = min(float(ack.acked_bytes), limit)
            step = inc / HYSTART_CSS_GROWTH_DIVISOR if self.in_css else inc
            self.cwnd = min(self.cwnd + step, self.ssthresh)

        if (
            use_hystart
            and self.rtt_sample_count >= HYSTART_N_RTT_SAMPLE
            and self.curr_round_min_rtt_us is not None
        ):
            if not self.in_css:
                if (
                    self.last_round_min_rtt_us is not None
                    and self.cwnd >= HYSTART_LOW_CWND_PKTS * self.mss
                ):
                    rtt_thresh = min(
                        max(
                            self.last_round_min_rtt_us // HYSTART_MIN_RTT_DIVISOR,
                            HYSTART_MIN_RTT_THRESH_US,
                        ),
                        HYSTART_MAX_RTT_THRESH_US,
                    )
                    if self.curr_round_min_rtt_us >= self.last_round_min_rtt_us + rtt_thresh:
                        self.css_baseline_min_rtt_us = self.curr_round_min_rtt_us
                        self.in_css = True
                        self.css_rounds = 0
            else:
                if (
                    self.css_baseline_min_rtt_us is not None
                    and self.curr_round_min_rtt_us < self.css_baseline_min_rtt_us
                ):
                    self.in_css = False
                    self.css_baseline_min_rtt_us = None
                    self.css_rounds = 0
        return True

    def on_dupacks(self, count: int) -> None:
        """RFC 5681 duplicate-ACK interface retained for standalone reference vectors."""
        if count == 3 and not self.in_recovery:
            self._disable_hystart()
            self.ssthresh = max(2 * self.mss, self.cwnd / 2)
            self.cwnd = self.ssthresh + 3 * self.mss
            self.in_recovery = True
        elif count > 3:
            self.cwnd += self.mss

    def on_congestion_event(
        self,
        *,
        in_nvp: bool = False,
        pipe_ack_bytes: int | None = None,
        loss_flight_bytes: int | None = None,
    ) -> None:
        """Enter loss recovery when pipe already excludes SACKed and RACK-lost bytes."""
        if self.in_recovery:
            return
        self._disable_hystart()
        if in_nvp:
            # RFC 7661 Section 4.4.1 loss response during Non-Validated Period
            ref = max(float(pipe_ack_bytes or 0), float(loss_flight_bytes or 0))
            self.ssthresh = max(2.0 * self.mss, ref / 2.0)
        else:
            self.ssthresh = max(2.0 * self.mss, self.cwnd / 2.0)
        self.cwnd = self.ssthresh
        self.in_recovery = True

    def on_recovered(self) -> None:
        if self.in_recovery:
            self.cwnd = self.ssthresh
            self.in_recovery = False

    def on_rto(self) -> None:
        self._disable_hystart()
        self.ssthresh = max(2 * self.mss, self.cwnd / 2)
        self.cwnd = self.mss
        self.in_recovery = False

    def on_nvp_expired(self) -> None:
        """RFC 7661 Section 4.4.3 adjustment at the end of a 5-minute Non-Validated Period."""
        if math.isfinite(self.ssthresh):
            self.ssthresh = max(self.ssthresh, 0.75 * self.cwnd)
        # Cap without ever increasing cwnd when cwnd < IW10 (RFC 7661 Section 4.4)
        self.cwnd = min(self.cwnd, max(self.cwnd / 2.0, float(10 * self.mss)))

    def on_tx_start(self, idle_us: int) -> None:
        del idle_us

    def pacing_rate_bps(self) -> float | None:
        return None


@dataclass
class Reno(Classic):
    def on_ack(self, ack: AckInfo) -> None:
        if ack.acked_bytes <= 0 or self.in_recovery:
            return
        if self._slow_start_step(ack):
            return
        if not ack.is_cwnd_limited:
            return
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
    last_ack_s: float = 0.0

    def _reset(self) -> None:
        self.epoch_start = 0.0
        self.origin_point = 0.0
        self.w_tcp = 0.0
        self.K = 0.0
        self.ack_cnt = 0.0
        self.cwnd_cnt = 0.0
        self.cnt = float("inf")

    def on_ack(self, ack: AckInfo) -> None:
        if ack.acked_bytes <= 0:
            return
        now_s = ack.now_us / 1e6
        if ack.rtt_us is not None:
            rtt_s = ack.rtt_us / 1e6
            self.d_min_s = min(self.d_min_s, rtt_s) if self.d_min_s > 0 else rtt_s
        if self.in_recovery:
            self.last_ack_s = now_s
            return
        if self._slow_start_step(ack):
            self.last_ack_s = now_s
            return
        if not ack.is_cwnd_limited:
            # RFC 9438 Section 5.8: shift epoch_start forward across application-limited
            # intervals so cubic time `t` excludes periods when cwnd was not utilized.
            if self.epoch_start > 0.0 and self.last_ack_s > 0.0 and now_s > self.last_ack_s:
                self.epoch_start += now_s - self.last_ack_s
            self.last_ack_s = now_s
            return
        self.last_ack_s = now_s
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
            self._disable_hystart()
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

    def on_congestion_event(
        self,
        *,
        in_nvp: bool = False,
        pipe_ack_bytes: int | None = None,
        loss_flight_bytes: int | None = None,
    ) -> None:
        if self.in_recovery:
            return
        self._disable_hystart()
        self.epoch_start = 0.0
        window = self.cwnd / self.mss
        self.w_last_max = (window * (2 - self.beta) / 2
                           if self.fast_convergence and window < self.w_last_max
                           else window)
        if in_nvp:
            # RFC 7661 Section 4.4.1 loss response during Non-Validated Period
            ref = max(float(pipe_ack_bytes or 0), float(loss_flight_bytes or 0))
            self.ssthresh = max(2.0 * self.mss, ref / 2.0)
        else:
            self.ssthresh = max(2.0 * self.mss, self.cwnd * (1.0 - self.beta))
        self.cwnd = self.ssthresh
        self.in_recovery = True

    def on_rto(self) -> None:
        self._disable_hystart()
        previous_window = self.cwnd / self.mss
        self.ssthresh = max(2 * self.mss, self.cwnd * (1 - self.beta))
        self.cwnd = self.mss
        self.in_recovery = False
        self.w_last_max = previous_window
        self._reset()

    def on_nvp_expired(self) -> None:
        super().on_nvp_expired()
        self.epoch_start = 0.0

    def on_tx_start(self, idle_us: int) -> None:
        """RFC 9438 Section 5.8 (Linux CA_EVENT_TX_START): shift epoch_start across
        true idle gaps where no ACKs arrived."""
        if idle_us > 0 and self.epoch_start > 0.0:
            idle_s = idle_us / 1e6
            self.epoch_start += idle_s
            if self.last_ack_s > 0.0:
                self.last_ack_s += idle_s
