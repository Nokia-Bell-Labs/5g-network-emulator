/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include <pdcp_layer/pdcp_layer.h>
#include <utils/rng_seed.h>
#include <utils/terminal_logging.h>

pdcp_layer::pdcp_layer(packet_handler_config handler_cfg, int _verbosity)
    : _harq_buffer(handler_cfg.pdcp_c.max_rtx, handler_cfg.pdcp_c.air_delay_var,
                   handler_cfg.pdcp_c.rtx_period, handler_cfg.pdcp_c.rtx_period_var,
                   handler_cfg.pdcp_c.rtx_proc_delay, handler_cfg.pdcp_c.rtx_proc_delay_var,
                   pdcp_rng_seed(handler_cfg.traffic_c.random_v, handler_cfg.ue_id,
                                 handler_cfg.tx_dir, PDCP_STREAM_HARQ),
                   _verbosity,
                   handler_cfg.pdcp_c.model),
      _ip_buffer(_verbosity),
      _packet_h(make_packet_handler(handler_cfg)),
      verbosity(_verbosity),
      tx_dir(handler_cfg.tx_dir)
{
    _ip_buffer.debug_queue_num = handler_cfg.queue_num;
    _ip_buffer.configure_l4s(handler_cfg.l4s_c,
                             pdcp_rng_seed(handler_cfg.traffic_c.random_v, handler_cfg.ue_id,
                                           handler_cfg.tx_dir, PDCP_STREAM_L4S));
    bh_d = handler_cfg.pdcp_c.bh_d;
    bh_d_var = handler_cfg.pdcp_c.bh_d_var;
}

pdcp_layer::~pdcp_layer()
{
    if (!_packet_h)
        return;
    try
    {
        exit();
    }
    catch (const std::exception &error)
    {
        LOG_ERROR_I("pdcp_layer::~pdcp_layer")
            << error.what() << END();
        std::terminate();
    }
}

void pdcp_layer::exit()
{
    if (!_packet_h)
        return;
    drop_all();
    _packet_h->quit();
}

void pdcp_layer::init(int _mod_i, int _layers, int _logic_units)
{
    (void)_layers;
    _harq_buffer.init(_mod_i, _logic_units);
    _packet_h->init();
}

float pdcp_layer::release()
{
	return _packet_h->release();
}

void pdcp_layer::step(float t)
{
    current_t = t;
    _ip_buffer.step(current_t);
    _harq_buffer.step(current_t);
    _packet_h->step(current_t);
    _packet_h->ingest(tx_dir, current_t);
    drain_ingress_pkts();
    cleanup_expired_pkts();
}

void pdcp_layer::drain_ingress_pkts()
{
    while(_packet_h->has_ingress_pkts())
    {
        ip_pkt pkt = _packet_h->pop_ingress_pkt();
        if(!_ip_buffer.add_pkt(pkt))
        {
            // A tail drop on a full buffer. Recorded here and not in drop_ingress_pkt
            // because that one only reaches the per object counters, and the per UE
            // totals have to add up the same way.
            _packet_h->record_error(pkt.size, bit_fate::queue_dropped);
            _packet_h->drop_ingress_pkt(std::move(pkt));
        }
    }
    harq_pkt dropped;
    while(_ip_buffer.pop_aqm_dropped_pkt(dropped))
    {
        _packet_h->record_error(dropped.bits, bit_fate::queue_dropped);
        _packet_h->drop(std::move(dropped), bit_fate::queue_dropped);
    }
}

void pdcp_layer::release_pkts(harq_pkt pkt)
{
    _packet_h->push(std::move(pkt));
}

bool pdcp_layer::has_pkts()
{
    return _ip_buffer.has_pkts() || _harq_buffer.is_pkt_ready();
}

float pdcp_layer::get_oldest_timestamp()
{
    if(_harq_buffer.is_pkt_ready())
    {
        return _harq_buffer.get_oldest_t();
    }
    else if(_ip_buffer.has_pkts())
    {
        return _ip_buffer.get_oldest_timestamp();
    }
    return current_t;
}

