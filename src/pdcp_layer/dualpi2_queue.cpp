/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <algorithm>
#include <chrono>
#include <utility>

#include <pdcp_layer/dualpi2_queue.h>

dualpi2_queue::dualpi2_queue(dualpi2_config _cfg)
    : cfg(_cfg),
      rng((unsigned int)std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count()),
      unit_dist(0.0f, 1.0f)
{
}

void dualpi2_queue::configure(dualpi2_config _cfg)
{
    cfg = _cfg;
    p_base = 0.0f;
    p_cl = 0.0f;
    p_c = 0.0f;
    p_l_last = 0.0f;
    prev_q = 0.0f;
    next_update_t = current_t;
}

bool dualpi2_queue::enqueue(ip_pkt pkt)
{
    if(is_l4s(pkt))
    {
        l_bits += pkt.size;
        l_queue.push_back(std::move(pkt));
    }
    else
    {
        c_bits += pkt.size;
        c_queue.push_back(std::move(pkt));
    }
    update_snapshot();
    return true;
}

bool dualpi2_queue::has_pkts() const
{
    return !l_queue.empty() || !c_queue.empty();
}

bool dualpi2_queue::pop_next(ip_pkt& pkt)
{
    while(has_pkts())
    {
        bool from_l4s = choose_l4s();
        std::deque<ip_pkt>& q = from_l4s ? l_queue : c_queue;
        std::uint64_t& q_bits = from_l4s ? l_bits : c_bits;

        if(q.empty())
        {
            from_l4s = !from_l4s;
            std::deque<ip_pkt>& other_q = from_l4s ? l_queue : c_queue;
            std::uint64_t& other_bits = from_l4s ? l_bits : c_bits;
            pkt = std::move(other_q.front());
            other_q.pop_front();
            other_bits =
                pkt.size <= other_bits ? other_bits - pkt.size : 0;
        }
        else
        {
            pkt = std::move(q.front());
            q.pop_front();
            q_bits = pkt.size <= q_bits ? q_bits - pkt.size : 0;
        }

        if(is_l4s(pkt))
        {
            float p_l = std::max(laqm(std::max(0.0f, current_t - pkt.current_t)), p_cl);
            p_l = std::min(std::max(p_l, 0.0f), cfg.p_lmax);
            p_l_last = p_l;
            if(mark_likelihood(p_l)) mark_ce(pkt);
        }
        else
        {
            last_classic_dequeue_t = current_t;
            if(mark_likelihood(p_c))
            {
                if(pkt.ecn == ECN_ECT0 && p_c < 1.0f)
                {
                    mark_ce(pkt);
                }
                else
                {
                    pkt.aqm_dropped = true;
                    record_drop(pkt);
                    dropped_queue.push_back(std::move(pkt));
                    update_snapshot();
                    continue;
                }
            }
        }

        update_snapshot();
        return true;
    }
    update_snapshot();
    return false;
}

bool dualpi2_queue::has_dropped_pkts() const
{
    return !dropped_queue.empty();
}

bool dualpi2_queue::pop_dropped_pkt(ip_pkt& pkt)
{
    if(dropped_queue.empty()) return false;
    pkt = std::move(dropped_queue.front());
    dropped_queue.pop_front();
    return true;
}

bool dualpi2_queue::pop_oldest(ip_pkt& pkt)
{
    const ip_pkt* l_head = l_queue.empty() ? nullptr : &l_queue.front();
    const ip_pkt* c_head = c_queue.empty() ? nullptr : &c_queue.front();
    if(l_head == nullptr && c_head == nullptr) return false;

    bool take_l = c_head == nullptr || (l_head != nullptr && l_head->current_t <= c_head->current_t);
    std::deque<ip_pkt>& q = take_l ? l_queue : c_queue;
    std::uint64_t& q_bits = take_l ? l_bits : c_bits;
    pkt = std::move(q.front());
    q.pop_front();
    q_bits = pkt.size <= q_bits ? q_bits - pkt.size : 0;
    update_snapshot();
    return true;
}

const ip_pkt* dualpi2_queue::peek_oldest() const
{
    const ip_pkt* l_head = l_queue.empty() ? nullptr : &l_queue.front();
    const ip_pkt* c_head = c_queue.empty() ? nullptr : &c_queue.front();
    if(l_head == nullptr) return c_head;
    if(c_head == nullptr) return l_head;
    return l_head->current_t <= c_head->current_t ? l_head : c_head;
}

float dualpi2_queue::oldest_timestamp(float fallback_t) const
{
    const ip_pkt* pkt = peek_oldest();
    return pkt == nullptr ? fallback_t : pkt->current_t;
}

