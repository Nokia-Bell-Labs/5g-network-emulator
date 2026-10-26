/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pdcp_layer/packet_handler.h>
#include <traffic_models/traffic_model.h>

class simulated_packet_handler : public packet_handler
{
public:
    simulated_packet_handler(int ue_id, traffic_config traffic_c, pdcp_config pdcp_c, unsigned int seed, int verbosity = 0);

    float ingest(int tx_dir, float current_t) override;
    void drop(harq_pkt pkt, bit_fate fate) override;
    void drop_ingress_pkt(ip_pkt pkt) override;
    void flush_released() override;
    float release() override;
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
    struct pending_packet_result
    {
        std::uint64_t original_size = 0;
        std::uint64_t accounted_bits = 0;
        bool dropped = false;
        bool congestion_signal = false;
    };

    void update_pending_packet(const ip_pkt& pkt, bit_fate fate);
    void packetize(
        std::uint64_t bits,
        float current_t,
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
    std::unordered_map<std::uint32_t, object_counters> objects_;
    bool object_events_enabled_ = false;
    std::unordered_map<std::uint32_t, object_counters> object_events_;
};
