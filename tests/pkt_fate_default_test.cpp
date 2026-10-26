// Packet fates with nothing registered. The registry is process-wide and freezes at the first
// packet_handler::step, so the default behaviour is checked in a binary of its own that registers
// nothing. pkt_fate_test.cpp runs the same scenarios with observers registered and compares the
// counters against the same literals.

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
// Called by the scenarios where the observing binary counts events; nothing to count here.
void expect_event_count(std::size_t) {}

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

void test_nothing_is_registered()
{
    assert(fate_observer_count() == 0);
    assert(fate_observer_failures() == 0);
    report("nothing is registered by default");
}

void test_scenarios_match_literals()
{
    check_text("sim_delivered", sim_delivered(), expected_sim_delivered);
    check_text("sim_fragments", sim_fragments(), expected_sim_fragments);
    check_text("sim_expired", sim_expired(), expected_sim_expired);
    check_text("sim_tail_drop", sim_tail_drop(), expected_sim_tail_drop);
    check_text("sim_aqm_drop", sim_aqm_drop(), expected_sim_aqm_drop);
    check_text("sim_ce", sim_ce(), expected_sim_ce);
    check_text("sim_radio_drop", sim_radio_drop(), expected_sim_radio_drop);
    check_text("sim_mixed", sim_mixed(), expected_sim_mixed);
    check_text("live", live_scenario(), expected_live);
    assert(fate_observer_failures() == 0);
    report("counters with nothing registered match the literals");
}

void test_registry_is_frozen_after_the_first_step()
{
    // The scenarios above stepped handlers, so the empty registry is frozen as well.
    bool threw = false;
    try
    {
        register_fate_observer([](const pkt_fate_event &) {});
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);
    assert(fate_observer_count() == 0);
    report("the empty registry freezes at the first step");
}
}

int main()
{
    test_nothing_is_registered();
    test_scenarios_match_literals();
    test_registry_is_frozen_after_the_first_step();
    return 0;
}
