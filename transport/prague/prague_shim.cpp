/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

// A C ABI around L4S's reference Prague, with its clock replaced by the simulation's.
//
// Nothing here is an algorithm. `prague_cc.cpp` is 500 lines of deliberately
// integer-exact fixed point arithmetic and wrap-around-safe 32 bit time, and a hand
// port to Python would be a source of silent divergence from the implementation
// everyone else compares against. So it is compiled as it stands and called through
// ctypes, and the only thing this file does is make `Now()` answer with simulated
// microseconds instead of the wall clock.
//
// The one subtlety is the constructor: it calls `Now()` before the override exists, and
// the reference's first call always returns 1 whatever the wall clock says. So a
// controller is born at t=1µs, which is why the Python side offsets its clock by one.

#include <cstddef>   // the reference header uses size_t without including it
#include <cstdint>

#include "prague_cc.h"

namespace
{
class SimPrague : public PragueCC
{
public:
    SimPrague(size_tp mtu, rate_tp init_rate, count_tp init_window)
        : PragueCC(mtu, 0, 0, init_rate, init_window) {}

    time_tp now_us = 1;         // 0 is reserved by the reference as "unset"
    time_tp Now() override { return now_us; }
};
}

extern "C"
{

void *prague_new(uint64_t mtu, uint64_t init_rate_bytes_s, int32_t init_window_pkts)
{
    return new SimPrague(size_tp(mtu), rate_tp(init_rate_bytes_s),
                         count_tp(init_window_pkts));
}

void prague_delete(void *cc) { delete static_cast<SimPrague *>(cc); }

void prague_set_now(void *cc, int32_t now_us)
{
    static_cast<SimPrague *>(cc)->now_us = now_us;
}

// The round trip sample: our own timestamp comes back echoed, and the controller
// subtracts it from Now().
void prague_packet_received(void *cc, int32_t timestamp, int32_t echoed_timestamp)
{
    static_cast<SimPrague *>(cc)->PacketReceived(timestamp, echoed_timestamp);
}

int prague_ack_received(void *cc, int32_t received, int32_t ce, int32_t lost,
                        int32_t sent, int error_l4s, int32_t *inflight)
{
    count_tp in = 0;
    const bool used = static_cast<SimPrague *>(cc)->ACKReceived(
        received, ce, lost, sent, error_l4s != 0, in);
    if (inflight != nullptr) *inflight = in;
    return used ? 1 : 0;
}

void prague_cc_info(void *cc, uint64_t *pacing_rate, int32_t *packet_window,
                    int32_t *packet_burst, uint64_t *packet_size)
{
    rate_tp rate = 0;
    count_tp window = 0, burst = 0;
    size_tp size = 0;
    static_cast<SimPrague *>(cc)->GetCCInfo(rate, window, burst, size);
    if (pacing_rate != nullptr) *pacing_rate = rate;
    if (packet_window != nullptr) *packet_window = window;
    if (packet_burst != nullptr) *packet_burst = burst;
    if (packet_size != nullptr) *packet_size = size;
}

void prague_reset(void *cc) { static_cast<SimPrague *>(cc)->ResetCCInfo(); }

// What the controller thinks, for telemetry and for tests that need to see the state
// and not just its consequences.
void prague_stats(void *cc, int32_t *srtt_us, int32_t *vrtt_us, int64_t *alpha,
                  int32_t *cc_state, int32_t *packets_ce, int32_t *packets_lost)
{
    PragueState s;
    static_cast<SimPrague *>(cc)->GetStats(s);
    if (srtt_us != nullptr) *srtt_us = s.m_srtt;
    if (vrtt_us != nullptr) *vrtt_us = s.m_vrtt;
    if (alpha != nullptr) *alpha = s.m_alpha;
    if (cc_state != nullptr) *cc_state = int32_t(s.m_cc_state);
    if (packets_ce != nullptr) *packets_ce = s.m_packets_CE;
    if (packets_lost != nullptr) *packets_lost = s.m_packets_lost;
}
}
