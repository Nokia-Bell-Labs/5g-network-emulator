// Packet fates with observers registered. The registry is process-wide and freezes at the first
// packet_handler::step, so every registration is a file-scope object below and runs before main().
// The empty-registry behaviour is checked by pkt_fate_default_test.cpp, which registers nothing and
// runs the same scenarios against the same counter literals.

#include <atomic>
#include <mutex>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <common/direction.h>
#include <netfilter/pkt_capture.h>
#include <pdcp_layer/captured_packet_handler.h>
#include <pdcp_layer/packet_handler.h>
#include <pdcp_layer/pdcp_layer.h>
#include <pdcp_layer/pkt_fate.h>
#include <simulator/configuration_loader.h>
#include <traffic_models/traffic_config.h>

namespace
{
std::mutex events_mutex;
std::vector<pkt_fate_event> events;
std::string call_order;
std::atomic<int> noop_calls{0};

struct registry_probe
{
    bool null_rejected = false;
    bool full_rejected = false;
    std::size_t count_after_registration = 0;
};
registry_probe probe;

// Registered first: only records that it ran, and in which place.
void first_observer(const pkt_fate_event &)
{
    std::lock_guard<std::mutex> lock(events_mutex);
    call_order += 'A';
}

// Registered second and throwing for every odd uid: the observer after it must still run.
void throwing_observer(const pkt_fate_event &event)
{
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        call_order += 'B';
    }
    if (event.uid % 2 == 1)
        throw std::runtime_error("observer failure");
}

void recording_observer(const pkt_fate_event &event)
{
    std::lock_guard<std::mutex> lock(events_mutex);
    call_order += 'C';
    events.push_back(event);
}

void noop_observer(const pkt_fate_event &)
{
    noop_calls++;
}

struct registrar
{
    registrar()
    {
        try
        {
            register_fate_observer(nullptr);
        }
        catch (const std::invalid_argument &)
        {
            probe.null_rejected = true;
        }
        register_fate_observer(&first_observer);
        register_fate_observer(&throwing_observer);
        register_fate_observer(&recording_observer);
        while (fate_observer_count() < max_fate_observers)
            register_fate_observer(&noop_observer);
        probe.count_after_registration = fate_observer_count();
        try
        {
            register_fate_observer(&noop_observer);
        }
        catch (const std::length_error &)
        {
            probe.full_rejected = true;
        }
    }
};
const registrar registration;

// Called by the scenarios at the points where they know how many packets are resolved.
void expect_event_count(std::size_t count)
{
    std::lock_guard<std::mutex> lock(events_mutex);
    assert(events.size() == count);
}

// Counters of one scenario after everything has been resolved, in bits.
struct run_counters
{
    std::uint64_t delivered = 0;
    std::uint64_t expired = 0;
    std::uint64_t queue_dropped = 0;
    std::uint64_t radio_dropped = 0;
    std::int64_t residual = 0;
    int ce_packets = 0;
    std::string verdicts;

    std::string text() const
    {
        std::ostringstream out;
        out << "delivered=" << delivered
            << ",expired=" << expired
            << ",queue_dropped=" << queue_dropped
            << ",radio_dropped=" << radio_dropped
            << ",residual=" << residual
            << ",ce_packets=" << ce_packets;
        if (!verdicts.empty())
            out << ",verdicts=" << verdicts;
        return out.str();
    }
};

const int sim_ue_id = 7;
const int live_ue_id = 9;
const int live_queue_num = 77;

//--------------------------------------------------------------------------------------------------
// Simulated source, through pdcp_layer
//--------------------------------------------------------------------------------------------------
packet_handler_config sim_config(int pkt_size_bits, int max_rtx = 4)
{
    packet_handler_config cfg;
    cfg.ue_type = SIM_UE;
    cfg.tx_dir = TX_DL;
    cfg.queue_num = -1;
    cfg.ue_id = sim_ue_id;
    cfg.init_t = nullptr;
    cfg.traffic_c = traffic_config(
        CONSTANT_TRAFFIC_MODEL, 0.0f, 0.0f, "", "", 0.0f, pkt_size_bits, 0.0f, 0.0f);
    cfg.pdcp_c = pdcp_config(
        max_rtx, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, true, harq_model::legacy_bler);
    cfg.log_traffic = true;
    cfg.log_quality = false;
    return cfg;
}

