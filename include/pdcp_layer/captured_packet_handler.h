/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <netfilter/pkt_capture.h>
#include <pdcp_layer/packet_handler.h>

class captured_packet_handler : public packet_handler
{
public:
    captured_packet_handler(int queue_num, std::chrono::microseconds *init_t, pdcp_config pdcp_c, unsigned int seed, int verbosity = 0);
    captured_packet_handler(std::unique_ptr<pkt_capture> capture, std::chrono::microseconds *init_t, pdcp_config pdcp_c, unsigned int seed, int verbosity = 0);
    ~captured_packet_handler() override;

    void init() override;
    void quit() override;
    float ingest(int tx_dir, float current_t) override;
    void drop_ingress_pkt(ip_pkt pkt) override;
    void push(harq_pkt pkt) override;
    void drop(harq_pkt pkt, bit_fate fate) override;
    void flush_released() override;
    float release() override;
    std::uint64_t pending_release_bits() const override;
    void fill_queue_status(pdcp_queue_status& status, float current_t) const override;

private:
    struct completion_state
    {
        explicit completion_state(const ip_pkt &source)
            : packet(source),
              original_bits(source.original_size)
        {
        }

        ip_pkt packet;
        std::uint64_t original_bits = 0;
        std::uint64_t accounted_bits = 0;
        std::uint64_t failed_bits = 0;
        bool failed = false;
        bit_fate failure_fate = bit_fate::queue_dropped;
        bool ce_marked = false;
        float ready_time = 0.0f;
    };

    float get_current_ts() const;
    completion_state &state_for(const ip_pkt &pkt);
    void account_fragment(
        const ip_pkt &pkt,
        bit_fate fate,
        float ready_time);
    bool state_is_complete(const completion_state &state) const;
    bool state_is_ready(const completion_state &state) const;
    final_packet_verdict state_verdict(
        const completion_state &state) const;
    bool release_state(std::uint32_t uid, float &delivered_bits);
    void force_drop_all();
    bool verdict(
        const ip_pkt& pkt,
        final_packet_verdict verdict) override;

private:
    bool do_check = true;
    std::chrono::microseconds *init_t = nullptr;
    uint32_t prev_uid = static_cast<uint32_t>(-1);
    int ce_rewrite_packets = 0;
    int drop_packets = 0;
    std::unique_ptr<pkt_capture> pkt_cptr;
    std::unordered_map<std::uint32_t, completion_state> completions;
    std::deque<std::uint32_t> completion_order;
    std::size_t completion_high_water = 0;
    std::unordered_set<std::uint32_t> finalized_uids;
    std::deque<std::uint32_t> finalized_order;
    static constexpr std::size_t max_completion_states = 65536;
    static constexpr std::size_t max_finalized_history = 65536;
    bool quitting = false;
};
