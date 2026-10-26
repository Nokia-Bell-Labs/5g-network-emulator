#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#include <pdcp_layer/captured_packet_handler.h>
#include <utils/terminal_logging.h>

captured_packet_handler::captured_packet_handler(int queue_num, std::chrono::microseconds *_init_t, pdcp_config pdcp_c, unsigned int seed, int verbosity)
    : captured_packet_handler(std::unique_ptr<pkt_capture>(new pkt_capture(queue_num)), _init_t, pdcp_c, seed, verbosity)
{
}

captured_packet_handler::captured_packet_handler(std::unique_ptr<pkt_capture> capture, std::chrono::microseconds *_init_t, pdcp_config pdcp_c, unsigned int seed, int verbosity)
    : packet_handler(pdcp_c, seed, verbosity),
      init_t(_init_t),
      pkt_cptr(std::move(capture))
{
    do_check = pdcp_c.order_packets;
}

captured_packet_handler::~captured_packet_handler()
{
    try
    {
        quit();
    }
    catch (const std::exception &error)
    {
        LOG_ERROR_I("captured_packet_handler::~captured_packet_handler")
            << error.what() << END();
        std::terminate();
    }
}

void captured_packet_handler::init()
{
    if(!pkt_cptr) return;
    pkt_cptr->start();
}

void captured_packet_handler::quit()
{
    if (quitting)
        return;
    quitting = true;
    if (!pkt_cptr)
        return;

    // Stop and join the receive thread first. The NFQUEUE socket remains open
    // so every packet already copied to userspace can still receive a verdict.
    pkt_cptr->stop();
    for (int attempt = 0;
         attempt < 1000 && !pkt_cptr->retry_pending_drops();
         attempt++)
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    if (pkt_cptr->pending_drop_count() != 0)
        throw std::runtime_error(
            "captured packet shutdown could not retry overflow drops");
    ingest(TX_DL, current_t);
    while (has_ingress_pkts())
    {
        ip_pkt pkt = pop_ingress_pkt();
        record_error(pkt.size, bit_fate::queue_dropped);
        drop_ingress_pkt(std::move(pkt));
    }
    force_drop_all();
    for (int attempt = 0;
         attempt < 1000 && !completions.empty();
         attempt++)
    {
        release();
        if (!completions.empty())
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
    }
    if (!completions.empty())
        throw std::runtime_error(
            "captured packet shutdown could not issue every final verdict");
    pkt_cptr->close();
}

float captured_packet_handler::ingest(int tx_dir, float current_t)
{
    (void)tx_dir;
    (void)current_t;
    if(!pkt_cptr) return 0.0f;

    (void)pkt_cptr->retry_pending_drops();
    std::uint64_t bits = 0;
    captured_packet_info info;
    while(pkt_cptr->pop_captured_packet(info))
    {
        if (info.bytes
            > std::numeric_limits<std::uint64_t>::max() / 8)
            throw std::overflow_error(
                "captured packet bit count overflow");
        const std::uint64_t size = info.bytes * 8;
        ip_pkt pkt(get_current_ts(), size, size, prev_uid, info.pkt_id, bh_d, bh_d_var);
        pkt.ecn = info.ecn;
        pkt.original_ecn = info.ecn;
        bits += pkt.size;
        if (completions.size() >= max_completion_states)
        {
            record_admitted_bits(pkt.size);
            if (!pkt_cptr->verdict(
                    pkt.uid,
                    packet_capture_action::DROP))
                throw std::runtime_error(
                    "captured completion queue is full and DROP verdict "
                    "failed");
            record_error(pkt.size, bit_fate::queue_dropped);
            packet_handler::verdict(
                pkt,
                final_packet_verdict::DROP);
            drop_packets++;
            prev_uid = info.pkt_id;
            continue;
        }
        (void)state_for(pkt);
        push_ingress_pkt(std::move(pkt));
        prev_uid = info.pkt_id;
    }
    return bits;
}

void captured_packet_handler::drop_ingress_pkt(ip_pkt pkt)
{
    account_fragment(
        pkt,
        bit_fate::queue_dropped,
        current_t);
}

