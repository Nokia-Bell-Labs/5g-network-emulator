# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Prague, bound to the L4S reference implementation rather than reimplemented.

The controller is `prague_cc.cpp` from the L4S team's `udp_prague`, compiled into
`prague/libpraguesim.so` with its clock overridden to read simulated time. See
`prague/prague_shim.cpp` for why binding was preferred to porting.

What this file does is translate between two vocabularies. The sender speaks bytes,
segments and a congestion window; Prague speaks a pacing rate, a window in packets
and the four counters a receiver echoes back. The translation is mechanical, but
two points are decisions and are marked as such below: the initial rate, and what
happens on duplicate acknowledgements.
"""
from __future__ import annotations

import ctypes
import os

from .cc import AckInfo

_LIB_NAME = "libpraguesim.so"
_DEFAULT_LIB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            "prague", _LIB_NAME)
_lib = None


class PragueUnavailable(RuntimeError):
    pass


def load_library(path: str | None = None):
    """Loads the binding, or says exactly how to build it."""
    global _lib
    if _lib is not None and path is None:
        return _lib
    target = path or os.environ.get("FIKORE_PRAGUE_LIB") or _DEFAULT_LIB
    if not os.path.exists(target):
        raise PragueUnavailable(
            f"{target} is not built. Run `make -C prague`, with PRAGUE_DIR pointing at "
            f"the L4S udp_prague tree if it is not at ../../L4STeam/udp_prague.")
    lib = ctypes.CDLL(target)
    lib.prague_new.argtypes = [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_int32]
    lib.prague_new.restype = ctypes.c_void_p
    lib.prague_delete.argtypes = [ctypes.c_void_p]
    lib.prague_set_now.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.prague_packet_received.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    lib.prague_ack_received.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32,
                                        ctypes.c_int32, ctypes.c_int32, ctypes.c_int,
                                        ctypes.POINTER(ctypes.c_int32)]
    lib.prague_ack_received.restype = ctypes.c_int
    lib.prague_cc_info.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64),
                                   ctypes.POINTER(ctypes.c_int32),
                                   ctypes.POINTER(ctypes.c_int32),
                                   ctypes.POINTER(ctypes.c_uint64)]
    lib.prague_reset.argtypes = [ctypes.c_void_p]
    lib.prague_stats.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32),
                                 ctypes.POINTER(ctypes.c_int32),
                                 ctypes.POINTER(ctypes.c_int64),
                                 ctypes.POINTER(ctypes.c_int32),
                                 ctypes.POINTER(ctypes.c_int32),
                                 ctypes.POINTER(ctypes.c_int32)]
    if path is None:
        _lib = lib
    return lib


class Prague:
    """The `CongestionControl` interface, over the reference controller."""

    # Prague's own default is 100 kbps, which is right for a sender that knows
    # nothing about its path and wrong for a cell that is known to carry tens of
    # Mbps: the ramp would dominate every short transfer measured here. The default
    # is instead ten segments over the reference RTT, which is the same argument
    # that gives the classic controllers IW10.
    def __init__(self, mss: int = 1500, cwnd: float | None = None,
                 init_rate_bps: float | None = None, init_window_pkts: int = 10,
                 lib_path: str | None = None) -> None:
        self.lib = load_library(lib_path)
        self.mss = mss
        rate_bytes_s = int((init_rate_bps or (init_window_pkts * mss * 8 / 0.025)) / 8)
        self._cc = self.lib.prague_new(mss, rate_bytes_s, init_window_pkts)
        if not self._cc:
            raise PragueUnavailable("the controller could not be created")
        self.cwnd = float(cwnd if cwnd is not None else init_window_pkts * mss)
        self.in_flight_pkts = 0
        self._rate_bytes_s = float(rate_bytes_s)
        self._packet_size = mss
        self._refresh()

    def __del__(self) -> None:
        cc = getattr(self, "_cc", None)
        if cc:
            self.lib.prague_delete(cc)
            self._cc = None

    # -- the interface ------------------------------------------------------------

    def on_ack(self, ack: AckInfo) -> None:
        # Simulated time, offset by one because the reference reserves 0 and is born
        # at 1. The wrap at 2^31 µs is the controller's own design, not a limit here.
        self.lib.prague_set_now(self._cc, ctypes.c_int32((ack.now_us + 1) & 0x7FFFFFFF))
        if ack.rtt_us is not None:
            # The echoed timestamp is what the controller subtracts from Now(); the
            # peer's own timestamp is only ever echoed back, and nothing in this
            # model reads it.
            echoed = (ack.now_us + 1 - ack.rtt_us) & 0x7FFFFFFF
            self.lib.prague_packet_received(self._cc, ctypes.c_int32(ack.now_us + 1),
                                            ctypes.c_int32(echoed))
        inflight = ctypes.c_int32(0)
        self.lib.prague_ack_received(self._cc, ack.pkts_received, ack.pkts_ce,
                                     ack.pkts_lost, ack.pkts_sent, 0,
                                     ctypes.byref(inflight))
        self.in_flight_pkts = inflight.value
        self._refresh()

    def on_dupacks(self, count: int) -> None:
        """Nothing. Prague reduces on the counters it is given, not on this event.

        A classic sender infers congestion from three duplicate acknowledgements
        because that is all it has. Prague is told, by the loss and CE counters the
        receiver echoes, and reacting here as well would be reducing twice for one
        event. The sender still retransmits: retransmission is its business, and the
        window is the controller's.
        """

    def on_recovered(self) -> None:
        """Also nothing, for the same reason: there is no recovery episode to leave."""

    def on_rto(self) -> None:
        self.lib.prague_reset(self._cc)
        self._refresh()

    def pacing_rate_bps(self) -> float | None:
        return self._rate_bytes_s * 8.0

    # -- state --------------------------------------------------------------------

    def _refresh(self) -> None:
        rate = ctypes.c_uint64(0)
        window = ctypes.c_int32(0)
        burst = ctypes.c_int32(0)
        size = ctypes.c_uint64(0)
        self.lib.prague_cc_info(self._cc, ctypes.byref(rate), ctypes.byref(window),
                                ctypes.byref(burst), ctypes.byref(size))
        self._rate_bytes_s = float(rate.value)
        self._packet_size = int(size.value)
        self.packet_burst = int(burst.value)
        # Prague counts its window in packets of the size it chose, which below a few
        # Mbps is smaller than the segment the sender actually uses. Bytes is the
        # conversion that keeps the pipe the controller intended.
        self.cwnd = float(window.value) * self._packet_size

    def stats(self) -> dict:
        srtt = ctypes.c_int32(0)
        vrtt = ctypes.c_int32(0)
        alpha = ctypes.c_int64(0)
        state = ctypes.c_int32(0)
        ce = ctypes.c_int32(0)
        lost = ctypes.c_int32(0)
        self.lib.prague_stats(self._cc, ctypes.byref(srtt), ctypes.byref(vrtt),
                              ctypes.byref(alpha), ctypes.byref(state),
                              ctypes.byref(ce), ctypes.byref(lost))
        return {
            "srtt_us": srtt.value,
            "vrtt_us": vrtt.value,
            # alpha is fixed point over 1<<20 in the reference
            "alpha": alpha.value / float(1 << 20),
            "state": ("init", "cong_avoid", "in_loss", "in_cwr")[state.value],
            "pacing_rate_bps": self._rate_bytes_s * 8.0,
            "packet_size": self._packet_size,
            "cwnd": self.cwnd,
            "packets_ce": ce.value,
            "packets_lost": lost.value,
        }
