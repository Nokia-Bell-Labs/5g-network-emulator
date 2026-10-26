/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <iostream>
#include <chrono>
#include <stdexcept>
#include <pdcp_layer/ip_buffer.h>
#include <utils/terminal_logging.h>

namespace
{
ip_pkt empty_pkt()
{
    return ip_pkt(0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f);
}
}

ip_buffer::ip_buffer(int _verbosity)
{
    verbosity = _verbosity; 
}

float ip_buffer::get_oldest_timestamp()
{
    if(backend_has_pkts()) return backend_oldest_timestamp();
    else return current_t; 
}

bool ip_buffer::has_pkts()
{
    return backend_has_pkts();
}

void ip_buffer::generate(
    std::uint64_t bits,
    std::uint64_t pkt_size,
    float t,
    float bh_d,
    float bh_d_var)
{
    if (pkt_size == 0)
        throw std::invalid_argument("packet size must be positive");
    if(current_size < max_size && bits > 0)
    {
        const std::uint64_t pkts = (bits + pkt_size - 1) / pkt_size;
        if(!backend_has_pkts()) oldest_t = t;
        for(std::uint64_t i = 0; i + 1 < pkts; i++)
        {
            add_pkt(ip_pkt(t, pkt_size, pkt_size, current_id, bh_d, bh_d_var));
            current_id++;
        }
        const std::uint64_t bits_left = bits - (pkts - 1) * pkt_size;
        if(bits_left > 0) 
        {
            add_pkt(ip_pkt(t, bits_left, bits_left, current_id, bh_d, bh_d_var));
            current_id++;
        }
    }
    else
    {
        if(bits > 0 && verbosity > 0)
        {
            e_mean.add(bits);
        }
        bits = 0; 
        
    }
    if(verbosity > 0) g_mean.add(bits);
}

std::uint64_t ip_buffer::drop_pkt(std::uint64_t bits)
{
    if(verbosity > 0) e_mean.add(bits);
    return bits; 
}

bool ip_buffer::add_pkt(ip_pkt pkt)
{
    if(pkt.size > max_size - current_size)
    {
        if(verbosity > 0) e_mean.add(pkt.size);
        return false;
    }

    current_size += pkt.size; 
    if(verbosity > 0) g_mean.add(pkt.size);
    generated_pkts_interval++;
    generated_pkts_total++;
    if(!backend_has_pkts()) oldest_t = pkt.current_t;
    backend_requeue_front(std::move(pkt));
    return true;
}

void ip_buffer::step(float _current_t){
    if(verbosity > 0) g_mean.step();
    if(verbosity > 0) e_mean.step();
    generated_pkts_interval = 0;
    current_t = _current_t;
    if(l4s_cfg.enabled) l4s_queue.step(current_t);
}

std::uint64_t ip_buffer::get_pkts(
    std::uint64_t bits,
    harq_pkt& out_pkt)
{
    out_pkt.bits = 0; 
    while(bits > 0 && backend_has_pkts())
    {
        ip_pkt pkt = empty_pkt();
        if(!backend_pop_next(pkt)) break;
        if(bits >= pkt.size)
        {
            oldest_t = pkt.current_t;
            bits -= pkt.size; 
            out_pkt.bits += pkt.size; 
            pkt.is_fragment = false;
            pkt.frags_created++;
            out_pkt.pkts.push_back(std::move(pkt));
        }
        else
        {
            ip_pkt pkt_f(pkt);
            pkt.size -= bits;
            out_pkt.bits += bits; 
            pkt_f.size = bits;
            pkt_f.is_fragment = true; 
            pkt_f.frags_created++;
            out_pkt.pkts.push_back(std::move(pkt_f));
            bits = 0; 
            pkt.is_fragment = true; 
            pkt.frags_created++;
            backend_requeue_front(std::move(pkt));
        } 
    }
    if (out_pkt.bits > current_size)
        throw std::logic_error("IP buffer bit accounting underflow");
    current_size -= out_pkt.bits;
    return out_pkt.bits; 
}

float ip_buffer::get_generated(bool partial)
{ 
    if(verbosity > 0) 
    {
        if(!partial)return BIT2MBIT*g_mean.get_total()*S2MS;
        else return BIT2MBIT*g_mean.get()*S2MS;
    }
    else return -1; 
}