run_counters counters_of(const pdcp_layer &layer)
{
    run_counters out;
    out.delivered = layer.delivered_bits_total();
    out.expired = layer.expired_bits_total();
    out.queue_dropped = layer.queue_dropped_bits_total();
    out.radio_dropped = layer.radio_dropped_bits_total();
    out.residual = layer.conservation_residual_bits();
    out.ce_packets = layer.ce_packets_total();
    return out;
}

// Three packets of 1000 bits leave in one block and are released two TTIs after they arrived.
run_counters sim_delivered()
{
    pdcp_layer layer(sim_config(1000), 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(3000, 1));
    layer.step(0.0);
    layer.step(0.002);
    assert(layer.handle_pkt(3000.0f, 0, 30.0f, 10.0f, 1) == 3000.0f);
    expect_event_count(0);
    assert(layer.release() == 3000.0f);
    expect_event_count(3);
    return counters_of(layer);
}

// One packet of 2000 bits released in two pieces: its fate is known at the second piece.
run_counters sim_fragments()
{
    pdcp_layer layer(sim_config(2000), 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(2000, 0));
    layer.step(0.0);
    layer.step(0.001);
    assert(layer.handle_pkt(1500.0f, 0, 30.0f, 10.0f, 1) == 1500.0f);
    assert(layer.release() == 1500.0f);
    expect_event_count(0);
    layer.step(0.002);
    assert(layer.handle_pkt(1000.0f, 0, 30.0f, 10.0f, 1) == 500.0f);
    assert(layer.release() == 500.0f);
    expect_event_count(1);
    return counters_of(layer);
}

// Two packets wait in the IP buffer past the delay budget.
run_counters sim_expired()
{
    pdcp_layer layer(sim_config(1000), 1);
    layer.init(0, 1, 1);
    layer.set_pkt_delay_budget(0.001);
    assert(layer.inject_bits(2000, 1));
    layer.step(0.0);
    expect_event_count(0);
    layer.step(0.01);
    expect_event_count(2);
    return counters_of(layer);
}

// The buffer holds 10.48 Gbit, so with packets of 2 Gbit the sixth of six is a tail drop. The
// other five are resolved when the UE is detached.
run_counters sim_tail_drop()
{
    pdcp_layer layer(sim_config(2000000000), 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(12000000000ULL, 2));
    layer.step(0.0);
    expect_event_count(1);
    layer.drop_all();
    expect_event_count(6);
    return counters_of(layer);
}

// A classic queue whose PI controller saturates drops every not-ECT packet at dequeue.
run_counters sim_aqm_drop()
{
    packet_handler_config cfg = sim_config(1000);
    cfg.l4s_c.enabled = true;
    cfg.l4s_c.target_s = 0.0001;
    cfg.l4s_c.rtt_max_s = 0.003;
    pdcp_layer layer(cfg, 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(3000, 3));
    layer.step(0.0);
    layer.step(0.01);
    expect_event_count(0);
    assert(layer.handle_pkt(3000.0f, 0, 30.0f, 10.0f, 1) == 0.0f);
    expect_event_count(3);
    return counters_of(layer);
}

// An ECT(1) packet that waits 10 ms in the L queue is marked and delivered with its mark.
run_counters sim_ce()
{
    packet_handler_config cfg = sim_config(1000);
    cfg.l4s_c.enabled = true;
    pdcp_layer layer(cfg, 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(1000, 4, ECN_ECT1));
    layer.step(0.0);
    layer.step(0.01);
    assert(layer.handle_pkt(1000.0f, 0, 30.0f, 10.0f, 1) == 1000.0f);
    assert(layer.release() == 1000.0f);
    expect_event_count(1);
    return counters_of(layer);
}