void captured_packet_handler::push(harq_pkt pkt)
{
    pkt.t_out += std::max(
        0.0f,
        pkt.backhaul_d
            + gauss_dist(gauss_dist_gen) * pkt.backhaul_d_var);
    for(std::deque<ip_pkt>::iterator jt = pkt.pkts.begin(); jt != pkt.pkts.end(); jt++)
    {
        jt->t_out = pkt.t_out;
        jt->ip_t = pkt.ip_t;
        account_fragment(
            *jt,
            bit_fate::delivered,
            pkt.t_out);
    }
}

void captured_packet_handler::drop(harq_pkt pkt, bit_fate fate)
{
    (void)fate;
    for(std::deque<ip_pkt>::iterator jt = pkt.pkts.begin(); jt != pkt.pkts.end(); jt++)
    {
        jt->t_out = pkt.t_out;
        jt->ip_t = pkt.ip_t;
        account_fragment(*jt, fate, current_t);
    }
}

void captured_packet_handler::flush_released()
{
    force_drop_all();
    // Detach is not shutdown: retain any failed verdict state and retry it on
    // subsequent disabled-UE steps rather than aborting the simulation.
    release();
}

float captured_packet_handler::release()
{
    float delivered_bits = 0.0f;
    if (do_check)
    {
        while (!completion_order.empty())
        {
            const std::uint32_t uid = completion_order.front();
            std::unordered_map<std::uint32_t, completion_state>::iterator it =
                completions.find(uid);
            if (it == completions.end())
            {
                completion_order.pop_front();
                continue;
            }
            if (!state_is_complete(it->second)
                || !state_is_ready(it->second)
                || !release_state(uid, delivered_bits))
                break;
            completion_order.pop_front();
        }
    }
    else
    {
        for (std::deque<std::uint32_t>::iterator it =
                 completion_order.begin();
             it != completion_order.end();)
        {
            const std::uint32_t uid = *it;
            std::unordered_map<std::uint32_t, completion_state>::iterator state =
                completions.find(uid);
            if (state == completions.end())
            {
                it = completion_order.erase(it);
                continue;
            }
            if (state_is_complete(state->second)
                && state_is_ready(state->second)
                && release_state(uid, delivered_bits))
                it = completion_order.erase(it);
            else
                ++it;
        }
    }
    tp_mean.add(delivered_bits);
    return delivered_bits;
}

void captured_packet_handler::fill_queue_status(pdcp_queue_status& status, float current_t) const
{
    packet_handler::fill_queue_status(status, current_t);
    if(pkt_cptr)
    {
        status.capture_size = pkt_cptr->captured_queue_size();
        captured_packet_info oldest_capture;
        if(pkt_cptr->peek_oldest_captured(oldest_capture))
        {
            status.capture_oldest_uid = (int)oldest_capture.pkt_id;
            status.capture_oldest_age = -1.0f;
        }

        packet_capture_stats stats = pkt_cptr->stats();
        status.nfqueue_queue_num = stats.queue_num;
        status.nfqueue_total_recv = stats.total_recv;
        status.nfqueue_total_rlsd = stats.total_rlsd;
        status.nfqueue_bytes_recv = stats.bytes_recv;
        status.nfqueue_recv_fails = stats.recv_fails;
        status.nfqueue_rlsd_fails = stats.rlsd_fails;
    }

    status.release_size = static_cast<int>(completions.size());
    status.release_high_water =
        static_cast<int>(completion_high_water);
    if(!completion_order.empty())
    {
        const std::unordered_map<std::uint32_t, completion_state>::const_iterator
            oldest = completions.find(completion_order.front());
        if (oldest != completions.end())
        {
            status.release_oldest_uid = oldest->second.packet.uid;
            status.release_oldest_age =
                current_t - oldest->second.packet.ip_t;
        }
    }
    status.nfqueue_ce_rewrite_packets = ce_rewrite_packets;
    status.nfqueue_drop_packets = drop_packets;
}

float captured_packet_handler::get_current_ts() const
{
    if(init_t == nullptr) return current_t;
    return (float)(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch() - *init_t).count()) * 0.000001f;
}

captured_packet_handler::completion_state &
captured_packet_handler::state_for(const ip_pkt &pkt)
{
    if (finalized_uids.count(pkt.uid) != 0)
        throw std::logic_error(
            "captured packet received a second terminal transition");
    std::unordered_map<std::uint32_t, completion_state>::iterator existing =
        completions.find(pkt.uid);
    if (existing != completions.end())
        return existing->second;
    std::pair<
        std::unordered_map<std::uint32_t, completion_state>::iterator,
        bool> inserted =
        completions.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(pkt.uid),
            std::forward_as_tuple(pkt));
    completion_order.push_back(pkt.uid);
    completion_high_water =
        std::max(completion_high_water, completions.size());
    return inserted.first->second;
}

