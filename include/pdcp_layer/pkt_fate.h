/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <pdcp_layer/packet_handler.h>

//--------------------------------------------------------------------------------------------------
// Packet fates: a research extension point at the point where a packet's bits meet their final
// state.
//
// Every packet admitted by a packet handler ends in exactly one of the four bit_fate values. An
// observer receives one pkt_fate_event per packet, at the place where that fate is resolved, in the
// simulated handler and in the captured (live) handler alike. The emulator never interprets what
// the observer does with it.
//
// Observers only read. They change no counter, verdict, queue or ordering, so the bit-conservation
// residual keeps its meaning and a run gives the same counters with and without observers. With
// nothing registered each fate costs one byte load in the handler.
//
// Registration rules:
//  - Registrations are made at start-up, typically from a file-scope object, before the first
//    packet is handled. Order of registration is order of invocation.
//  - The registry freezes at the first packet_handler::step of any handler. A handler that is
//    used without a step freezes it at the first fate it resolves. A registration after that
//    point throws std::logic_error. This lets every UE thread walk an immutable list without a
//    lock.
//  - A null function throws std::invalid_argument. The registry holds at most max_fate_observers
//    entries; one more throws std::length_error.
//  - There is no way to unregister or reset the registry in a running process.
//--------------------------------------------------------------------------------------------------

// What happened to one packet. The numbers are those of the packet as it entered the handler.
//
//  - uid: the packet id of the handler. In live mode it is the NFQUEUE packet id; in simulated mode
//    it counts packets of one handler, so it repeats across UEs and directions. Together with
//    ue_id and tx_dir it identifies a packet.
//  - fate: the final fate. A packet whose fragments met different fates reports the first failure
//    that the handler saw; a packet with no failure reports delivered.
//  - bits: the original size of the packet, the whole of it whatever its fragments met.
//  - delay_s: time from the arrival of the packet (ip_pkt::ip_t) to the moment its fate is
//    resolved: for delivered, the release; for a drop, the drop. Never negative.
//  - ce: true only for a packet that was delivered with a CE mark.
//  - tx_dir: TX_DL or TX_UL, or -1 if the handler was not told.
//  - queue_num: the NFQUEUE number in live mode, -1 in simulated mode.
//  - ue_id: the id of the UE that owns the handler, or -1 if the handler was not told.
struct pkt_fate_event
{
    std::uint32_t uid;
    bit_fate fate;
    std::uint64_t bits;
    double delay_s;
    bool ce;
    int tx_dir;
    int queue_num;
    int ue_id;
};

static_assert(std::is_trivially_copyable<pkt_fate_event>::value,
              "pkt_fate_event is built on the stack for every fate and must stay trivially copyable");

constexpr std::size_t max_fate_observers = 16;

//--------------------------------------------------------------------------------------------------
// Observers
//
// Called in registration order, exactly once per packet uid, on the phase thread of the UE that
// owns the handler (one thread per UE). Different UEs run concurrently, so an observer must keep
// its state per UE or synchronise it, and it must not block.
//
// An observer that throws is swallowed and counted in fate_observer_failures(); the remaining
// observers still run, and nothing in the handler changes.
//--------------------------------------------------------------------------------------------------
using pkt_fate_observer = void (*)(const pkt_fate_event &);

// Throws std::invalid_argument for a null function, std::logic_error after the freeze and
// std::length_error when the registry is full.
void register_fate_observer(pkt_fate_observer fn);
std::size_t fate_observer_count();
std::uint64_t fate_observer_failures();

// Freezes the registry (idempotent) and returns the number of observers. Called by the handlers.
std::size_t freeze_fate_observers();

// Calls every observer for one event. Never throws.
void notify_pkt_fate(const pkt_fate_event &event) noexcept;