// No retransmission is allowed and the only HARQ outcome is a failure.
run_counters sim_radio_drop()
{
    pdcp_layer layer(sim_config(1000, 0), 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(1000, 5));
    layer.step(0.0);
    layer.step(0.003);
    layer.set_harq_scripted_outcomes({0.0});
    assert(layer.handle_pkt(1000.0f, 27, -20.0f, 0.0f, 1) == 0.0f);
    expect_event_count(1);
    return counters_of(layer);
}

// A packet of 1000 bits sends 600 of them before it ages out. The per fragment counters show 600
// delivered and 400 expired; the packet has one fate, the failure.
run_counters sim_mixed()
{
    pdcp_layer layer(sim_config(1000), 1);
    layer.init(0, 1, 1);
    layer.set_pkt_delay_budget(0.001);
    assert(layer.inject_bits(1000, 6));
    layer.step(0.0);
    layer.step(0.0005);
    assert(layer.handle_pkt(600.0f, 0, 30.0f, 10.0f, 1) == 600.0f);
    assert(layer.release() == 600.0f);
    expect_event_count(0);
    layer.step(0.01);
    expect_event_count(1);
    return counters_of(layer);
}

//--------------------------------------------------------------------------------------------------
// Captured source, through captured_packet_handler
//--------------------------------------------------------------------------------------------------
struct fake_capture : public pkt_capture
{
    explicit fake_capture(int queue_num) : pkt_capture(queue_num) {}

    void start() override {}
    void close() override {}

    bool verdict(std::uint32_t pkt_id, packet_capture_action action) override
    {
        verdicts.emplace_back(pkt_id, action);
        return true;
    }

    void capture(std::uint32_t id, std::uint64_t bytes, std::uint8_t ecn)
    {
        captured_packet_info info;
        info.bytes = bytes;
        info.pkt_id = id;
        info.ecn = ecn;
        enqueue_captured_packet(info, std::vector<std::uint8_t>(), ecn);
    }

    std::vector<std::pair<std::uint32_t, packet_capture_action>> verdicts;
};

// A block that carries `bits` of one packet. A partial block is a fragment.
harq_pkt block_of(int id, double t, const ip_pkt &pkt, std::uint64_t bits, bool ce = false)
{
    harq_pkt block(id, 0.0, t, 0, 0, bits, 0.0, 0.0);
    ip_pkt part(pkt);
    part.size = bits;
    part.is_fragment = bits != pkt.original_size;
    if (ce)
    {
        part.ecn = ECN_CE;
        part.ce_marked = true;
    }
    block.pkts.push_back(part);
    return block;
}

void lose(captured_packet_handler &handler, harq_pkt block, bit_fate fate)
{
    handler.record_error(block.bits, fate);
    handler.drop(std::move(block), fate);
}

std::string letter(packet_capture_action action)
{
    if (action == packet_capture_action::ACCEPT)
        return "A";
    if (action == packet_capture_action::ACCEPT_CE)
        return "C";
    return "D";
}

