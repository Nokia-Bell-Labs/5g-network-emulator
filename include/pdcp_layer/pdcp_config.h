/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>

#include <mac_layer/harq_model.h>

struct dualpi2_config
{
    bool enabled = false;
    float k = 2.0f;
    float target_s = 0.015f;
    float rtt_max_s = 0.100f;
    float min_th_s = 0.0008f;
    float range_s = 0.0004f;
    float p_lmax = 1.0f;
    float classic_guard_s = 0.001f;
};

struct dualpi2_stats
{
    int ce_packets = 0;
    std::uint64_t ce_bits = 0;
    int aqm_drops = 0;
    std::uint64_t aqm_drop_bits = 0;
    int l4s_queue_size = 0;
    int classic_queue_size = 0;
    std::uint64_t l4s_queue_bits = 0;
    std::uint64_t classic_queue_bits = 0;
    float p_l = 0.0f;
    float p_c = 0.0f;
    float p_cl = 0.0f;
};
//--------------------------------------------------------------------------------------------------
// pdcp_config(): PDCP configuration struct.
// Input: 
//              *max_rtx_ul/dl: max number of retransmissions for UL/DL HARQ packets.
//              *air_delay_var: added variance to the delay comming from the air propagation.
//              *rtx_period_ul/dl: ack/nack delay in seconds for UL/DL HARQ packets.
//              *rtx_period_ul/dl_var: white noise spread around the ack/nack delay in seconds for UL/DL HARQ packets.
//              *rtx_proc_delay_ul/dl: ack/nack processing delay in seconds for UL/DL HARQ packets.
//              *rtx_proc_delay_ul/dl_var: white noise spread around the ack/nack processing delay in seconds for UL/DL HARQ packets.
//              *backhaul_d: backhauling delay, use for Release buffer.
//              *backhaul_d_var: white noise spread around the backhauling delay.
//              *order_pkts: enable/disable RLC reordering.
//--------------------------------------------------------------------------------------------------
struct pdcp_config
{
    pdcp_config(){}
    pdcp_config(int _max_rtx, float _air_delay_var, 
                float _rtx_period, float _rtx_period_var, float _rtx_proc_delay, 
                float _rtx_proc_delay_var, float _bh_d, float _bh_d_var,
                bool _order_packets,
                harq_model _harq_model = harq_model::legacy_bler)
                {
                    max_rtx = _max_rtx; 
                    air_delay_var = _air_delay_var; 
                    rtx_period = _rtx_period; 
                    rtx_period_var = _rtx_period_var; 
                    rtx_proc_delay = _rtx_proc_delay;
                    rtx_proc_delay_var = _rtx_proc_delay_var;
                    bh_d = _bh_d; 
                    bh_d_var = _bh_d_var;
                    order_packets = _order_packets;
                    model = _harq_model;
                }
    int max_rtx = 4;
    float air_delay_var = 0.0f;
    float rtx_period = 0.004f;
    float rtx_period_var = 0.0f;
    float rtx_proc_delay = 0.001f;
    float rtx_proc_delay_var = 0.0f;
    float bh_d = 0.005f;
    float bh_d_var = 0.001f;
    bool order_packets = true;
    harq_model model = harq_model::legacy_bler;
};