void captured_packet_handler::account_fragment(
    const ip_pkt &pkt,
    bit_fate fate,
    float ready_time)
{
    completion_state &state = state_for(pkt);
    if (state.original_bits != pkt.original_size)
        throw std::logic_error(
            "captured packet fragment original size changed");
    if (pkt.size > state.original_bits - state.accounted_bits)
        throw std::logic_error(
            "captured packet fragments exceed original size");
    state.accounted_bits += pkt.size;
    if (fate != bit_fate::delivered)
    {
        if (!state.failed)
            state.failure_fate = fate;
        state.failed = true;
        state.failed_bits += pkt.size;
    }
    state.ce_marked = state.ce_marked || pkt.ce_marked;
    state.ready_time = std::max(state.ready_time, ready_time);
}

bool captured_packet_handler::state_is_complete(
    const completion_state &state) const
{
    return state.accounted_bits == state.original_bits;
}

bool captured_packet_handler::state_is_ready(
    const completion_state &state) const
{
    return state.failed || state.ready_time <= current_t;
}

final_packet_verdict captured_packet_handler::state_verdict(
    const completion_state &state) const
{
    if (state.failed)
        return final_packet_verdict::DROP;
    return state.ce_marked
               ? final_packet_verdict::ACCEPT_CE
               : final_packet_verdict::ACCEPT;
}

bool captured_packet_handler::release_state(
    std::uint32_t uid,
    float &delivered_bits)
{
    std::unordered_map<std::uint32_t, completion_state>::iterator it =
        completions.find(uid);
    if (it == completions.end())
        return true;
    completion_state &state = it->second;
    const final_packet_verdict final = state_verdict(state);
    if (!verdict(state.packet, final))
        return false;

    if (final == final_packet_verdict::DROP)
    {
        const std::uint64_t additional_drop_bits =
            state.original_bits - state.failed_bits;
        if (additional_drop_bits > 0)
            record_error(
                additional_drop_bits,
                state.failure_fate);
        drop_packets++;
    }
    else
    {
        delivered_bits += static_cast<float>(state.original_bits);
        delivered_bits_total_ += state.original_bits;
        l_mean.add(current_t - state.packet.current_t);
        ipl_mean.add(current_t - state.packet.ip_t);
        l_mean.step();
        ipl_mean.step();
        if (final == final_packet_verdict::ACCEPT_CE)
            ce_rewrite_packets++;
    }

    finalized_uids.insert(uid);
    finalized_order.push_back(uid);
    if (finalized_order.size() > max_finalized_history)
    {
        finalized_uids.erase(finalized_order.front());
        finalized_order.pop_front();
    }
    completions.erase(it);
    return true;
}

void captured_packet_handler::force_drop_all()
{
    for (std::unordered_map<std::uint32_t, completion_state>::iterator it =
             completions.begin();
         it != completions.end();
         ++it)
    {
        it->second.accounted_bits = it->second.original_bits;
        if (!it->second.failed)
            it->second.failure_fate = bit_fate::queue_dropped;
        it->second.failed = true;
        it->second.ready_time = current_t;
    }
}

std::uint64_t captured_packet_handler::pending_release_bits() const
{
    std::uint64_t bits = 0;
    for (const auto &entry : completions)
    {
        const completion_state &state = entry.second;
        const std::uint64_t pending =
            state.accounted_bits - state.failed_bits;
        if (pending
            > std::numeric_limits<std::uint64_t>::max() - bits)
            throw std::overflow_error(
                "captured completion bit count overflow");
        bits += pending;
    }
    return bits;
}

bool captured_packet_handler::verdict(
    const ip_pkt& pkt,
    final_packet_verdict verdict_value)
{
    if(!pkt_cptr)
        return packet_handler::verdict(pkt, verdict_value);

    packet_capture_action action = packet_capture_action::DROP;
    if (verdict_value == final_packet_verdict::ACCEPT)
        action = packet_capture_action::ACCEPT;
    else if (verdict_value == final_packet_verdict::ACCEPT_CE)
        action = packet_capture_action::ACCEPT_CE;

    if (!pkt_cptr->verdict(pkt.uid, action))
        return false;
    return packet_handler::verdict(pkt, verdict_value);
}
