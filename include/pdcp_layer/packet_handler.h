/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <random>
#include <unordered_map>

#include <pdcp_layer/pdcp_config.h>
#include <pdcp_layer/pdcp_queue_status.h>
#include <pkts/pkts.h>
#include <traffic_models/traffic_config.h>
#include <utils/logging/mean_handler.h>

enum class final_packet_verdict
{
    ACCEPT,
    ACCEPT_CE,
    DROP
};

// Terminal state of a packet's bits, from the point of view of whoever handed them over.
// The three losses are kept apart because they ask the client for different things, and
// because the first two are easy to mistake for one another:
//
//   - expired is deterministic and about this packet: it sat longer than the budget.
//   - queue_dropped is mostly the AQM asking the sender to slow down. It is
//     probabilistic, it looks at the head of the queue and not at the packet it
//     sacrifices, and with the default 15 ms target it fires some twenty times earlier
//     than the 350 ms budget. A packet dropped here would have made it out well within
//     its budget.
//   - radio_dropped is the only one that means the link itself is bad.
enum class bit_fate
{
    delivered,
    // Past the delay budget.
    expired,
    // Anything the queue itself got rid of for a reason that is not the radio: an AQM
    // drop, a tail drop on a full buffer, or the queues being emptied when the UE was
    // detached. The detach is not broken out because the client asked for it and already
    // knows, so the only thing the count would add is noise.
    queue_dropped,
    // Retransmissions exhausted.
    radio_dropped
};

// What happened to the bits of one object. Cumulative and monotonic, like the per UE
// counters, so that two reads can be diffed.
struct object_counters
{
    std::uint64_t injected_bits = 0;
    std::uint64_t delivered_bits = 0;
    std::uint64_t expired_bits = 0;
    std::uint64_t queue_dropped_bits = 0;
    std::uint64_t radio_dropped_bits = 0;
    // Congestion marks, in bits of delivered payload. Per object rather than per UE
    // because a scalable sender needs the marks of its own flow, and a UE can carry
    // more than one.
    std::uint64_t ce_bits = 0;

    // Both drop causes as one number. The co-simulation spec closes an object with
    // delivered + dropped + expired == injected, so the sum stays available under the
    // name it has there, covering exactly what it used to cover.
    std::uint64_t dropped_bits() const
    {
        return queue_dropped_bits + radio_dropped_bits;
    }
};

class packet_handler
{
public:
    packet_handler(pdcp_config pdcp_c, unsigned int seed, int verbosity = 0);
    virtual ~packet_handler();

    virtual void init();
    virtual void quit();
    virtual void step(float t);
    virtual float ingest(int tx_dir, float current_t) = 0;
    bool has_ingress_pkts() const;
    ip_pkt pop_ingress_pkt();
    virtual void drop_ingress_pkt(ip_pkt pkt);
    virtual void push(harq_pkt pkt);
    virtual void drop(harq_pkt pkt, bit_fate fate);
    // Bits already granted on the air and waiting out the backhaul delay. A detach has to
    // resolve them too: nobody calls release() for a UE that is gone, so without this they
    // end up neither delivered nor lost. A captured source overrides push() and never
    // fills pkt_list, so for it this is a no-op and the netfilter verdicts are untouched.
    virtual void flush_released();
    virtual float release();
    virtual void fill_queue_status(pdcp_queue_status& status, float current_t) const;
    virtual std::uint64_t pending_release_bits() const;
    std::uint64_t pending_ingress_bits() const;

    float get_generated(bool partial = true);
    float get_error(bool partial = true);
    float get_ip_latency(bool elapsed = true);
    float get_latency(bool elapsed = true);
    float get_tp(bool elapsed = true);

    // Runtime control hooks. Only a simulated source has a target rate to change; a
    // captured one answers false and the registry turns that into an explicit error.
    virtual bool set_traffic_target(int tx_dir, float bps) { (void)tx_dir; (void)bps; return false; }
    virtual bool get_traffic_target(int tx_dir, float &bps) const { (void)tx_dir; (void)bps; return false; }

