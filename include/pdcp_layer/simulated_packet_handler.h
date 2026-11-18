/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pdcp_layer/packet_handler.h>
#include <traffic_models/traffic_model.h>

class simulated_packet_handler : public packet_handler
{
public:
    simulated_packet_handler(
        int ue_id,
        traffic_config traffic_c,
        pdcp_config pdcp_c,
        unsigned int seed,
        int verbosity = 0,
        bool l4s_dual_queue = false);

    double ingest(int tx_dir, double current_t) override;
    void drop(harq_pkt pkt, bit_fate fate) override;
    void drop_ingress_pkt(ip_pkt pkt) override;
    void flush_released() override;
    float release() override;
    std::uint64_t pending_release_bits() const override;
    bool set_traffic_target(int tx_dir, float bps) override;
    bool get_traffic_target(int tx_dir, float &bps) const override;
    bool inject_bits(
        std::uint64_t bits,
        std::uint32_t tag,
        std::uint8_t ecn) override;
    std::uint64_t injected_bits_total() const override
    {
        return injected_bits_total_;
    }
    int get_pkt_size() const override { return traffic_m->get_pkt_size(0); }
    const std::unordered_map<std::uint32_t, object_counters> &objects() const override { return objects_; }
    bool forget_object(std::uint32_t tag) override { return objects_.erase(tag) > 0; }
    void enable_object_events() override
    {
        object_events_.clear();
        object_events_enabled_ = true;
    }
    std::unordered_map<std::uint32_t, object_counters> take_object_events() override
    {
        std::unordered_map<std::uint32_t, object_counters> out;
        out.swap(object_events_);
        return out;
    }

private:
    // A fragment that finished the backhaul but whose packet cannot be handed over yet because
    // an older packet is still in flight. Only used when pdcp_c.order_packets is set.
    struct held_fragment
    {
        held_fragment(const ip_pkt &fragment, const harq_pkt &block)
            : pkt(fragment),
              block_current_t(block.current_t),
              block_ip_t(block.ip_t)
        {
        }

        ip_pkt pkt;
        double block_current_t;
        double block_ip_t;
    };

    // What one call of release() handed over, for the throughput and latency averages.
    struct release_tally
    {
        std::uint64_t bits = 0;
        double latency = 0.0;
        double ip_latency = 0.0;
        int count = 0;
    };

    struct pending_packet_result
    {
        std::uint64_t original_size = 0;
        std::uint64_t accounted_bits = 0;
        bool dropped = false;
        // The packet is queued in release_order_ and has not had its turn yet. Fragments
        // of such a packet are held until it is whole and everything before it is settled.
        bool awaiting_turn = false;
        // The verdict and the packet fate have been reported.
        bool announced = false;
        // The fate of the first fragment that failed, which names the packet once it is whole.
        bit_fate failure_fate = bit_fate::queue_dropped;
        bool congestion_signal = false;
        std::vector<held_fragment> held;
    };

    // Entry point of every fragment that is lost, and of delivered fragments when the
    // packets are not ordered.
    void update_pending_packet(const ip_pkt& pkt, bit_fate fate);
    pending_packet_result &account_pending_fragment(
        const ip_pkt &pkt,
        bit_fate fate);
    void announce_packet(
        const ip_pkt &pkt,
        const pending_packet_result &state);
    void announce_if_settled(
        const ip_pkt &pkt,
        pending_packet_result &state);
    void account_object_fragment(const ip_pkt &pkt, bit_fate fate);

    // In-order delivery, used when pdcp_c.order_packets is set. Packets are handed over in the
    // order this handler created them, which is uid order. A packet that lost a fragment never
    // becomes whole, so it does not stop the ones behind it: its turn passes as soon as it is
    // known to be lost.
    void register_for_release(const ip_pkt &pkt);
    int order_queue_index(const ip_pkt &pkt) const;
    void receive_fragment(
        const ip_pkt &pkt,
        const harq_pkt &block,
        release_tally &tally);
    void advance_release_cursor(release_tally &tally);
    void advance_queue_cursor(std::deque<std::uint32_t> &queue, release_tally &tally);
    void deliver_held_fragments(
        pending_packet_result &state,
        release_tally &tally);
    void deliver_fragment(
        const ip_pkt &pkt,
        double block_current_t,
        double block_ip_t,
        release_tally &tally);
    void drop_held_fragments();
    void retire_if_settled(std::uint32_t uid);
    static bool is_whole(const pending_packet_result &state);

    void packetize(
        std::uint64_t bits,
        double current_t,
        std::uint32_t tag,
        std::uint8_t ecn);
    std::uint64_t quantize_generated_bits(float bits);

private:
    // One object's worth of bits waiting to be packetized, with what its packets
    // declare in the ECN field.
    struct pending_injection
    {
        std::uint32_t tag = 0;
        std::uint64_t bits = 0;
        std::uint8_t ecn = ECN_NOT_ECT;
    };

    std::unique_ptr<traffic_model> traffic_m;
    // Bits handed over by the client and not yet turned into IP packets. They go out on
    // the next ingest, in full: the pacing is the client's job, not the emulator's.
    // Injected bits waiting for the next ingest, kept per object so that each one is
    // packetized on its own and its byte count is conserved exactly.
    std::vector<pending_injection> pending_injections_;
    std::uint64_t injected_bits_total_ = 0;
    double generated_residual_bits_ = 0.0;
    int current_id = 0;
    std::unordered_map<uint32_t, pending_packet_result> pending_results;
    bool order_packets_ = false;
    // Uids of the packets created by packetize(), oldest first, whose turn has not come.
    // When DualPI2 is active, L4S (index 1) and classic (index 0) preserve order within
    // their own queue so L4S packets never wait behind classic traffic.
    std::deque<std::uint32_t> release_order_[2];
    std::uint64_t held_bits_ = 0;
    std::unordered_map<std::uint32_t, object_counters> objects_;
    bool object_events_enabled_ = false;
    std::unordered_map<std::uint32_t, object_counters> object_events_;
};
