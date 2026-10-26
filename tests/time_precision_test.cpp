// Simulation timestamps keep sub-microsecond precision after 12 hours of simulated time.
//
// Simulated time used to be held in float. A float has 24 bits of mantissa, so its spacing
// is about 1 ms at 8192 s and 3.9 ms at 43200 s (12 h). Anything that depended on the
// difference between two timestamps at that age (a packet delay, a sojourn time in the
// queue, the age compared against the delay budget) was rounded to a multiple of that
// spacing: a 250 us delay read as 0 or 3.9 ms, and a packet 1 us inside its budget was
// treated the same as one 1 us outside it. A short run never shows it, a long one does.
//
// Timestamps are now double end to end (spacing about 7 ps at 43200 s). Every case below
// runs at 43200 s plus a fractional part that float cannot represent, and checks a
// decision or a measurement that is exact to better than 1 us. Each case also computes the
// same quantity with the timestamps narrowed to float and asserts that the narrowed result
// does NOT meet the same bound, so the test documents why the case matters and would stop
// being meaningful if the narrowing were ever brought back.
//
// What the API offers, per case:
//   - Packet delay: the latency means are float reporting values, but they hold a small
//     difference computed in double and only then narrowed, so a 250 us mean is still
//     accurate to about 1e-11 s. They are asserted against 1 us.
//   - DualPI2: the head timestamp is double, and the L4S marking probability follows the
//     sojourn time through the laqm ramp, so the ramp is asserted (p_l is float, but its
//     input is the double sojourn time) together with the marking decision at the
//     threshold.
//   - Expiry: pdcp_layer::is_expired and oldest_allowed_ip_t are not reachable from outside the class, so the decision
//     is observed through pdcp_layer::step (the ip buffer cleanup) and the expired bits
//     counter, and through harq_handler::pop_pkt_older_than, which takes the same
//     oldest allowed timestamp as a double.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <utility>

#include <common/direction.h>
#include <mac_layer/harq_handler.h>
#include <pdcp_layer/dualpi2_queue.h>
#include <pdcp_layer/pdcp_config.h>
#include <pdcp_layer/pdcp_layer.h>
#include <pdcp_layer/pdcp_queue_status.h>
#include <pdcp_layer/simulated_packet_handler.h>
#include <pkts/pkts.h>
#include <simulator/configuration_loader.h>
#include <traffic_models/traffic_config.h>