// Seven packets of the live source. 100 is accepted, 101 accepted with a CE mark, 102 dropped from
// the queue, 103 expired, 104 lost on the radio. 105 (2000 bits) arrives in three pieces over two
// TTIs. 106 (1600 bits) arrives in two pieces, the first delivered and the second dropped.
run_counters live_scenario()
{
    pdcp_config config(4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, true);
    std::unique_ptr<fake_capture> fake(new fake_capture(live_queue_num));
    fake_capture *fake_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler handler(std::move(capture), nullptr, config, 1);
    handler.set_fate_identity(live_ue_id, TX_UL, live_queue_num);
    handler.init();

    const std::uint64_t bytes[7] = {125, 125, 125, 125, 125, 250, 200};
    for (std::uint32_t i = 0; i < 7; i++)
        fake_ptr->capture(100 + i, bytes[i], ECN_ECT1);
    handler.step(0.0);
    handler.ingest(TX_UL, 0.0);
    std::vector<ip_pkt> pkts;
    while (handler.has_ingress_pkts())
        pkts.push_back(handler.pop_ingress_pkt());
    assert(pkts.size() == 7);

    handler.push(block_of(0, 0.0, pkts[0], 1000));
    handler.push(block_of(1, 0.0, pkts[1], 1000, true));
    lose(handler, block_of(2, 0.0, pkts[2], 1000), bit_fate::queue_dropped);
    lose(handler, block_of(3, 0.0, pkts[3], 1000), bit_fate::expired);
    lose(handler, block_of(4, 0.0, pkts[4], 1000), bit_fate::radio_dropped);
    handler.push(block_of(5, 0.0, pkts[5], 700));
    handler.push(block_of(6, 0.0, pkts[5], 700));
    handler.push(block_of(7, 0.0, pkts[6], 600));
    lose(handler, block_of(8, 0.0, pkts[6], 1000), bit_fate::queue_dropped);

    // Packets are released in uid order, so the incomplete 105 holds 106 back.
    handler.step(0.01);
    handler.release();
    expect_event_count(5);

    handler.push(block_of(9, 0.02, pkts[5], 600));
    handler.step(0.02);
    handler.release();
    expect_event_count(7);

    run_counters out;
    out.delivered = handler.delivered_bits_total();
    out.expired = handler.expired_bits_total();
    out.queue_dropped = handler.queue_dropped_bits_total();
    out.radio_dropped = handler.radio_dropped_bits_total();
    out.residual = static_cast<std::int64_t>(handler.admitted_bits_total())
        - static_cast<std::int64_t>(
            out.delivered + out.expired + out.queue_dropped + out.radio_dropped
            + handler.pending_ingress_bits() + handler.pending_release_bits());
    out.ce_packets = handler.ce_packets_total();
    for (std::size_t i = 0; i < fake_ptr->verdicts.size(); i++)
        out.verdicts += (i ? "/" : "")
            + std::to_string(fake_ptr->verdicts[i].first) + letter(fake_ptr->verdicts[i].second);
    return out;
}

// What each scenario leaves in the counters, with or without observers.
const char *const expected_sim_delivered =
    "delivered=3000,expired=0,queue_dropped=0,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_sim_fragments =
    "delivered=2000,expired=0,queue_dropped=0,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_sim_expired =
    "delivered=0,expired=2000,queue_dropped=0,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_sim_tail_drop =
    "delivered=0,expired=0,queue_dropped=12000000000,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_sim_aqm_drop =
    "delivered=0,expired=0,queue_dropped=3000,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_sim_ce =
    "delivered=1000,expired=0,queue_dropped=0,radio_dropped=0,residual=0,ce_packets=1";
const char *const expected_sim_radio_drop =
    "delivered=0,expired=0,queue_dropped=0,radio_dropped=1000,residual=0,ce_packets=0";
const char *const expected_sim_mixed =
    "delivered=600,expired=400,queue_dropped=0,radio_dropped=0,residual=0,ce_packets=0";
const char *const expected_live =
    "delivered=4000,expired=1000,queue_dropped=2600,radio_dropped=1000,residual=0,ce_packets=1,"
    "verdicts=100A/101C/102D/103D/104D/105A/106D";

void report(const char *name)
{
    std::cout << "ok   " << name << "\n";
}

void check_text(const char *name, const run_counters &counters, const char *literal)
{
    if (counters.text() != literal)
        std::cerr << "FAIL " << name << ": " << counters.text() << std::endl;
    assert(counters.text() == literal);
}

struct expected_event
{
    std::uint32_t uid;
    bit_fate fate;
    std::uint64_t bits;
    double delay_s;
    bool ce;
};

