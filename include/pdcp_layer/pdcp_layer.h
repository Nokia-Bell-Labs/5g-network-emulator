/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <mac_layer/harq_handler.h>
#include <pdcp_layer/ip_buffer.h>
#include <pdcp_layer/packet_handler.h>
#include <pdcp_layer/pdcp_config.h>
#include <pdcp_layer/pdcp_queue_status.h>

// Orchestrates the common packet flow between ingress, HARQ, and packet release.
// Traffic source and final release behavior live in packet_handler implementations.
class pdcp_layer
{
public: 
    pdcp_layer(packet_handler_config handler_cfg, int _verbosity = 0);
    ~pdcp_layer();
    pdcp_layer(pdcp_layer &&) noexcept = default;
    pdcp_layer &operator=(pdcp_layer &&) = delete;
    pdcp_layer(const pdcp_layer &) = delete;
    pdcp_layer &operator=(const pdcp_layer &) = delete;
public: 
    void exit();
    void init(int _mod_i, int _layers, int _logic_units);
    float release(); 
    void step(float t);
    bool has_pkts();
    float get_oldest_timestamp();
    float handle_pkt(
        float bits,
        int mcs,
        float sinr,
        float distance,
        int layers);
    float get_generated(bool partial = true);
    int get_generated_packets(bool partial = true);
    float get_error(bool partial = true);
    float get_ip_latency(bool partial = true);
    float get_latency(bool partial = true);
    float get_tp(bool partial = true);
    bool using_l4s() const;
    pdcp_queue_status get_queue_status() const;
    dualpi2_stats get_l4s_interval_stats();
    void set_pkt_delay_budget(float budget_s) { pkt_delay_budget_s = budget_s; }
    float get_pkt_delay_budget() const { return pkt_delay_budget_s; }
    // Empties every live buffer through the existing drop paths. Used when a UE is
    // logically detached: for a real UE the drop path is what issues the netfilter
    // verdict, so packets must not be discarded silently or the kernel queue stalls.
    void drop_all();
    bool set_traffic_target(float bps) { return _packet_h->set_traffic_target(tx_dir, bps); }
    bool get_traffic_target(float &bps) const { return _packet_h->get_traffic_target(tx_dir, bps); }
    bool inject_bits(
        std::uint64_t bits,
        std::uint32_t tag,
        std::uint8_t ecn = ECN_NOT_ECT)
    { return _packet_h->inject_bits(bits, tag, ecn); }
    const std::unordered_map<std::uint32_t, object_counters> &objects() const { return _packet_h->objects(); }
    bool forget_object(std::uint32_t tag) { return _packet_h->forget_object(tag); }
    void enable_object_events() { _packet_h->enable_object_events(); }
    std::unordered_map<std::uint32_t, object_counters> take_object_events()
    { return _packet_h->take_object_events(); }

    // Everything the client needs to pace its own injection, cumulative where it makes
    // sense so that two reads can be diffed. Bits here; the control channel converts.
    std::uint64_t injected_bits_total() const
    {
        return _packet_h->injected_bits_total();
    }
    std::uint64_t delivered_bits_total() const
    {
        return _packet_h->delivered_bits_total();
    }
    // Bits that went over the air more than once. Counted here and not in the HARQ
    // handler because what the client reads is the state of the flow, not of the buffer.
    std::uint64_t retransmitted_bits_total() const
    {
        return rtx_bits_total_;
    }
    std::uint64_t expired_bits_total() const
    {
        return _packet_h->expired_bits_total();
    }
    std::uint64_t dropped_bits_total() const
    {
        return _packet_h->dropped_bits_total();
    }
    std::uint64_t queue_dropped_bits_total() const
    {
        return _packet_h->queue_dropped_bits_total();
    }
    std::uint64_t radio_dropped_bits_total() const
    {
        return _packet_h->radio_dropped_bits_total();
    }
    int ce_packets_total() const { return _packet_h->ce_packets_total(); }
    std::uint64_t pending_bits() const;
    std::int64_t conservation_residual_bits() const;
    std::uint64_t last_charged_grant_bits() const
    {
        return last_charged_grant_bits_;
    }
    void set_harq_scripted_outcomes(
        const std::vector<double> &outcomes)
    {
        _harq_buffer.set_scripted_outcomes(outcomes);
    }
    int get_pkt_size() const { return _packet_h->get_pkt_size(); }

private:
    void release_pkts(harq_pkt pkt);
    void drain_ingress_pkts();
    void cleanup_expired_pkts();
    void cleanup_expired_ip_pkts();
    void cleanup_expired_harq_pkts();
    void drop_harq_pkt(harq_pkt pkt, bit_fate fate);
    bool is_expired(float ip_t) const;
    bool is_expired(const harq_pkt& pkt) const;
    float oldest_allowed_ip_t() const;
    std::uint64_t quantize_grant(float bits);

protected: 
    ip_buffer _ip_buffer;
    harq_handler _harq_buffer;
    std::unique_ptr<packet_handler> _packet_h;
    int verbosity = 0;
    int tx_dir;
    
protected: 
    int current_id = 0; 
    
protected: 
    float bh_d; 
    float bh_d_var; 

public: 
    float current_t = 0;

protected:
    float pkt_delay_budget_s = 0.350f;
    std::uint64_t rtx_bits_total_ = 0;
    std::uint64_t last_charged_grant_bits_ = 0;
    double grant_residual_bits_ = 0.0;

private:
};
