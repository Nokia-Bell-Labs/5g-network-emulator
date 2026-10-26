/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <string>

struct pdcp_queue_status
{
    int ip_buffer_size = 0;
    int ip_oldest_uid = -1;
    float ip_oldest_age = -1.0f;

    int harq_size = 0;
    int harq_oldest_id = -1;
    float harq_oldest_age = -1.0f;
    int harq_oldest_retry_ordinal = -1;
    int harq_high_water_blocks = 0;
    int harq_capacity_blocks = 0;
    std::string harq_model;
    std::uint64_t retransmitted_bits_total = 0;
    std::uint64_t radio_dropped_bits_total = 0;
    std::uint64_t last_charged_grant_bits = 0;
    std::uint64_t admitted_bits_total = 0;
    std::uint64_t pending_bits_total = 0;
    std::int64_t conservation_residual_bits = 0;

    int capture_size = 0;
    int capture_oldest_uid = -1;
    float capture_oldest_age = -1.0f;

    int release_size = 0;
    int release_high_water = 0;
    int release_oldest_uid = -1;
    float release_oldest_age = -1.0f;
    int final_accept_packets = 0;
    int final_accept_ce_packets = 0;
    int final_drop_packets = 0;
    int nfqueue_queue_num = -1;
    int nfqueue_ce_rewrite_packets = 0;
    int nfqueue_drop_packets = 0;
    std::uint64_t nfqueue_total_recv = 0;
    std::uint64_t nfqueue_total_rlsd = 0;
    std::uint64_t nfqueue_bytes_recv = 0;
    std::uint64_t nfqueue_recv_fails = 0;
    std::uint64_t nfqueue_rlsd_fails = 0;

    float pkt_delay_budget_s = -1.0f;

    int l4s_ce_packets = 0;
    std::uint64_t l4s_ce_bits = 0;
    int l4s_aqm_drops = 0;
    std::uint64_t l4s_aqm_drop_bits = 0;
    int l4s_queue_size = 0;
    int classic_queue_size = 0;
    std::uint64_t l4s_queue_bits = 0;
    std::uint64_t classic_queue_bits = 0;
    float dualpi2_p_l = 0.0f;
    float dualpi2_p_c = 0.0f;
    float dualpi2_p_cl = 0.0f;
};
