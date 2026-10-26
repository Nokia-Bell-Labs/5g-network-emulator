/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include <mac_layer/mac_definitions.h>
#include <mac_layer/harq_model.h>
#include <pkts/pkts.h>

//--------------------------------------------------------------------------------------------------
// harq_handler(): it implements a buffer to queue the packets that have to be retransmitted. Wether
// a packet has to be retransmitted is also estimated by this class using a simple HARQ statistical
//model
// Input: 
//      * _max_rtx: max number of retransmissions
//      * _air_delay_var: added variance to the delay comming from the air propagation.
//      * _rtx_period: ack/nack delay in seconds
//      * _rtx_period_var: white noise spread around the ack/nack delay in seconds
//      * _rtx_proc_delay: ack/nack processing delay in seconds
//      * _rtx_proc_delay_var: white noise spread around the ack/nack processing delay in seconds
//      * _verbosity: enable/disable verbosity
//--------------------------------------------------------------------------------------------------
class harq_handler
{
public: 
    harq_handler(int _max_rtx, float _air_delay, 
                 float _rtx_period, float _rtx_period_var, 
                 float _rtx_p_delay, float _rtx_p_delay_var,
                 unsigned int _seed, int _verbosity = 0,
                 harq_model _model = harq_model::legacy_bler,
                 std::size_t _max_queue_blocks = 4096);
private: 
    int max_rtx; 
    float air_delay_var;
    float rtx_p_delay; 
    float rtx_p_delay_var;
    float rtx_period; 
    float rtx_period_var; 
    std::deque<harq_pkt> harq_buffer; 
    float current_t = 0.0;
    int verbosity = 0; 

    int mod_i; 
    int rbg_prbs;
    bool lookup_context_initialized = false;
    harq_model model;
    std::size_t max_queue_blocks;
    std::size_t high_water_blocks = 0;

    std::mt19937 bler_generator;
    std::mt19937 air_delay_generator;
    std::mt19937 rtx_period_generator;
    std::mt19937 processing_delay_generator;
    std::uniform_real_distribution<double> unit_probability{0.0, 1.0};
    std::uniform_real_distribution<float> signed_unit{-1.0f, 1.0f};
    std::deque<double> scripted_outcomes;
public: 
    void step(float t);
    bool enqueue_retry(
        harq_pkt &pkt,
        float distance,
        int retry_ordinal);
    bool is_pkt_ready() const;
    harq_pkt get_pkt();
    bool pop_pkt_older_than(float oldest_allowed_ip_t, harq_pkt& out_pkt);
    float get_oldest_t() const;
    bool get_rtx(int mcs, float sinr, int attempt_ordinal, int layers);
    void init(int _mod_i, int _rbg_prbs);
    int size() const { return (int)harq_buffer.size(); }
    const harq_pkt* peek_oldest() const;
    int maximum_retransmissions() const { return max_rtx; }
    bool retry_available_after(int attempt_ordinal) const
    {
        return attempt_ordinal < max_rtx;
    }
    harq_model configured_model() const { return model; }
    std::size_t high_water_mark() const { return high_water_blocks; }
    std::size_t queue_capacity() const { return max_queue_blocks; }
    std::uint64_t queued_bits() const;
    void set_scripted_outcomes(const std::vector<double> &outcomes);

    static double legacy_failure_probability(
        double bler,
        int attempt_ordinal);
    static double legacy_bler(
        int modulation_index,
        int rbg_prbs,
        int layers,
        int mcs,
        float sinr);

private: 
    float emulate_ack_delay(float distance);
    double draw_outcome();
};
