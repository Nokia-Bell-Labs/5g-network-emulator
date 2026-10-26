/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <pdcp_layer/captured_packet_handler.h>
#include <pdcp_layer/packet_handler.h>
#include <pdcp_layer/simulated_packet_handler.h>
#include <utils/rng_seed.h>
#include <simulator/configuration_loader.h>
#include <utils/conversions.h>

packet_handler::packet_handler(pdcp_config pdcp_c, unsigned int seed, int _verbosity)
    : gauss_dist_gen(seed)
{
    bh_d = pdcp_c.bh_d;
    bh_d_var = pdcp_c.bh_d_var;
    if (!std::isfinite(bh_d)
        || !std::isfinite(bh_d_var)
        || bh_d < 0.0f
        || bh_d_var < 0.0f)
        throw std::invalid_argument(
            "backhaul delays must be finite and non-negative seconds");
    verbosity = _verbosity;
}

packet_handler::~packet_handler()
{
}

void packet_handler::init()
{
}

void packet_handler::quit()
{
}

void packet_handler::step(float t)
{
    tp_mean.step();
    g_mean.step();
    e_mean.step();
    final_accept_packets_interval = 0;
    final_accept_ce_packets_interval = 0;
    final_drop_packets_interval = 0;
    current_t = t;
}

bool packet_handler::has_ingress_pkts() const
{
    return !ingress_pkts.empty();
}

ip_pkt packet_handler::pop_ingress_pkt()
{
    ip_pkt pkt = std::move(ingress_pkts.front());
    ingress_pkts.pop_front();
    return pkt;
}

void packet_handler::drop_ingress_pkt(ip_pkt pkt)
{
    (void)pkt;
}

void packet_handler::push(harq_pkt pkt)
{
    if (pkt_list.size() >= max_release_blocks_)
    {
        record_error(pkt.bits, bit_fate::queue_dropped);
        drop(std::move(pkt), bit_fate::queue_dropped);
        return;
    }
    pkt.t_out += std::max(
        0.0f,
        pkt.backhaul_d
            + gauss_dist(gauss_dist_gen) * pkt.backhaul_d_var);
    pkt_list.push_back(std::move(pkt));
    release_high_water_ =
        std::max(release_high_water_, pkt_list.size());
}

void packet_handler::drop(harq_pkt pkt, bit_fate fate)
{
    (void)pkt;
    (void)fate;
}

void packet_handler::flush_released()
{
    for(std::deque<harq_pkt>::const_iterator it = pkt_list.begin(); it != pkt_list.end(); ++it)
    {
        record_error(it->bits, bit_fate::queue_dropped);
    }
    pkt_list.clear();
}

float packet_handler::release()
{
    int count = 0;
    std::uint64_t bits = 0;
    float latency = 0;
    float ip_latency = 0;
    for(std::deque<harq_pkt>::iterator it=pkt_list.begin(); it!=pkt_list.end();)
    {
        if(it->t_out <= current_t)
        {
            bits += it->bits;
            latency += current_t - it->current_t;
            ip_latency += current_t - it->ip_t;
            count++;
            for(std::deque<ip_pkt>::const_iterator pkt_it = it->pkts.begin(); pkt_it != it->pkts.end(); ++pkt_it)
            {
                verdict(*pkt_it, pkt_it->ce_marked ? final_packet_verdict::ACCEPT_CE : final_packet_verdict::ACCEPT);
            }
            it = pkt_list.erase(it);
        }
        else it++;
    }
    if(count > 0)
    {
        l_mean.add(latency/count);
        ipl_mean.add(ip_latency/count);
        l_mean.step();
        ipl_mean.step();
    }
    tp_mean.add(bits);
    return bits;
}

void packet_handler::fill_queue_status(pdcp_queue_status& status, float current_t) const
{
    status.release_size = (int)pkt_list.size();
    status.release_high_water =
        static_cast<int>(release_high_water_);
    if(!pkt_list.empty())
    {
        const harq_pkt &pkt = pkt_list.front();
        status.release_oldest_uid = pkt.id;
        status.release_oldest_age = current_t - pkt.ip_t;
    }
    status.final_accept_packets = final_accept_packets_interval;
    status.final_accept_ce_packets = final_accept_ce_packets_interval;
    status.final_drop_packets = final_drop_packets_interval;
}

