/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <algorithm>
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

simulated_packet_handler::simulated_packet_handler(
    int ue_id,
    traffic_config traffic_c,
    pdcp_config pdcp_c,
    unsigned int seed,
    int verbosity,
    bool l4s_dual_queue)
    : packet_handler(pdcp_c, seed, verbosity),
      traffic_m(new traffic_model(ue_id, traffic_c)),
      order_packets_(pdcp_c.order_packets)
{
    l4s_dual_queue_ = l4s_dual_queue;
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
    double current_t,
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
        register_for_release(pkt);
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
        register_for_release(pkt);
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

double simulated_packet_handler::ingest(int tx_dir, double current_t)
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
    if(order_packets_)
    {
        // Fragments waiting for an older packet are as undelivered as the ones that were
        // still in the backhaul, so they are lost with the rest.
        drop_held_fragments();
        release_tally unused;
        advance_release_cursor(unused);
    }
}

float simulated_packet_handler::release()
{
    release_tally tally;
    for(std::deque<harq_pkt>::iterator it = pkt_list.begin(); it != pkt_list.end();)
    {
        if(it->t_out <= current_t)
        {
            if(order_packets_)
            {
                for(std::deque<ip_pkt>::const_iterator pkt_it = it->pkts.begin(); pkt_it != it->pkts.end(); ++pkt_it)
                {
                    receive_fragment(*pkt_it, *it, tally);
                }
            }
            else
            {
                tally.bits += it->bits;
                tally.latency += current_t - it->current_t;
                tally.ip_latency += current_t - it->ip_t;
                tally.count++;
                for(std::deque<ip_pkt>::const_iterator pkt_it = it->pkts.begin(); pkt_it != it->pkts.end(); ++pkt_it)
                {
                    update_pending_packet(*pkt_it, bit_fate::delivered);
                }
            }
            it = pkt_list.erase(it);
        }
        else it++;
    }
    if(order_packets_) advance_release_cursor(tally);
    else delivered_bits_total_ += tally.bits;

    if(tally.count > 0)
    {
        l_mean.add(static_cast<float>(tally.latency / tally.count));
        ipl_mean.add(static_cast<float>(tally.ip_latency / tally.count));
        l_mean.step();
        ipl_mean.step();
    }
    tp_mean.add(tally.bits);
    return static_cast<float>(tally.bits);
}

std::uint64_t simulated_packet_handler::pending_release_bits() const
{
    const std::uint64_t in_backhaul = packet_handler::pending_release_bits();
    if (held_bits_ > std::numeric_limits<std::uint64_t>::max() - in_backhaul)
        throw std::overflow_error("pending release bit count overflow");
    return in_backhaul + held_bits_;
}

void simulated_packet_handler::account_object_fragment(const ip_pkt& pkt, bit_fate fate)
{
    // Per object accounting is per fragment: a packet half delivered and half dropped
    // contributes to both counters, and the three add up to what was injected. It does
    // not wait for the packet to be whole again, which is what the verdict needs.
    if(pkt.tag == 0) return;
    account_fragment(objects_[pkt.tag], pkt, fate);
    if(object_events_enabled_) account_fragment(object_events_[pkt.tag], pkt, fate);
}

simulated_packet_handler::pending_packet_result &
simulated_packet_handler::account_pending_fragment(const ip_pkt& pkt, bit_fate fate)
{
    const bool dropped = fate != bit_fate::delivered;
    pending_packet_result& state = pending_results[pkt.uid];
    if(state.original_size == 0) state.original_size = pkt.original_size;
    state.accounted_bits += pkt.size;
    if(dropped && !state.dropped) state.failure_fate = fate;
    state.dropped = state.dropped || dropped;
    state.congestion_signal = state.congestion_signal || pkt.ce_marked;

    if (state.accounted_bits > state.original_size)
        throw std::logic_error(
            "packet fragment accounting exceeds original size");
    return state;
}

void simulated_packet_handler::announce_packet(
    const ip_pkt& pkt,
    const pending_packet_result& state)
{
    if(state.dropped)
    {
        verdict(pkt, final_packet_verdict::DROP);
        resolve_fate(pkt.uid, state.failure_fate, state.original_size, pkt.ip_t, false);
    }
    else if(state.congestion_signal)
    {
        verdict(pkt, final_packet_verdict::ACCEPT_CE);
        resolve_fate(pkt.uid, bit_fate::delivered, state.original_size, pkt.ip_t, true);
    }
    else
    {
        verdict(pkt, final_packet_verdict::ACCEPT);
        resolve_fate(pkt.uid, bit_fate::delivered, state.original_size, pkt.ip_t, false);
    }
}

void simulated_packet_handler::update_pending_packet(const ip_pkt& pkt, bit_fate fate)
{
    account_object_fragment(pkt, fate);
    pending_packet_result& state = account_pending_fragment(pkt, fate);
    if(!is_whole(state)) return;

    if(!order_packets_)
    {
        announce_packet(pkt, state);
        pending_results.erase(pkt.uid);
        return;
    }

    // Ordered delivery only reaches here with a loss: the packet is reported as soon as
    // the rest of it is accounted for, since waiting for the older packets would delay
    // nothing but the report.
    announce_if_settled(pkt, state);
    retire_if_settled(pkt.uid);
}

int simulated_packet_handler::order_queue_index(const ip_pkt& pkt) const
{
    if(!l4s_dual_queue_) return 0;
    const std::uint8_t ecn = pkt.original_ecn;
    return (ecn == ECN_ECT1 || ecn == ECN_CE) ? 1 : 0;
}

