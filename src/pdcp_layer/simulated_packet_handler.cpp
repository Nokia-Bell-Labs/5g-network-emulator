/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <cmath>
#include <limits>
#include <stdexcept>

#include <pdcp_layer/simulated_packet_handler.h>

namespace
{
void account_fragment(object_counters &counters, const ip_pkt &pkt, bit_fate fate)
{
    if(fate == bit_fate::delivered)
    {
        counters.delivered_bits += pkt.size;
        // A mark only means anything on bits that arrived; a marked fragment that is
        // then dropped is a loss, and counting it twice would tell the sender to back
        // off twice for one event.
        if(pkt.ce_marked) counters.ce_bits += pkt.size;
    }
    else if(fate == bit_fate::expired) counters.expired_bits += pkt.size;
    else if(fate == bit_fate::radio_dropped) counters.radio_dropped_bits += pkt.size;
    else counters.queue_dropped_bits += pkt.size;
}
}

simulated_packet_handler::simulated_packet_handler(int ue_id, traffic_config traffic_c, pdcp_config pdcp_c, unsigned int seed, int verbosity)
    : packet_handler(pdcp_c, seed, verbosity),
      traffic_m(new traffic_model(ue_id, traffic_c))
{
}

bool simulated_packet_handler::set_traffic_target(int tx_dir, float bps)
{
    traffic_m->set_target(tx_dir, bps);
    return true;
}

bool simulated_packet_handler::get_traffic_target(int tx_dir, float &bps) const
{
    bps = traffic_m->get_target(tx_dir);
    return true;
}

bool simulated_packet_handler::inject_bits(
    std::uint64_t bits,
    std::uint32_t tag,
    std::uint8_t ecn)
{
    if(bits == 0) return true;
    if (bits
        > std::numeric_limits<std::uint64_t>::max()
            - injected_bits_total_)
        return false;
    for(size_t i = 0; i < pending_injections_.size(); i++)
    {
        // Same object and same marking merge; a different marking does not, because
        // the two would produce different packets.
        if(pending_injections_[i].tag == tag && pending_injections_[i].ecn == ecn)
        {
            if (bits
                > std::numeric_limits<std::uint64_t>::max()
                    - pending_injections_[i].bits)
                return false;
            pending_injections_[i].bits += bits;
            injected_bits_total_ += bits;
            if(tag != 0)
            {
                objects_[tag].injected_bits += bits;
            }
            return true;
        }
    }
    pending_injection p;
    p.tag = tag;
    p.bits = bits;
    p.ecn = ecn;
    pending_injections_.push_back(p);
    injected_bits_total_ += bits;
    if(tag != 0)
    {
        objects_[tag].injected_bits += bits;
    }
    return true;
}

void simulated_packet_handler::packetize(
    std::uint64_t bits,
    float current_t,
    std::uint32_t tag,
    std::uint8_t ecn)
{
    if (bits == 0)
        return;
    const int configured_packet_size = traffic_m->get_pkt_size(0);
    if (configured_packet_size <= 0)
        throw std::invalid_argument("packet size must be positive");
    const std::uint64_t packet_size =
        static_cast<std::uint64_t>(configured_packet_size);
    const std::uint64_t packets =
        (bits + packet_size - 1) / packet_size;
    for(std::uint64_t i = 0; i + 1 < packets; i++)
    {
        ip_pkt pkt(
            current_t,
            packet_size,
            packet_size,
            current_id,
            bh_d,
            bh_d_var);
        pkt.tag = tag;
        pkt.ecn = ecn;
        pkt.original_ecn = ecn;
        push_ingress_pkt(std::move(pkt));
        current_id++;
    }

    const std::uint64_t bits_left =
        bits - (packets - 1) * packet_size;
    if(bits_left > 0)
    {
        ip_pkt pkt(current_t, bits_left, bits_left, current_id, bh_d, bh_d_var);
        pkt.tag = tag;
        pkt.ecn = ecn;
        pkt.original_ecn = ecn;
        push_ingress_pkt(std::move(pkt));
        current_id++;
    }
}

std::uint64_t simulated_packet_handler::quantize_generated_bits(float bits)
{
    if (!std::isfinite(bits) || bits < 0.0f)
        throw std::invalid_argument(
            "generated traffic bits must be finite and non-negative");
    const double capacity =
        static_cast<double>(bits) + generated_residual_bits_;
    if (capacity
        > static_cast<double>(
              std::numeric_limits<std::uint64_t>::max()))
        throw std::overflow_error("generated traffic bit count overflow");
    const std::uint64_t whole_bits =
        static_cast<std::uint64_t>(std::floor(capacity));
    generated_residual_bits_ =
        capacity - static_cast<double>(whole_bits);
    return whole_bits;
}