// Runs one scenario with a clean log. The throwing observer fails once per odd uid, so the failure
// counter must move by exactly that many.
run_counters run_observed(run_counters (*scenario)())
{
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        events.clear();
        call_order.clear();
    }
    const std::uint64_t failures_before = fate_observer_failures();
    const run_counters counters = scenario();
    std::lock_guard<std::mutex> lock(events_mutex);
    std::uint64_t odd = 0;
    for (const pkt_fate_event &event : events)
        odd += event.uid % 2;
    assert(fate_observer_failures() - failures_before == odd);
    return counters;
}

// Exactly one event per expected uid and no other, in the right place of the call order.
void expect_events(
    const std::vector<expected_event> &want,
    int ue_id,
    int tx_dir,
    int queue_num)
{
    std::lock_guard<std::mutex> lock(events_mutex);
    assert(events.size() == want.size());
    std::string order;
    for (std::size_t i = 0; i < events.size(); i++)
        order += "ABC";
    assert(call_order == order);
    for (const expected_event &expected : want)
    {
        int matches = 0;
        for (const pkt_fate_event &event : events)
        {
            if (event.uid != expected.uid)
                continue;
            matches++;
            assert(event.fate == expected.fate);
            assert(event.bits == expected.bits);
            assert(std::fabs(event.delay_s - expected.delay_s) < 1e-6);
            assert(event.ce == expected.ce);
            assert(event.ue_id == ue_id);
            assert(event.tx_dir == tx_dir);
            assert(event.queue_num == queue_num);
        }
        assert(matches == 1);
    }
}

// The bits of the events of each fate add up to the counter of that fate.
void expect_sums_match(const run_counters &counters)
{
    std::lock_guard<std::mutex> lock(events_mutex);
    std::uint64_t delivered = 0, expired = 0, queue_dropped = 0, radio_dropped = 0;
    for (const pkt_fate_event &event : events)
    {
        switch (event.fate)
        {
        case bit_fate::delivered: delivered += event.bits; break;
        case bit_fate::expired: expired += event.bits; break;
        case bit_fate::queue_dropped: queue_dropped += event.bits; break;
        case bit_fate::radio_dropped: radio_dropped += event.bits; break;
        }
    }
    assert(delivered == counters.delivered);
    assert(expired == counters.expired);
    assert(queue_dropped == counters.queue_dropped);
    assert(radio_dropped == counters.radio_dropped);
    assert(counters.residual == 0);
}

void test_registry_limits_before_the_freeze()
{
    assert(probe.null_rejected);
    assert(probe.full_rejected);
    assert(probe.count_after_registration == max_fate_observers);
    assert(fate_observer_count() == max_fate_observers);
    report("a null function and a 17th observer are rejected");
}