std::uint64_t pdcp_layer::quantize_grant(float bits)
{
    if (!std::isfinite(bits) || bits < 0.0f)
        throw std::invalid_argument(
            "scheduler grant must be finite and non-negative");
    const double capacity =
        static_cast<double>(bits) + grant_residual_bits_;
    if (capacity
        > static_cast<double>(
              std::numeric_limits<std::uint64_t>::max()))
        throw std::overflow_error("scheduler grant bit count overflow");
    const std::uint64_t whole_bits =
        static_cast<std::uint64_t>(std::floor(capacity));
    grant_residual_bits_ =
        capacity - static_cast<double>(whole_bits);
    return whole_bits;
}

float pdcp_layer::handle_pkt(
    float bits,
    int mcs,
    float sinr,
    float distance,
    int layers)
{
    last_charged_grant_bits_ = 0;
    const std::uint64_t grant_bits = quantize_grant(bits);
    if (grant_bits == 0)
        return 0.0f;

    if(_harq_buffer.is_pkt_ready())
    {
        const harq_pkt *ready = _harq_buffer.peek_oldest();
        if (ready != nullptr && ready->bits <= grant_bits)
        {
            harq_pkt pkt = _harq_buffer.get_pkt();
            if (is_expired(pkt))
            {
                drop_harq_pkt(std::move(pkt), bit_fate::expired);
                return 0.0f;
            }
            last_charged_grant_bits_ = pkt.bits;
            rtx_bits_total_ += pkt.bits;
            const int attempt = pkt.attempt_ordinal;
            if(_harq_buffer.get_rtx(
                   pkt.mcs_i,
                   sinr,
                   attempt,
                   pkt.layers))
            {
                if (!_harq_buffer.retry_available_after(attempt)
                    || !_harq_buffer.enqueue_retry(
                        pkt,
                        distance,
                        attempt + 1))
                    drop_harq_pkt(
                        std::move(pkt),
                        bit_fate::radio_dropped);
                return 0.0f;
            }
            const float effective_bits =
                static_cast<float>(pkt.bits);
            release_pkts(std::move(pkt));
            return effective_bits;
        }
        // A different HARQ process may use an undersized grant for fresh
        // data while the ready front block waits for a grant that can carry it.
    }

    if(!_ip_buffer.has_pkts())
        return 0.0f;

    harq_pkt pkt(
        current_id,
        _ip_buffer.get_oldest_timestamp(),
        current_t,
        mcs,
        distance,
        0,
        bh_d,
        bh_d_var);
    current_id++;
    pkt.layers = std::max(1, std::min(layers, 4));
    _ip_buffer.get_pkts(grant_bits, pkt);
    harq_pkt dropped;
    while(_ip_buffer.pop_aqm_dropped_pkt(dropped))
    {
        _packet_h->record_error(
            dropped.bits,
            bit_fate::queue_dropped);
        _packet_h->drop(
            std::move(dropped),
            bit_fate::queue_dropped);
    }
    if(pkt.bits == 0)
        return 0.0f;
    if(is_expired(pkt))
    {
        drop_harq_pkt(std::move(pkt), bit_fate::expired);
        return 0.0f;
    }

    last_charged_grant_bits_ = pkt.bits;
    if(_harq_buffer.get_rtx(
           mcs,
           sinr,
           0,
           pkt.layers))
    {
        if (!_harq_buffer.retry_available_after(0)
            || !_harq_buffer.enqueue_retry(
                pkt,
                distance,
                1))
            drop_harq_pkt(
                std::move(pkt),
                bit_fate::radio_dropped);
        return 0.0f;
    }

    const float effective_bits = static_cast<float>(pkt.bits);
    release_pkts(std::move(pkt));
    return effective_bits;
}

void pdcp_layer::drop_harq_pkt(harq_pkt pkt, bit_fate fate)
{
    _ip_buffer.drop_pkt(pkt.bits);
    _packet_h->record_error(pkt.bits, fate);
    _packet_h->drop(std::move(pkt), fate);
}

