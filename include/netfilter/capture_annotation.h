/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <netfilter/netfilter_interface.h>

//--------------------------------------------------------------------------------------------------
// Packet annotation: a research extension point at the NFQUEUE capture boundary.
//
// An annotator reads the bytes of a captured packet and fills a small fixed-size record, the
// ext_blob. The record travels with the capture record (captured_packet_info) up to
// captured_packet_handler::ingest, where each registered ingest observer receives it once, with the
// packet uid. The emulator never interprets the record: `kind` and `data` belong to whoever
// registered the annotator and the observer. ip_pkt and harq_pkt do not carry it.
//
// With nothing registered the capture path performs one atomic load per packet and nothing else.
//
// Registration rules (both registries):
//  - Registrations are made at start-up, typically from a file-scope object, before the first
//    packet is handled. Order of registration is order of invocation.
//  - Each registry freezes on its first use: annotators at the first call of annotate_packet (the
//    first captured packet of any queue), observers at the first call of
//    captured_packet_handler::ingest. A registration after that point throws std::logic_error. This
//    lets the reader threads walk an immutable list without a lock.
//  - A null function throws std::invalid_argument. A registry holds at most max_annotation_hooks
//    entries; one more throws std::length_error.
//  - There is no way to unregister or reset a registry in a running process.
//--------------------------------------------------------------------------------------------------

struct ext_blob
{
    std::uint8_t data[32]{};
    std::uint16_t kind = 0;
};

static_assert(std::is_trivially_copyable<ext_blob>::value,
              "ext_blob is copied by value with the capture record and must stay trivially copyable");
static_assert(sizeof(ext_blob) == 34 && alignof(ext_blob) == 2,
              "ext_blob layout changed: 32 data bytes plus a 16-bit kind");

constexpr std::size_t max_annotation_hooks = 16;

//--------------------------------------------------------------------------------------------------
// Annotators
//
// Called in registration order from pkt_capture::handle_capture, on the NFQUEUE reader thread of the
// queue that received the packet, before the packet is enqueued. Several queues have several reader
// threads, so an annotator may run concurrently with itself: it must not write shared state without
// its own synchronisation, must not allocate, and must not wait.
//
// An annotator reads meta (the payload is the whole IPv4 packet; the capture is bound to AF_INET)
// and may fill the blob. It returns true when it recognised the packet: the first annotator that
// returns true wins and the rest are skipped. It should then set a non-zero kind. When it returns
// false, whatever it wrote to the blob is discarded. A packet that no annotator recognises keeps
// kind 0 and zeroed data.
//
// An annotator that throws is treated as not having recognised the packet: the blob is reset, the
// exception is swallowed, annotator_failures() is incremented, and the next annotator is tried.
//--------------------------------------------------------------------------------------------------
using packet_annotator = bool (*)(const nfq_packet_metadata &meta, ext_blob &blob);

// Throws std::invalid_argument for a null function, std::logic_error after the freeze and
// std::length_error when the registry is full.
void register_annotator(packet_annotator fn);
std::size_t annotator_count();
std::uint64_t annotator_failures();

// Runs the registered annotators on one packet. The first call freezes the registry. Never throws.
void annotate_packet(const nfq_packet_metadata &meta, ext_blob &blob) noexcept;

//--------------------------------------------------------------------------------------------------
// Ingest observers
//
// Called in registration order from captured_packet_handler::ingest, exactly once for every captured
// packet, at the point where the capture record becomes an ip_pkt. They run on the thread that calls
// ingest, which is the phase thread of the UE that owns the queue (one thread per UE). They see
// every packet, including those whose blob has kind 0, and including packets that the handler
// drops right away because its completion queue is full. They must not block.
//
// uid is the NFQUEUE packet id, which ip_pkt::uid carries afterwards. tx_dir is the direction passed
// to ingest and queue_num identifies the capture queue.
//
// An observer that throws is swallowed and counted in ingest_observer_failures(); the remaining
// observers still run.
//--------------------------------------------------------------------------------------------------
using ingest_observer = void (*)(std::uint32_t uid, int tx_dir, int queue_num, const ext_blob &blob);

// Same errors as register_annotator.
void register_ingest_observer(ingest_observer fn);
std::size_t ingest_observer_count();
std::uint64_t ingest_observer_failures();

// Freezes the observer registry (idempotent) and returns the number of observers. Called at the
// start of every captured_packet_handler::ingest.
std::size_t freeze_ingest_observers();

// Calls every observer for one packet. Never throws.
void notify_ingest_observers(
    std::uint32_t uid,
    int tx_dir,
    int queue_num,
    const ext_blob &blob) noexcept;