std::uint64_t packet_handler::pending_ingress_bits() const
{
    std::uint64_t bits = 0;
    for (const ip_pkt &pkt : ingress_pkts)
    {
        if (pkt.size > std::numeric_limits<std::uint64_t>::max() - bits)
            throw std::overflow_error("pending ingress bit count overflow");
        bits += pkt.size;
    }
    return bits;
}

std::uint64_t packet_handler::pending_release_bits() const
{
    std::uint64_t bits = 0;
    for (const harq_pkt &pkt : pkt_list)
    {
        if (pkt.bits > std::numeric_limits<std::uint64_t>::max() - bits)
            throw std::overflow_error("pending release bit count overflow");
        bits += pkt.bits;
    }
    return bits;
}

float packet_handler::get_generated(bool partial)
{
    if(verbosity <= 0) return -1;
    if(!partial) return BIT2MBIT * g_mean.get_total() * S2MS;
    return BIT2MBIT * g_mean.get() * S2MS;
}

float packet_handler::get_error(bool partial)
{
    if(verbosity <= 0) return -1;
    if(!partial) return BIT2MBIT * e_mean.get_total() * S2MS;
    return BIT2MBIT * e_mean.get() * S2MS;
}

float packet_handler::get_latency(bool elapsed)
{
    if(!elapsed)return l_mean.get_total();
    else return l_mean.get();
}

float packet_handler::get_ip_latency(bool elapsed)
{
    if(!elapsed)return ipl_mean.get_total();
    else return ipl_mean.get();
}

float packet_handler::get_tp(bool elapsed)
{
    if(!elapsed)return BIT2MBIT*tp_mean.get_total()*S2MS;
    else return BIT2MBIT*tp_mean.get()*S2MS;
}

void packet_handler::record_error(std::uint64_t bits, bit_fate fate)
{
    if(verbosity > 0) e_mean.add(bits);
    switch(fate)
    {
        case bit_fate::expired:        expired_bits_total_ += bits; break;
        case bit_fate::queue_dropped:  queue_dropped_bits_total_ += bits; break;
        case bit_fate::radio_dropped:  radio_dropped_bits_total_ += bits; break;
        case bit_fate::delivered:      break;
    }
}

void packet_handler::push_ingress_pkt(ip_pkt pkt)
{
    record_admitted_bits(pkt.size);
    if(verbosity > 0) g_mean.add(pkt.size);
    ingress_pkts.push_back(std::move(pkt));
}

void packet_handler::record_admitted_bits(std::uint64_t bits)
{
    if (bits
        > std::numeric_limits<std::uint64_t>::max()
            - admitted_bits_total_)
        throw std::overflow_error("admitted packet bit count overflow");
    admitted_bits_total_ += bits;
}

bool packet_handler::verdict(
    const ip_pkt&,
    final_packet_verdict verdict_value)
{
    switch(verdict_value)
    {
    case final_packet_verdict::ACCEPT:
        final_accept_packets_interval++;
        break;
    case final_packet_verdict::ACCEPT_CE:
        final_accept_ce_packets_interval++;
        ce_packets_total_++;
        break;
    case final_packet_verdict::DROP:
        final_drop_packets_interval++;
        break;
    }
    return true;
}

std::unique_ptr<packet_handler> make_packet_handler(packet_handler_config cfg)
{
    const unsigned int seed = pdcp_rng_seed(cfg.traffic_c.random_v, cfg.ue_id, cfg.tx_dir, PDCP_STREAM_PACKET_HANDLER);

    if(cfg.ue_type == SIM_UE)
    {
        std::unique_ptr<packet_handler> handler(new simulated_packet_handler(cfg.ue_id, cfg.traffic_c, cfg.pdcp_c, seed, cfg.log_traffic));
        return handler;
    }

    assert(cfg.queue_num >= 0);
    std::unique_ptr<packet_handler> handler(new captured_packet_handler(cfg.queue_num, cfg.init_t, cfg.pdcp_c, seed, cfg.log_quality));
    return handler;
}