void dualpi2_queue::step(float _current_t)
{
    current_t = _current_t;
    maybe_update_pi();
    update_snapshot();
}

void dualpi2_queue::clear()
{
    l_queue.clear();
    c_queue.clear();
    dropped_queue.clear();
    l_bits = 0;
    c_bits = 0;
    update_snapshot();
}

int dualpi2_queue::size() const
{
    return (int)(l_queue.size() + c_queue.size());
}

dualpi2_stats dualpi2_queue::get_stats() const
{
    dualpi2_stats stats = total_stats;
    stats.l4s_queue_size = (int)l_queue.size();
    stats.classic_queue_size = (int)c_queue.size();
    stats.l4s_queue_bits = l_bits;
    stats.classic_queue_bits = c_bits;
    stats.p_l = p_l_last;
    stats.p_c = p_c;
    stats.p_cl = p_cl;
    return stats;
}

dualpi2_stats dualpi2_queue::get_interval_stats()
{
    dualpi2_stats stats = interval_stats;
    stats.l4s_queue_size = (int)l_queue.size();
    stats.classic_queue_size = (int)c_queue.size();
    stats.l4s_queue_bits = l_bits;
    stats.classic_queue_bits = c_bits;
    stats.p_l = p_l_last;
    stats.p_c = p_c;
    stats.p_cl = p_cl;
    interval_stats.ce_packets = 0;
    interval_stats.ce_bits = 0;
    interval_stats.aqm_drops = 0;
    interval_stats.aqm_drop_bits = 0;
    return stats;
}

bool dualpi2_queue::is_l4s(const ip_pkt& pkt) const
{
    return pkt.ecn == ECN_ECT1 || pkt.ecn == ECN_CE;
}

float dualpi2_queue::queue_delay(const std::deque<ip_pkt>& q) const
{
    if(q.empty()) return 0.0f;
    return std::max(0.0f, current_t - q.front().current_t);
}

float dualpi2_queue::laqm(float qdelay) const
{
    if(qdelay <= cfg.min_th_s) return 0.0f;
    if(cfg.range_s <= 0.0f) return cfg.p_lmax;
    return std::min(cfg.p_lmax, (qdelay - cfg.min_th_s) / cfg.range_s);
}

bool dualpi2_queue::mark_likelihood(float probability)
{
    if(probability <= 0.0f) return false;
    if(probability >= 1.0f) return true;
    return unit_dist(rng) < probability;
}

bool dualpi2_queue::choose_l4s() const
{
    if(l_queue.empty()) return false;
    if(c_queue.empty()) return true;
    if(cfg.classic_guard_s <= 0.0f) return true;
    return (current_t - last_classic_dequeue_t) < cfg.classic_guard_s;
}

void dualpi2_queue::mark_ce(ip_pkt& pkt)
{
    if(pkt.ecn == ECN_CE) return;
    pkt.ecn = ECN_CE;
    pkt.ce_marked = true;
    total_stats.ce_packets++;
    total_stats.ce_bits += pkt.size;
    interval_stats.ce_packets++;
    interval_stats.ce_bits += pkt.size;
}

void dualpi2_queue::record_drop(const ip_pkt& pkt)
{
    total_stats.aqm_drops++;
    total_stats.aqm_drop_bits += pkt.size;
    interval_stats.aqm_drops++;
    interval_stats.aqm_drop_bits += pkt.size;
}

void dualpi2_queue::update_snapshot()
{
    total_stats.l4s_queue_size = (int)l_queue.size();
    total_stats.classic_queue_size = (int)c_queue.size();
    total_stats.l4s_queue_bits = l_bits;
    total_stats.classic_queue_bits = c_bits;
    total_stats.p_l = p_l_last;
    total_stats.p_c = p_c;
    total_stats.p_cl = p_cl;
}

void dualpi2_queue::maybe_update_pi()
{
    float tupdate = std::min(cfg.target_s, cfg.rtt_max_s / 3.0f);
    if(tupdate <= 0.0f) tupdate = 0.001f;
    if(current_t < next_update_t) return;

    float curq = std::max(queue_delay(l_queue), queue_delay(c_queue));
    float alpha = 0.1f * tupdate / (cfg.rtt_max_s * cfg.rtt_max_s);
    float beta = 0.3f / cfg.rtt_max_s;
    p_base += alpha * (curq - cfg.target_s) + beta * (curq - prev_q);
    p_base = std::min(std::max(p_base, 0.0f), 1.0f);
    p_cl = std::min(std::max(p_base * cfg.k, 0.0f), 1.0f);
    p_c = std::min(std::max(p_base * p_base, 0.0f), 1.0f);
    prev_q = curq;
    next_update_t = current_t + tupdate;
}