void simulated_packet_handler::register_for_release(const ip_pkt& pkt)
{
    if(!order_packets_) return;
    pending_packet_result& state = pending_results[pkt.uid];
    state.original_size = pkt.original_size;
    state.awaiting_turn = true;
    release_order_[order_queue_index(pkt)].push_back(pkt.uid);
}

void simulated_packet_handler::receive_fragment(
    const ip_pkt& pkt,
    const harq_pkt& block,
    release_tally& tally)
{
    pending_packet_result& state = account_pending_fragment(pkt, bit_fate::delivered);
    if(state.awaiting_turn)
    {
        state.held.emplace_back(pkt, block);
        if (pkt.size > std::numeric_limits<std::uint64_t>::max() - held_bits_)
            throw std::overflow_error("held fragment bit count overflow");
        held_bits_ += pkt.size;
    }
    else
    {
        // Nothing is waiting for this fragment: either its packet was never queued for
        // ordering, or the packet is lost and its turn has already passed.
        deliver_fragment(pkt, block.current_t, block.ip_t, tally);
    }

    announce_if_settled(pkt, state);
    retire_if_settled(pkt.uid);
}

void simulated_packet_handler::advance_release_cursor(release_tally& tally)
{
    advance_queue_cursor(release_order_[0], tally);
    advance_queue_cursor(release_order_[1], tally);
}

void simulated_packet_handler::advance_queue_cursor(
    std::deque<std::uint32_t>& queue,
    release_tally& tally)
{
    while(!queue.empty())
    {
        const std::uint32_t uid = queue.front();
        std::unordered_map<std::uint32_t, pending_packet_result>::iterator it =
            pending_results.find(uid);
        if(it == pending_results.end())
            throw std::logic_error("packet queued for release has no state");

        pending_packet_result& state = it->second;
        if(state.dropped)
        {
            // The fragments that did arrive are handed over in their turn. The packet is
            // lost whether they are or not, and the ones that are still on their way are
            // delivered on arrival.
            deliver_held_fragments(state, tally);
            state.awaiting_turn = false;
            retire_if_settled(uid);
        }
        else if(is_whole(state))
        {
            const ip_pkt first_fragment = state.held.front().pkt;
            deliver_held_fragments(state, tally);
            state.awaiting_turn = false;
            announce_if_settled(first_fragment, state);
            pending_results.erase(it);
        }
        else return;
        queue.pop_front();
    }
}

void simulated_packet_handler::deliver_held_fragments(
    pending_packet_result& state,
    release_tally& tally)
{
    for(std::vector<held_fragment>::const_iterator it = state.held.begin(); it != state.held.end(); ++it)
    {
        if (it->pkt.size > held_bits_)
            throw std::logic_error("held fragment bit accounting underflow");
        held_bits_ -= it->pkt.size;
        deliver_fragment(it->pkt, it->block_current_t, it->block_ip_t, tally);
    }
    state.held.clear();
}

void simulated_packet_handler::deliver_fragment(
    const ip_pkt& pkt,
    double block_current_t,
    double block_ip_t,
    release_tally& tally)
{
    account_object_fragment(pkt, bit_fate::delivered);
    delivered_bits_total_ += pkt.size;
    tally.bits += pkt.size;
    tally.latency += current_t - block_current_t;
    tally.ip_latency += current_t - block_ip_t;
    tally.count++;
}

void simulated_packet_handler::drop_held_fragments()
{
    std::vector<std::uint32_t> uids;
    for(int q = 0; q < 2; q++)
    {
        for(std::deque<std::uint32_t>::const_iterator it = release_order_[q].begin();
            it != release_order_[q].end();
            ++it)
        {
            if(!pending_results[*it].held.empty()) uids.push_back(*it);
        }
    }
    std::sort(uids.begin(), uids.end());

    // Sorted by uid so fate reports remain deterministic across both queues.
    for(std::uint32_t uid : uids)
    {
        pending_packet_result& state = pending_results[uid];
        for(std::vector<held_fragment>::const_iterator it = state.held.begin(); it != state.held.end(); ++it)
        {
            if (it->pkt.size > held_bits_)
                throw std::logic_error("held fragment bit accounting underflow");
            held_bits_ -= it->pkt.size;
            account_object_fragment(it->pkt, bit_fate::queue_dropped);
            record_error(it->pkt.size, bit_fate::queue_dropped);
        }
        const ip_pkt first_fragment = state.held.front().pkt;
        state.held.clear();
        if(!state.dropped)
        {
            state.dropped = true;
            state.failure_fate = bit_fate::queue_dropped;
        }
        announce_if_settled(first_fragment, state);
    }
}

void simulated_packet_handler::announce_if_settled(
    const ip_pkt& pkt,
    pending_packet_result& state)
{
    if(state.announced || !is_whole(state)) return;
    // A packet that arrived whole waits for its turn. A lost one has nothing to wait for.
    if(!state.dropped && state.awaiting_turn) return;
    announce_packet(pkt, state);
    state.announced = true;
}

bool simulated_packet_handler::is_whole(const pending_packet_result& state)
{
    return state.original_size > 0 && state.accounted_bits == state.original_size;
}

void simulated_packet_handler::retire_if_settled(std::uint32_t uid)
{
    std::unordered_map<std::uint32_t, pending_packet_result>::iterator it = pending_results.find(uid);
    if(it == pending_results.end()) return;
    const pending_packet_result& state = it->second;
    if(!state.awaiting_turn && state.announced && is_whole(state))
        pending_results.erase(it);
}