void test_simulated_delivered()
{
    const run_counters counters = run_observed(&sim_delivered);
    expect_events(
        {{0, bit_fate::delivered, 1000, 0.002, false},
         {1, bit_fate::delivered, 1000, 0.002, false},
         {2, bit_fate::delivered, 1000, 0.002, false}},
        sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_delivered", counters, expected_sim_delivered);
    report("simulated: delivered packets give one event each");
}

void test_simulated_fragments()
{
    const run_counters counters = run_observed(&sim_fragments);
    expect_events({{0, bit_fate::delivered, 2000, 0.002, false}}, sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_fragments", counters, expected_sim_fragments);
    report("simulated: a packet released in two pieces gives one event");
}

void test_simulated_expired()
{
    const run_counters counters = run_observed(&sim_expired);
    expect_events(
        {{0, bit_fate::expired, 1000, 0.01, false},
         {1, bit_fate::expired, 1000, 0.01, false}},
        sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_expired", counters, expected_sim_expired);
    report("simulated: packets past the delay budget give one expired event each");
}

void test_simulated_tail_drop()
{
    const run_counters counters = run_observed(&sim_tail_drop);
    std::vector<expected_event> want;
    for (std::uint32_t uid = 0; uid < 6; uid++)
        want.push_back({uid, bit_fate::queue_dropped, 2000000000ULL, 0.0, false});
    expect_events(want, sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_tail_drop", counters, expected_sim_tail_drop);
    report("simulated: a tail drop and the detach of the rest give one event per packet");
}

void test_simulated_aqm_drop()
{
    const run_counters counters = run_observed(&sim_aqm_drop);
    expect_events(
        {{0, bit_fate::queue_dropped, 1000, 0.01, false},
         {1, bit_fate::queue_dropped, 1000, 0.01, false},
         {2, bit_fate::queue_dropped, 1000, 0.01, false}},
        sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_aqm_drop", counters, expected_sim_aqm_drop);
    report("simulated: AQM drops at dequeue give one event each");
}

void test_simulated_ce()
{
    const run_counters counters = run_observed(&sim_ce);
    expect_events({{0, bit_fate::delivered, 1000, 0.01, true}}, sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_ce", counters, expected_sim_ce);
    report("simulated: a delivered CE-marked packet reports ce");
}

void test_simulated_radio_drop()
{
    const run_counters counters = run_observed(&sim_radio_drop);
    expect_events({{0, bit_fate::radio_dropped, 1000, 0.003, false}}, sim_ue_id, TX_DL, -1);
    expect_sums_match(counters);
    check_text("sim_radio_drop", counters, expected_sim_radio_drop);
    report("simulated: an exhausted HARQ block gives a radio_dropped event");
}

// The counters work per fragment and the event per packet, so a packet that is part delivered
// and part lost shows in the counters under both fates and in the event under the failure.
void test_simulated_mixed_fates()
{
    const run_counters counters = run_observed(&sim_mixed);
    expect_events({{0, bit_fate::expired, 1000, 0.01, false}}, sim_ue_id, TX_DL, -1);
    assert(counters.delivered == 600);
    assert(counters.expired == 400);
    assert(counters.residual == 0);
    check_text("sim_mixed", counters, expected_sim_mixed);
    report("simulated: a part delivered packet reports its failure once, whole");
}

void test_live_verdicts_and_fragments()
{
    const run_counters counters = run_observed(&live_scenario);
    expect_events(
        {{100, bit_fate::delivered, 1000, 0.01, false},
         {101, bit_fate::delivered, 1000, 0.01, true},
         {102, bit_fate::queue_dropped, 1000, 0.01, false},
         {103, bit_fate::expired, 1000, 0.01, false},
         {104, bit_fate::radio_dropped, 1000, 0.01, false},
         {105, bit_fate::delivered, 2000, 0.02, false},
         {106, bit_fate::queue_dropped, 1600, 0.02, false}},
        live_ue_id, TX_UL, live_queue_num);
    expect_sums_match(counters);
    check_text("live", counters, expected_live);
    report("live: ACCEPT, ACCEPT_CE, DROP and fragments give one event per uid");
}

void test_failures_are_counted()
{
    assert(fate_observer_failures() > 0);
    report("a throwing observer is counted and the later ones still run");
}

void test_registry_is_frozen_after_the_first_step()
{
    bool threw = false;
    try
    {
        register_fate_observer(&noop_observer);
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);
    threw = false;
    try
    {
        register_fate_observer(nullptr);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    assert(threw);
    assert(fate_observer_count() == max_fate_observers);
    assert(noop_calls > 0);
    report("registering after the first step throws");
}
}

int main()
{
    test_registry_limits_before_the_freeze();
    test_simulated_delivered();
    test_simulated_fragments();
    test_simulated_expired();
    test_simulated_tail_drop();
    test_simulated_aqm_drop();
    test_simulated_ce();
    test_simulated_radio_drop();
    test_simulated_mixed_fates();
    test_live_verdicts_and_fragments();
    test_failures_are_counted();
    test_registry_is_frozen_after_the_first_step();
    return 0;
}