bool pdcp_layer::is_expired(float ip_t) const
{
    return ip_t < oldest_allowed_ip_t();
}

bool pdcp_layer::is_expired(const harq_pkt& pkt) const
{
    return is_expired(pkt.ip_t);
}

float pdcp_layer::oldest_allowed_ip_t() const
{
    return current_t - pkt_delay_budget_s;
}

float pdcp_layer::get_generated(bool partial)
{
    return _ip_buffer.get_generated(partial);
}

int pdcp_layer::get_generated_packets(bool partial)
{
    return _ip_buffer.get_generated_packets(partial);
}

float pdcp_layer::get_error(bool partial)
{
    return _ip_buffer.get_error(partial);
}

float pdcp_layer::get_latency(bool partial)
{
    return _packet_h->get_latency(partial);
}

float pdcp_layer::get_ip_latency(bool partial)
{
    return _packet_h->get_ip_latency(partial);
}

float pdcp_layer::get_tp(bool partial)
{
    return _packet_h->get_tp(partial);
}

bool pdcp_layer::using_l4s() const
{
    return _ip_buffer.using_dualpi2();
}

std::uint64_t pdcp_layer::pending_bits() const
{
    const std::uint64_t components[] = {
        _packet_h->pending_ingress_bits(),
        _ip_buffer.bits(),
        _harq_buffer.queued_bits(),
        _packet_h->pending_release_bits()};
    std::uint64_t total = 0;
    for (std::uint64_t component : components)
    {
        if (component
            > std::numeric_limits<std::uint64_t>::max() - total)
            throw std::overflow_error("PDCP pending bit count overflow");
        total += component;
    }
    return total;
}

std::int64_t pdcp_layer::conservation_residual_bits() const
{
    const std::uint64_t terminal_components[] = {
        _packet_h->delivered_bits_total(),
        _packet_h->expired_bits_total(),
        _packet_h->queue_dropped_bits_total(),
        _packet_h->radio_dropped_bits_total(),
        pending_bits()};
    std::uint64_t accounted = 0;
    for (std::uint64_t component : terminal_components)
    {
        if (component
            > std::numeric_limits<std::uint64_t>::max() - accounted)
            throw std::overflow_error("PDCP accounting total overflow");
        accounted += component;
    }
    const std::uint64_t admitted = _packet_h->admitted_bits_total();
    if (admitted >= accounted)
    {
        const std::uint64_t difference = admitted - accounted;
        if (difference
            > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()))
            throw std::overflow_error(
                "PDCP positive conservation residual overflow");
        return static_cast<std::int64_t>(difference);
    }
    const std::uint64_t difference = accounted - admitted;
    if (difference
        > static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max()))
        throw std::overflow_error(
            "PDCP negative conservation residual overflow");
    return -static_cast<std::int64_t>(difference);
}

// Emptying the queues of a detached UE. All of it counts as a queue drop: the client
// asked for the detach and knows it happened, so a cause of its own would only add a
// counter nobody reads.
void pdcp_layer::drop_all()
{
    while(_packet_h->has_ingress_pkts())
    {
        ip_pkt pkt = _packet_h->pop_ingress_pkt();
        // Same reason as the tail drop above: drop_ingress_pkt does not reach the per UE
        // totals, so they would stop adding up to what was injected.
        _packet_h->record_error(pkt.size, bit_fate::queue_dropped);
        _packet_h->drop_ingress_pkt(std::move(pkt));
    }

    while(_ip_buffer.has_pkts())
    {
        harq_pkt pkt(current_id, _ip_buffer.get_oldest_timestamp(), current_t, 0, 0, 0, bh_d, bh_d_var);
        current_id++;
        if(!_ip_buffer.pop_oldest_pkt(pkt)) break;
        drop_harq_pkt(std::move(pkt), bit_fate::queue_dropped);
    }

    harq_pkt rtx_pkt;
    while(_harq_buffer.pop_pkt_older_than(std::numeric_limits<float>::max(), rtx_pkt))
    {
        drop_harq_pkt(std::move(rtx_pkt), bit_fate::queue_dropped);
    }

    _packet_h->flush_released();
}