int ip_buffer::get_generated_packets(bool partial)
{
    return partial ? generated_pkts_interval : generated_pkts_total;
}

float ip_buffer::get_error(bool partial)
{ 
    if(verbosity > 0) 
    {
        if(!partial)return BIT2MBIT*e_mean.get_total()*S2MS;
        else return BIT2MBIT*e_mean.get()*S2MS;
    }
    else return -1; 
}

bool ip_buffer::pop_oldest_pkt(harq_pkt& out_pkt)
{
    if(!backend_has_pkts()) return false;

    out_pkt.pkts.clear();
    out_pkt.bits = 0;

    ip_pkt pkt = empty_pkt();
    if(!backend_pop_oldest(pkt)) return false;

    out_pkt.bits = pkt.size;
    out_pkt.pkts.push_back(std::move(pkt));

    if (out_pkt.bits > current_size)
        throw std::logic_error("IP buffer bit accounting underflow");
    current_size -= out_pkt.bits;

    if(backend_has_pkts()) oldest_t = backend_oldest_timestamp();
    else oldest_t = current_t;

    return true;
}

const ip_pkt* ip_buffer::peek_oldest_pkt() const
{
    return backend_peek_oldest();
}

void ip_buffer::configure_l4s(dualpi2_config cfg, unsigned int seed)
{
    l4s_cfg = cfg;
    l4s_queue.configure(cfg);
    l4s_queue.seed_rng(seed);
}

dualpi2_stats ip_buffer::get_l4s_stats() const
{
    if(!l4s_cfg.enabled) return dualpi2_stats();
    return l4s_queue.get_stats();
}

dualpi2_stats ip_buffer::get_l4s_interval_stats()
{
    if(!l4s_cfg.enabled) return dualpi2_stats();
    return l4s_queue.get_interval_stats();
}

bool ip_buffer::pop_aqm_dropped_pkt(harq_pkt& out_pkt)
{
    if(!l4s_cfg.enabled || !l4s_queue.has_dropped_pkts()) return false;
    ip_pkt pkt(0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f);
    if(!l4s_queue.pop_dropped_pkt(pkt)) return false;
    out_pkt.pkts.clear();
    out_pkt.bits = pkt.size;
    out_pkt.ip_t = pkt.ip_t;
    out_pkt.current_t = current_t;
    out_pkt.t_out = current_t;
    out_pkt.pkts.push_back(std::move(pkt));
    if (out_pkt.bits > current_size)
        throw std::logic_error("IP buffer bit accounting underflow");
    current_size -= out_pkt.bits;
    return true;
}

bool ip_buffer::backend_has_pkts() const
{
    return l4s_cfg.enabled ? l4s_queue.has_pkts() : !pkt_list.empty();
}

int ip_buffer::backend_size() const
{
    return l4s_cfg.enabled ? l4s_queue.size() : (int)pkt_list.size();
}

bool ip_buffer::backend_pop_next(ip_pkt& pkt)
{
    if(l4s_cfg.enabled) return l4s_queue.pop_next(pkt);
    if(pkt_list.empty()) return false;
    pkt = std::move(pkt_list.front());
    pkt_list.pop_front();
    return true;
}

bool ip_buffer::backend_pop_oldest(ip_pkt& pkt)
{
    if(l4s_cfg.enabled) return l4s_queue.pop_oldest(pkt);
    if(pkt_list.empty()) return false;
    pkt = std::move(pkt_list.front());
    pkt_list.pop_front();
    return true;
}

void ip_buffer::backend_requeue_front(ip_pkt pkt)
{
    if(l4s_cfg.enabled)
    {
        l4s_queue.enqueue(std::move(pkt));
        return;
    }
    if(pkt.size <= pkt.original_size && pkt.is_fragment) pkt_list.push_front(std::move(pkt));
    else pkt_list.push_back(std::move(pkt));
}

const ip_pkt* ip_buffer::backend_peek_oldest() const
{
    if(l4s_cfg.enabled) return l4s_queue.peek_oldest();
    if(pkt_list.empty()) return nullptr;
    return &pkt_list.front();
}

float ip_buffer::backend_oldest_timestamp() const
{
    if(l4s_cfg.enabled) return l4s_queue.oldest_timestamp(current_t);
    if(pkt_list.empty()) return current_t;
    return pkt_list.front().current_t;
}