float simulated_packet_handler::ingest(int tx_dir, float current_t)
{
    const float generated_sample =
        traffic_m->generate(tx_dir, current_t);
    if (!std::isfinite(generated_sample))
        throw std::invalid_argument(
            "generated traffic bits must be finite");
    const std::uint64_t generated =
        generated_sample > 0.0f
            ? quantize_generated_bits(generated_sample)
            : 0;
    if(generated > 0) packetize(generated, current_t, 0, ECN_NOT_ECT);

    // Injected bits are packetized on their own, one object at a time, so that an
    // injection of N bytes always yields the same packets regardless of what the
    // generator produced in the same step and of what the other objects injected.
    // Injection adds to the configured traffic, it does not replace it.
    std::uint64_t injected = 0;
    for(size_t i = 0; i < pending_injections_.size(); i++)
    {
        injected += pending_injections_[i].bits;
        packetize(pending_injections_[i].bits, current_t, pending_injections_[i].tag,
                  pending_injections_[i].ecn);
    }
    pending_injections_.clear();

    return generated + injected;
}

void simulated_packet_handler::drop(harq_pkt pkt, bit_fate fate)
{
    for(std::deque<ip_pkt>::const_iterator it = pkt.pkts.begin(); it != pkt.pkts.end(); ++it)
    {
        update_pending_packet(*it, fate);
    }
}

void simulated_packet_handler::drop_ingress_pkt(ip_pkt pkt)
{
    update_pending_packet(pkt, bit_fate::queue_dropped);
}

void simulated_packet_handler::flush_released()
{
    for(std::deque<harq_pkt>::const_iterator it = pkt_list.begin(); it != pkt_list.end(); ++it)
    {
        for(std::deque<ip_pkt>::const_iterator pkt_it = it->pkts.begin(); pkt_it != it->pkts.end(); ++pkt_it)
        {
            update_pending_packet(*pkt_it, bit_fate::queue_dropped);
        }
    }
    packet_handler::flush_released();
}

float simulated_packet_handler::release()
{
    int count = 0;
    std::uint64_t bits = 0;
    float latency = 0.0f;
    float ip_latency = 0.0f;
    for(std::deque<harq_pkt>::iterator it = pkt_list.begin(); it != pkt_list.end();)
    {
        if(it->t_out <= current_t)
        {
            bits += it->bits;
            latency += current_t - it->current_t;
            ip_latency += current_t - it->ip_t;
            count++;
            for(std::deque<ip_pkt>::const_iterator pkt_it = it->pkts.begin(); pkt_it != it->pkts.end(); ++pkt_it)
            {
                update_pending_packet(*pkt_it, bit_fate::delivered);
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
    delivered_bits_total_ += bits;
    return bits;
}

void simulated_packet_handler::update_pending_packet(const ip_pkt& pkt, bit_fate fate)
{
    // Per object accounting is per fragment: a packet half delivered and half dropped
    // contributes to both counters, and the three add up to what was injected. It does
    // not wait for the packet to be whole again, which is what the verdict below needs.
    if(pkt.tag != 0)
    {
        account_fragment(objects_[pkt.tag], pkt, fate);
        if(object_events_enabled_) account_fragment(object_events_[pkt.tag], pkt, fate);
    }

    const bool dropped = fate != bit_fate::delivered;
    pending_packet_result& state = pending_results[pkt.uid];
    if(state.original_size == 0) state.original_size = pkt.original_size;
    state.accounted_bits += pkt.size;
    state.dropped = state.dropped || dropped;
    state.congestion_signal = state.congestion_signal || pkt.ce_marked;

    if (state.accounted_bits > state.original_size)
        throw std::logic_error(
            "packet fragment accounting exceeds original size");
    if(state.original_size > 0 && state.accounted_bits == state.original_size)
    {
        if(state.dropped) verdict(pkt, final_packet_verdict::DROP);
        else if(state.congestion_signal) verdict(pkt, final_packet_verdict::ACCEPT_CE);
        else verdict(pkt, final_packet_verdict::ACCEPT);
        pending_results.erase(pkt.uid);
    }
}