void pdcp_layer::cleanup_expired_pkts()
{
    cleanup_expired_ip_pkts();
    cleanup_expired_harq_pkts();
}

void pdcp_layer::cleanup_expired_ip_pkts()
{
    while(_ip_buffer.has_pkts())
    {
        float oldest_ip_t = _ip_buffer.get_oldest_timestamp();
        if(!is_expired(oldest_ip_t)) break;

        harq_pkt pkt(current_id, oldest_ip_t, current_t, 0, 0, 0, bh_d, bh_d_var);
        current_id++;

        if(!_ip_buffer.pop_oldest_pkt(pkt)) break;
        drop_harq_pkt(std::move(pkt), bit_fate::expired);
    }
}

void pdcp_layer::cleanup_expired_harq_pkts()
{
    harq_pkt pkt;
    while(_harq_buffer.pop_pkt_older_than(oldest_allowed_ip_t(), pkt))
    {
        drop_harq_pkt(std::move(pkt), bit_fate::expired);
    }
}

pdcp_queue_status pdcp_layer::get_queue_status() const
{
    pdcp_queue_status status;

    status.ip_buffer_size = _ip_buffer.size();
    const ip_pkt* oldest_ip = _ip_buffer.peek_oldest_pkt();
    if(oldest_ip != nullptr)
    {
        status.ip_oldest_uid = oldest_ip->uid;
        status.ip_oldest_age = current_t - oldest_ip->ip_t;
    }

    status.harq_size = _harq_buffer.size();
    const harq_pkt* oldest_harq = _harq_buffer.peek_oldest();
    if(oldest_harq != nullptr)
    {
        status.harq_oldest_id = oldest_harq->id;
        status.harq_oldest_age = current_t - oldest_harq->ip_t;
        status.harq_oldest_retry_ordinal =
            oldest_harq->attempt_ordinal;
    }
    status.harq_high_water_blocks =
        static_cast<int>(_harq_buffer.high_water_mark());
    status.harq_capacity_blocks =
        static_cast<int>(_harq_buffer.queue_capacity());
    status.harq_model =
        harq_model_name(_harq_buffer.configured_model());
    status.retransmitted_bits_total = rtx_bits_total_;
    status.radio_dropped_bits_total =
        _packet_h->radio_dropped_bits_total();
    status.last_charged_grant_bits =
        last_charged_grant_bits_;
    status.admitted_bits_total =
        _packet_h->admitted_bits_total();
    status.pending_bits_total = pending_bits();
    status.conservation_residual_bits =
        conservation_residual_bits();

    status.pkt_delay_budget_s = pkt_delay_budget_s;
    dualpi2_stats l4s_stats = _ip_buffer.get_l4s_stats();
    status.l4s_ce_packets = l4s_stats.ce_packets;
    status.l4s_ce_bits = l4s_stats.ce_bits;
    status.l4s_aqm_drops = l4s_stats.aqm_drops;
    status.l4s_aqm_drop_bits = l4s_stats.aqm_drop_bits;
    status.l4s_queue_size = l4s_stats.l4s_queue_size;
    status.classic_queue_size = l4s_stats.classic_queue_size;
    status.l4s_queue_bits = l4s_stats.l4s_queue_bits;
    status.classic_queue_bits = l4s_stats.classic_queue_bits;
    status.dualpi2_p_l = l4s_stats.p_l;
    status.dualpi2_p_c = l4s_stats.p_c;
    status.dualpi2_p_cl = l4s_stats.p_cl;
    _packet_h->fill_queue_status(status, current_t);
    return status;
}

dualpi2_stats pdcp_layer::get_l4s_interval_stats()
{
    return _ip_buffer.get_l4s_interval_stats();
}