namespace
{
// 12 h of simulated time, with a fractional part that has to survive. The float spacing
// at this magnitude is 2^-8 s = 3.90625 ms.
constexpr double T0 = 43200.000123;
constexpr double ONE_US = 1e-6;
constexpr double FLOAT_SPACING_AT_12H = 1.0 / 256.0;

bool within(double value, double expected, double bound)
{
    return std::fabs(value - expected) < bound;
}

// The difference of two timestamps as it would come out if both were stored in float.
double float_delta(double later, double earlier)
{
    const float a = static_cast<float>(later);
    const float b = static_cast<float>(earlier);
    return static_cast<double>(a - b);
}

// The expiry decision `ip_t < now - budget` with every term narrowed to float.
bool float_expired(double ip_t, double now, double budget)
{
    const float f_ip_t = static_cast<float>(ip_t);
    const float f_now = static_cast<float>(now);
    const float f_budget = static_cast<float>(budget);
    return f_ip_t < f_now - f_budget;
}

pdcp_config make_pdcp_config(double backhaul_d)
{
    return pdcp_config(4, 0.0, 0.0, 0.0, 0.0, 0.0, backhaul_d, 0.0, true);
}

// A traffic source that generates nothing by itself: every packet comes from inject_bits,
// so the packet timestamp is exactly the time of the step that ingests it.
traffic_config make_silent_traffic()
{
    return traffic_config(CONSTANT_TRAFFIC_MODEL, 0.0f, 0.0f, "", "", 0.0f, 1000, 0.0f, 0.0f);
}

packet_handler_config make_sim_config(pdcp_config pdcp_c, traffic_config traffic)
{
    packet_handler_config cfg;
    cfg.ue_type = SIM_UE;
    cfg.tx_dir = TX_DL;
    cfg.queue_num = -1;
    cfg.ue_id = 1;
    cfg.init_t = nullptr;
    cfg.traffic_c = traffic;
    cfg.pdcp_c = pdcp_c;
    cfg.log_traffic = true;
    cfg.log_quality = false;
    return cfg;
}

// A DualPI2 queue whose L4S marking depends on the sojourn time alone: the coupling factor
// is zero and the PI target is far above any delay used here, so p_cl stays at 0 and the
// only input to the marking probability is the laqm ramp.
dualpi2_config make_laqm_only_config(double range_s)
{
    dualpi2_config cfg;
    cfg.enabled = true;
    cfg.k = 0.0f;
    cfg.target_s = 0.5;
    cfg.min_th_s = 0.0008;
    cfg.range_s = range_s;
    cfg.p_lmax = 1.0f;
    cfg.classic_guard_s = 1.0;
    return cfg;
}

// Enqueues one L4S-capable packet stamped `t_enq`, steps the queue to `t_deq` and takes the
// packet out, which is the moment the marking decision is made.
ip_pkt dequeue_after(dualpi2_queue &queue, double t_enq, double t_deq)
{
    ip_pkt pkt(t_enq, 1000, 1000, 1, 0.0, 0.0);
    pkt.ecn = ECN_ECT1;
    pkt.original_ecn = ECN_ECT1;
    queue.step(t_enq);
    assert(queue.enqueue(pkt));
    queue.step(t_deq);
    ip_pkt out(0.0, 0, 0, 0, 0.0, 0.0);
    assert(queue.pop_next(out));
    return out;
}

// What the narrowing to float does to the quantities used below.
void test_float_cannot_resolve_the_case()
{
    const double t_a = 43200.000000;
    const double t_b = 43200.000250;
    const double expected = 0.000250;

    // Double: the difference is the delay, to far better than 1 us.
    assert(within(t_b - t_a, expected, ONE_US));
    assert(within(t_b - t_a, expected, 1e-9));

    // Float: both timestamps round to the same representable value, so a 250 us delay
    // reads as zero and the error is the whole delay.
    assert(static_cast<float>(t_b) == static_cast<float>(t_a));
    assert(!within(float_delta(t_b, t_a), expected, ONE_US));

    // The float spacing at 12 h is what makes this unavoidable.
    const float f = static_cast<float>(t_a);
    const double spacing = static_cast<double>(std::nextafter(f, 1.0e9f)) - static_cast<double>(f);
    assert(within(spacing, FLOAT_SPACING_AT_12H, 1e-12));
    assert(spacing > 1000.0 * ONE_US);

    // The fractional part used by the other cases does not survive either.
    assert(static_cast<float>(T0) == 43200.0f);
    assert(T0 - 43200.0 > 1e-4);
}

// Packet delay, at the release stage: a block granted at T0 that reaches the end of a
// 250 us backhaul delay is not released 1 us early, is released on time, and its latency
// is reported to better than 1 us.
void test_packet_delay_is_exact_at_12h()
{
    const double bh_d = 0.000250;
    const double ip_age = 0.000100;   // time spent in the ip buffer before the grant

    simulated_packet_handler handler(1, make_silent_traffic(), make_pdcp_config(0.0), 1);
    handler.step(T0);

    harq_pkt pkt(0, T0 - ip_age, T0, 0, 0.0f, 1000, bh_d, 0.0);
    ip_pkt ip(T0 - ip_age, 1000, 1000, 10, bh_d, 0.0);
    pkt.pkts.push_back(ip);
    handler.push(std::move(pkt));

    // 1 us before the end of the delay: still in the release buffer.
    handler.step(T0 + bh_d - ONE_US);
    assert(handler.release() == 0.0f);
    assert(handler.delivered_bits_total() == 0);

    // At the end of the delay it goes out, and the latency it reports is the delay.
    handler.step(T0 + bh_d);
    assert(handler.release() == 1000.0f);
    assert(handler.delivered_bits_total() == 1000);

    // latency: grant to release; ip latency: ingestion to release. Both are float means of
    // a difference computed in double, so they keep the sub-ms part.
    const double latency = handler.get_latency(true);
    const double ip_latency = handler.get_ip_latency(true);
    assert(within(latency, bh_d, ONE_US));
    assert(within(ip_latency, bh_d + ip_age, ONE_US));

    // With float timestamps the same two quantities are off by far more than 1 us.
    assert(!within(float_delta(T0 + bh_d, T0), bh_d, ONE_US));
    assert(!within(float_delta(T0 + bh_d, T0 - ip_age), bh_d + ip_age, ONE_US));
}

// Packet delay through the whole layer: injected at T0, granted 100 us later, released
// after a 250 us backhaul delay. The queue status age is a float reporting field, but it
// holds a difference computed in double.
void test_packet_delay_through_pdcp_layer_at_12h()
{
    const double bh_d = 0.000250;
    const double wait = 0.000100;

    pdcp_layer layer(make_sim_config(make_pdcp_config(bh_d), make_silent_traffic()), 1);
    layer.init(0, 1, 1);
    layer.step(T0);
    assert(layer.inject_bits(1000, 7));
    layer.step(T0);                       // ingests the packet with ip_t == T0
    assert(layer.has_pkts());
    assert(within(layer.get_oldest_timestamp(), T0, 1e-9));

    // The age of the oldest packet at T0 + 400 us (still waiting, no grant yet).
    layer.step(T0 + 0.000400);
    pdcp_queue_status status = layer.get_queue_status();
    assert(status.ip_buffer_size == 1);
    assert(within(status.ip_oldest_age, 0.000400, ONE_US));

    // Grant at T0 + 100 us on a fresh layer, so that the timeline is exactly the one above.
    pdcp_layer timed(make_sim_config(make_pdcp_config(bh_d), make_silent_traffic()), 1);
    timed.init(0, 1, 1);
    timed.step(T0);
    assert(timed.inject_bits(1000, 7));
    timed.step(T0);
    timed.step(T0 + wait);
    assert(timed.handle_pkt(1000.0f, 0, 30.0f, 10.0f, 1) == 1000.0f);

    // 1 us before the backhaul delay is over: not released.
    timed.step(T0 + wait + bh_d - ONE_US);
    assert(timed.release() == 0.0f);
    assert(timed.delivered_bits_total() == 0);

    // On time: released. latency counts from the grant, ip latency from the injection.
    timed.step(T0 + wait + bh_d);
    assert(timed.release() == 1000.0f);
    assert(timed.delivered_bits_total() == 1000);
    assert(within(timed.get_latency(true), bh_d, ONE_US));
    assert(within(timed.get_ip_latency(true), wait + bh_d, ONE_US));

    // The same timeline in float cannot report either.
    assert(!within(float_delta(T0 + 0.000400, T0), 0.000400, ONE_US));
    assert(!within(float_delta(T0 + wait + bh_d, T0 + wait), bh_d, ONE_US));
}

// DualPI2 sojourn time. The head timestamp is a double and is returned as stored; the
// sojourn time seen by the marking decision is current_t minus that timestamp.
void test_dualpi2_sojourn_is_exact_at_12h()
{
    dualpi2_queue queue(make_laqm_only_config(0.0004));
    queue.step(T0);

    ip_pkt pkt(T0, 1000, 1000, 1, 0.0, 0.0);
    pkt.ecn = ECN_ECT1;
    assert(queue.enqueue(pkt));
    assert(queue.oldest_timestamp(-1.0) == T0);   // stored and returned as a double

    const double now = T0 + 0.000400;
    queue.step(now);
    const double sojourn = now - queue.oldest_timestamp(-1.0);
    assert(within(sojourn, 0.000400, 1e-9));
    assert(within(sojourn, 0.000400, ONE_US));
    assert(!within(float_delta(now, T0), 0.000400, ONE_US));

    // Below the threshold (800 us) the L4S probability stays at 0.
    ip_pkt out(0.0, 0, 0, 0, 0.0, 0.0);
    assert(queue.pop_next(out));
    assert(queue.get_stats().p_l == 0.0f);
    assert(out.ecn == ECN_ECT1 && !out.ce_marked);
}

// The laqm ramp follows the sojourn time linearly between min_th_s (800 us) and
// min_th_s + range_s (1200 us). The probability is a float, but the ramp position comes
// from the double sojourn time: 100 us above the threshold is a probability of 0.25, and 1
// us of sojourn time is worth 0.0025, so the bound below is well under 1 us of sojourn.
void test_dualpi2_laqm_ramp_is_exact_at_12h()
{
    const double min_th = 0.0008;
    const double range = 0.0004;
    const double offsets_us[] = {50.0, 100.0, 200.0, 300.0, 399.0};

    for(double off_us : offsets_us)
    {
        dualpi2_queue queue(make_laqm_only_config(range));
        const double sojourn = min_th + off_us * 1e-6;
        const ip_pkt out = dequeue_after(queue, T0, T0 + sojourn);
        (void)out;
        const double expected_p = off_us * 1e-6 / range;
        const double p_l = queue.get_stats().p_l;
        // 0.1 us of sojourn time is 0.00025 of probability.
        assert(within(p_l, expected_p, 0.00025));
        assert(p_l > 0.0 && p_l < 1.0);
    }

    // Float timestamps put the sojourn on a multiple of 3.9 ms: it is either 0 or past the
    // top of the ramp, never on it.
    const double sojourn = min_th + 100e-6;
    const double float_sojourn = float_delta(T0 + sojourn, T0);
    assert(float_sojourn == 0.0 || float_sojourn >= FLOAT_SPACING_AT_12H);
    assert(!within(float_sojourn, sojourn, ONE_US));
}

// The marking decision at the threshold, with the ramp reduced to a step (range 0 means
// the probability jumps to p_lmax once the sojourn time is above min_th_s). A packet that
// waited 799.5 us is not marked, one that waited 800.5 us is.
void test_dualpi2_marking_threshold_is_exact_at_12h()
{
    const double min_th = 0.0008;

    // Just below the threshold: no mark, nothing counted.
    {
        dualpi2_queue queue(make_laqm_only_config(0.0));
        const ip_pkt out = dequeue_after(queue, T0, T0 + min_th - 0.5e-6);
        assert(out.ecn == ECN_ECT1);
        assert(!out.ce_marked);
        assert(queue.get_stats().ce_packets == 0);
        assert(queue.get_stats().p_l == 0.0f);
    }

    // Just above the threshold: marked CE and counted.
    {
        dualpi2_queue queue(make_laqm_only_config(0.0));
        const ip_pkt out = dequeue_after(queue, T0, T0 + min_th + 0.5e-6);
        assert(out.ecn == ECN_CE);
        assert(out.ce_marked);
        assert(queue.get_stats().ce_packets == 1);
        assert(queue.get_stats().ce_bits == 1000);
        assert(queue.get_stats().p_l == 1.0f);
    }

    // With float timestamps the two sojourn times land on the same side: both measure the
    // same multiple of the float spacing, so they could not be told apart.
    const double below = float_delta(T0 + min_th - 0.5e-6, T0);
    const double above = float_delta(T0 + min_th + 0.5e-6, T0);
    assert(below == above);
    assert(!within(below, min_th - 0.5e-6, ONE_US));
}

// The expiry decision at the delay budget, seen through pdcp_layer::step: a packet ingested
// at T0 with a 300 ms budget is kept when the layer is stepped to T0 + 300 ms - 1 us and
// expired when it is stepped to T0 + 300 ms + 1 us.
void test_expiry_decision_is_exact_at_12h()
{
    const double budget = 0.300;

    // 1 us younger than the budget: kept.
    {
        pdcp_layer layer(make_sim_config(make_pdcp_config(0.0), make_silent_traffic()), 1);
        layer.init(0, 1, 1);
        layer.set_pkt_delay_budget(budget);
        layer.step(T0);
        assert(layer.inject_bits(1000, 7));
        layer.step(T0);
        assert(layer.get_queue_status().ip_buffer_size == 1);

        layer.step(T0 + budget - ONE_US);
        assert(layer.get_queue_status().ip_buffer_size == 1);
        assert(layer.expired_bits_total() == 0);
        assert(layer.has_pkts());
        assert(layer.conservation_residual_bits() == 0);
    }

    // 1 us older than the budget: expired and accounted as such.
    {
        pdcp_layer layer(make_sim_config(make_pdcp_config(0.0), make_silent_traffic()), 1);
        layer.init(0, 1, 1);
        layer.set_pkt_delay_budget(budget);
        layer.step(T0);
        assert(layer.inject_bits(1000, 7));
        layer.step(T0);
        assert(layer.get_queue_status().ip_buffer_size == 1);

        layer.step(T0 + budget + ONE_US);
        assert(layer.get_queue_status().ip_buffer_size == 0);
        assert(layer.expired_bits_total() == 1000);
        assert(!layer.has_pkts());
        assert(layer.conservation_residual_bits() == 0);
    }

    // With float timestamps and budget, the two decisions are the same one, so a packet
    // 1 us inside the budget and one 1 us outside it cannot be told apart.
    const bool float_young = float_expired(T0, T0 + budget - ONE_US, budget);
    const bool float_old = float_expired(T0, T0 + budget + ONE_US, budget);
    assert(float_young == float_old);
}

// The same threshold on a packet waiting for retransmission: harq_handler takes the
// oldest allowed ip timestamp (now minus budget) as a double, the quantity that
// pdcp_layer::oldest_allowed_ip_t returns.
void test_harq_expiry_threshold_is_exact_at_12h()
{
    const double budget = 0.300;
    const double now = T0 + budget;

    harq_handler harq(4, 0.0, 0.0, 0.0, 0.0, 0.0, 0);
    harq.init(0, 1);
    harq.step(now);

    // Stamped 1 us after the oldest allowed time: it is within the budget.
    harq_pkt young(1, T0 + ONE_US, now, 0, 0, 1000, 0.0, 0.0);
    // Stamped 1 us before the oldest allowed time: it is past the budget.
    harq_pkt old(2, T0 - ONE_US, now, 0, 0, 1200, 0.0, 0.0);
    assert(harq.enqueue_retry(young, 0.0f, 1));
    assert(harq.enqueue_retry(old, 0.0f, 1));

    const double oldest_allowed_ip_t = now - budget;
    assert(within(oldest_allowed_ip_t, T0, 1e-9));

    harq_pkt expired;
    assert(harq.pop_pkt_older_than(oldest_allowed_ip_t, expired));
    assert(expired.id == 2);
    assert(harq.size() == 1);
    harq_pkt none;
    assert(!harq.pop_pkt_older_than(oldest_allowed_ip_t, none));   // the young one stays

    // In float the two stamps and the threshold are the same number.
    const float f_threshold = static_cast<float>(now) - static_cast<float>(budget);
    const float f_young = static_cast<float>(T0 + ONE_US);
    const float f_old = static_cast<float>(T0 - ONE_US);
    assert(f_young == f_old);
    assert((f_young < f_threshold) == (f_old < f_threshold));
}
}

int main()
{
    test_float_cannot_resolve_the_case();
    std::puts("ok   float_cannot_resolve_the_case");
    test_packet_delay_is_exact_at_12h();
    std::puts("ok   packet_delay_is_exact_at_12h");
    test_packet_delay_through_pdcp_layer_at_12h();
    std::puts("ok   packet_delay_through_pdcp_layer_at_12h");
    test_dualpi2_sojourn_is_exact_at_12h();
    std::puts("ok   dualpi2_sojourn_is_exact_at_12h");
    test_dualpi2_laqm_ramp_is_exact_at_12h();
    std::puts("ok   dualpi2_laqm_ramp_is_exact_at_12h");
    test_dualpi2_marking_threshold_is_exact_at_12h();
    std::puts("ok   dualpi2_marking_threshold_is_exact_at_12h");
    test_expiry_decision_is_exact_at_12h();
    std::puts("ok   expiry_decision_is_exact_at_12h");
    test_harq_expiry_threshold_is_exact_at_12h();
    std::puts("ok   harq_expiry_threshold_is_exact_at_12h");
    return 0;
}