    // Client driven injection: the runtime twin of the file driven traffic_generator.
    // The caller decides when and how much; the emulator only packetizes and queues.
    // ecn is what the packets declare, which is what the AQM classifies on.
    virtual bool inject_bits(
        std::uint64_t bits,
        std::uint32_t tag,
        std::uint8_t ecn = ECN_NOT_ECT)
    { (void)bits; (void)tag; (void)ecn; return false; }
    virtual std::uint64_t injected_bits_total() const { return 0; }

    // Per object accounting. A source that cannot be injected into has no objects.
    virtual const std::unordered_map<std::uint32_t, object_counters> &objects() const
    {
        static const std::unordered_map<std::uint32_t, object_counters> none;
        return none;
    }
    virtual bool forget_object(std::uint32_t tag) { (void)tag; return false; }
    // The events control operation is opt-in, so existing get-only clients pay neither
    // the write nor the memory cost of maintaining deltas. Once enabled, every terminal
    // counter movement is accumulated by tag until control_manager collects it at the
    // next quiescent point.
    virtual void enable_object_events() {}
    virtual std::unordered_map<std::uint32_t, object_counters> take_object_events()
    { return {}; }
    virtual int get_pkt_size() const { return 0; }

    // Cumulative, monotonic counters. The client diffs two reads, so a lost read loses
    // nothing and the emulator keeps no "since last time" state.
    std::uint64_t delivered_bits_total() const
    {
        return delivered_bits_total_;
    }
    std::uint64_t expired_bits_total() const { return expired_bits_total_; }
    std::uint64_t queue_dropped_bits_total() const
    {
        return queue_dropped_bits_total_;
    }
    std::uint64_t radio_dropped_bits_total() const
    {
        return radio_dropped_bits_total_;
    }
    std::uint64_t dropped_bits_total() const
    {
        return queue_dropped_bits_total_ + radio_dropped_bits_total_;
    }
    int ce_packets_total() const { return ce_packets_total_; }
    std::uint64_t admitted_bits_total() const
    {
        return admitted_bits_total_;
    }

    // The caller states the cause; see bit_fate for why the three are not interchangeable.
    // Never called with delivered.
    void record_error(std::uint64_t bits, bit_fate fate);

protected:
    void push_ingress_pkt(ip_pkt pkt);
    void record_admitted_bits(std::uint64_t bits);
    virtual bool verdict(
        const ip_pkt& pkt,
        final_packet_verdict verdict);

protected:
    std::deque<ip_pkt> ingress_pkts;
    std::deque<harq_pkt> pkt_list;
    std::size_t release_high_water_ = 0;
    static constexpr std::size_t max_release_blocks_ = 65536;
    float current_t = 0;
    float bh_d = 0;
    float bh_d_var = 0;
    int verbosity = 0;

    mean_handler<float> tp_mean;
    mean_handler<float> l_mean;
    mean_handler<float> ipl_mean;
    mean_handler<float> g_mean;
    mean_handler<float> e_mean;
    std::uint64_t delivered_bits_total_ = 0;
    std::uint64_t expired_bits_total_ = 0;
    std::uint64_t queue_dropped_bits_total_ = 0;
    std::uint64_t radio_dropped_bits_total_ = 0;
    std::uint64_t admitted_bits_total_ = 0;
    int ce_packets_total_ = 0;
    int final_accept_packets_interval = 0;
    int final_accept_ce_packets_interval = 0;
    int final_drop_packets_interval = 0;

    std::mt19937 gauss_dist_gen;
    std::uniform_real_distribution<float> gauss_dist{-1,1};
};

struct packet_handler_config
{
    int ue_type;
    int tx_dir;
    int queue_num;
    int ue_id;
    std::chrono::microseconds *init_t;
    traffic_config traffic_c;
    pdcp_config pdcp_c;
    dualpi2_config l4s_c;
    bool log_traffic;
    bool log_quality;
};

std::unique_ptr<packet_handler> make_packet_handler(packet_handler_config cfg);
