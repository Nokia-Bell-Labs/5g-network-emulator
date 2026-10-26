/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <deque>
#include <random>

#include <pdcp_layer/pdcp_config.h>
#include <pkts/pkts.h>

class dualpi2_queue
{
public:
    explicit dualpi2_queue(dualpi2_config cfg = dualpi2_config());

    void configure(dualpi2_config cfg);
    void seed_rng(unsigned int seed) { rng.seed(seed); }
    bool enqueue(ip_pkt pkt);
    bool has_pkts() const;
    bool pop_next(ip_pkt& pkt);
    bool has_dropped_pkts() const;
    bool pop_dropped_pkt(ip_pkt& pkt);
    bool pop_oldest(ip_pkt& pkt);
    const ip_pkt* peek_oldest() const;
    float oldest_timestamp(float current_t) const;
    void step(float current_t);
    void clear();

    int size() const;
    std::uint64_t bits() const { return l_bits + c_bits; }
    dualpi2_stats get_stats() const;
    dualpi2_stats get_interval_stats();
    const dualpi2_config& config() const { return cfg; }

private:
    bool is_l4s(const ip_pkt& pkt) const;
    float queue_delay(const std::deque<ip_pkt>& q) const;
    float laqm(float qdelay) const;
    bool mark_likelihood(float probability);
    bool choose_l4s() const;
    void mark_ce(ip_pkt& pkt);
    void record_drop(const ip_pkt& pkt);
    void update_snapshot();
    void maybe_update_pi();

private:
    dualpi2_config cfg;
    std::deque<ip_pkt> l_queue;
    std::deque<ip_pkt> c_queue;
    std::deque<ip_pkt> dropped_queue;
    std::uint64_t l_bits = 0;
    std::uint64_t c_bits = 0;
    float current_t = 0.0f;

    float p_base = 0.0f;
    float p_cl = 0.0f;
    float p_c = 0.0f;
    float p_l_last = 0.0f;
    float prev_q = 0.0f;
    float next_update_t = 0.0f;
    float last_classic_dequeue_t = 0.0f;

    dualpi2_stats total_stats;
    dualpi2_stats interval_stats;
    std::mt19937 rng;
    std::uniform_real_distribution<float> unit_dist;
};
