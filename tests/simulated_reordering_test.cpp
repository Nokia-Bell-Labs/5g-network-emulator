// Backhaul FIFO and in-order delivery of the simulated packet handler.
//
// The backhaul is a FIFO pipe in both handlers. The simulated handler hands packets over in
// uid order when pdcp_c.order_packets is set, and as soon as they cross the backhaul when it
// is not. Whatever the setting, every bit is in exactly one place at every TTI, which
// pdcp_layer::conservation_residual_bits() checks.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include <common/direction.h>
#include <netfilter/pkt_capture.h>
#include <pdcp_layer/captured_packet_handler.h>
#include <pdcp_layer/pdcp_layer.h>
#include <pdcp_layer/pkt_fate.h>
#include <pdcp_layer/simulated_packet_handler.h>
#include <simulator/configuration_loader.h>
#include <traffic_models/traffic_config.h>

namespace
{
std::vector<pkt_fate_event> recorded_fates;

void record_fate(const pkt_fate_event &event)
{
    recorded_fates.push_back(event);
}

struct fate_registrar
{
    fate_registrar() { register_fate_observer(&record_fate); }
};
const fate_registrar registrar;

// Uids of the packets that were delivered, in the order their fate was reported.
std::vector<std::uint32_t> delivered_uids()
{
    std::vector<std::uint32_t> uids;
    for (const pkt_fate_event &event : recorded_fates)
        if (event.fate == bit_fate::delivered)
            uids.push_back(event.uid);
    return uids;
}

std::vector<std::uint32_t> uids_with_fate(bit_fate fate)
{
    std::vector<std::uint32_t> uids;
    for (const pkt_fate_event &event : recorded_fates)
        if (event.fate == fate)
            uids.push_back(event.uid);
    return uids;
}

traffic_config silent_traffic()
{
    return traffic_config(
        CONSTANT_TRAFFIC_MODEL, 0.0f, 0.0f, "", "", 0.0f, 1000, 0.0f, 0.0f);
}

//--------------------------------------------------------------------------------------------------
// The backhaul FIFO
//--------------------------------------------------------------------------------------------------
harq_pkt block_of_one_packet(
    int id,
    double now,
    std::uint32_t uid,
    std::uint64_t bits,
    double backhaul_d,
    double backhaul_d_var)
{
    harq_pkt block(id, now, now, 0, 0.0f, bits, backhaul_d, backhaul_d_var);
    block.pkts.emplace_back(
        now, bits, bits, static_cast<int>(uid), backhaul_d, backhaul_d_var);
    return block;
}

// With a spread as wide as the delay itself, an unclamped sample lets a block overtake the
// one pushed 100 us before it. The handler is unordered, so the order of the reports is the
// order in which the blocks left the backhaul.
void test_backhaul_is_fifo_in_the_simulated_handler()
{
    recorded_fates.clear();
    const double backhaul_d = 0.010;
    const double backhaul_d_var = 0.009;
    simulated_packet_handler handler(
        90,
        silent_traffic(),
        pdcp_config(4, 0.0, 0.004, 0.0, 0.001, 0.0, backhaul_d, backhaul_d_var, false),
        5);
    handler.set_fate_identity(90, TX_DL, -1);

    const int blocks = 300;
    for (int i = 0; i < blocks; i++)
    {
        const double now = i * 0.0001;
        handler.step(now);
        handler.push(block_of_one_packet(i, now, i, 1000, backhaul_d, backhaul_d_var));
        handler.release();
    }
    for (int i = 0; i < 400; i++)
    {
        handler.step(blocks * 0.0001 + i * 0.0001);
        handler.release();
    }

    const std::vector<std::uint32_t> uids = delivered_uids();
    assert(uids.size() == static_cast<std::size_t>(blocks));
    for (int i = 0; i < blocks; i++)
        assert(uids[i] == static_cast<std::uint32_t>(i));
    assert(handler.delivered_bits_total() == 1000u * blocks);
    assert(handler.pending_release_bits() == 0);
}

// The same, through the captured handler. It is unordered here so that the verdicts follow
// the departure times and not the capture order.
class fake_capture : public pkt_capture
{
public:
    explicit fake_capture(int queue_num) : pkt_capture(queue_num) {}

    void start() override {}
    void close() override {}
    bool verdict(std::uint32_t pkt_id, packet_capture_action action) override
    {
        if (action != packet_capture_action::DROP)
            released.push_back(pkt_id);
        else
            dropped.push_back(pkt_id);
        return true;
    }

    void capture(std::uint32_t id, std::uint64_t bytes)
    {
        captured_packet_info info;
        info.bytes = bytes;
        info.pkt_id = id;
        info.ecn = ECN_NOT_ECT;
        enqueue_captured_packet(info, std::vector<std::uint8_t>(), ECN_NOT_ECT);
    }

    std::vector<std::uint32_t> released;
    std::vector<std::uint32_t> dropped;
};

void test_backhaul_is_fifo_in_the_captured_handler()
{
    const double backhaul_d = 0.010;
    const double backhaul_d_var = 0.009;
    const int packets = 60;
    std::unique_ptr<fake_capture> fake(new fake_capture(91));
    fake_capture *capture_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler handler(
        std::move(capture),
        nullptr,
        pdcp_config(4, 0.0, 0.004, 0.0, 0.001, 0.0, backhaul_d, backhaul_d_var, false),
        5);
    handler.init();
    for (int i = 0; i < packets; i++)
        capture_ptr->capture(i, 125);
    handler.step(0.0);
    handler.ingest(TX_DL, 0.0);

    int pushed = 0;
    while (handler.has_ingress_pkts())
    {
        const double now = pushed * 0.0001;
        handler.step(now);
        ip_pkt pkt = handler.pop_ingress_pkt();
        harq_pkt block(pushed, pkt.ip_t, now, 0, 0.0f, pkt.size, backhaul_d, backhaul_d_var);
        block.pkts.push_back(pkt);
        handler.push(std::move(block));
        handler.release();
        pushed++;
    }
    for (int i = 0; i < 400; i++)
    {
        handler.step(packets * 0.0001 + i * 0.0001);
        handler.release();
    }

    assert(capture_ptr->released.size() == static_cast<std::size_t>(packets));
    for (int i = 0; i < packets; i++)
        assert(capture_ptr->released[i] == static_cast<std::uint32_t>(i));
}

// A detach empties the backhaul, so a block pushed after the re-attach must not wait for the
// departure time of one that no longer exists.
void test_detach_forgets_the_departure_time_of_the_last_block()
{
    recorded_fates.clear();
    simulated_packet_handler handler(
        92,
        silent_traffic(),
        pdcp_config(4, 0.0, 0.004, 0.0, 0.001, 0.0, 1.0, 0.0, false),
        5);
    handler.set_fate_identity(92, TX_DL, -1);

    handler.step(0.0);
    handler.push(block_of_one_packet(0, 0.0, 0, 1000, 1.0, 0.0));
    handler.step(0.1);
    handler.flush_released();
    assert(handler.queue_dropped_bits_total() == 1000);
    assert(handler.pending_release_bits() == 0);

    handler.step(0.2);
    handler.push(block_of_one_packet(1, 0.2, 1, 1000, 0.001, 0.0));
    handler.step(0.2005);
    assert(handler.release() == 0.0f);
    handler.step(0.2015);
    assert(handler.release() == 1000.0f);
    assert(delivered_uids() == std::vector<std::uint32_t>({1}));

    // The same for the captured handler, which keeps its state in another structure.
    std::unique_ptr<fake_capture> fake(new fake_capture(93));
    fake_capture *capture_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler captured(
        std::move(capture),
        nullptr,
        pdcp_config(4, 0.0, 0.004, 0.0, 0.001, 0.0, 1.0, 0.0, false),
        5);
    captured.init();
    capture_ptr->capture(7, 125);
    captured.step(0.0);
    captured.ingest(TX_DL, 0.0);
    ip_pkt first = captured.pop_ingress_pkt();
    harq_pkt first_block(0, first.ip_t, 0.0, 0, 0.0f, first.size, 1.0, 0.0);
    first_block.pkts.push_back(first);
    captured.push(std::move(first_block));
    captured.step(0.1);
    captured.flush_released();
    assert(capture_ptr->dropped.size() == 1);

    capture_ptr->capture(8, 125);
    captured.step(0.2);
    captured.ingest(TX_DL, 0.2);
    ip_pkt second = captured.pop_ingress_pkt();
    harq_pkt second_block(1, second.ip_t, 0.2, 0, 0.0f, second.size, 0.001, 0.0);
    second_block.pkts.push_back(second);
    captured.push(std::move(second_block));
    captured.step(0.2015);
    captured.release();
    assert(capture_ptr->released.size() == 1);
    assert(capture_ptr->released.front() == 8);
}

//--------------------------------------------------------------------------------------------------
// Delivery order of the simulated handler
//--------------------------------------------------------------------------------------------------
packet_handler_config layer_config(
    int ue_id,
    bool order_packets,
    int max_rtx,
    double backhaul_d = 0.0012)
{
    packet_handler_config cfg;
    cfg.ue_type = SIM_UE;
    cfg.tx_dir = TX_DL;
    cfg.queue_num = -1;
    cfg.ue_id = ue_id;
    cfg.init_t = nullptr;
    cfg.traffic_c = silent_traffic();
    // A failed block is retried 2.5 ms later and a good one leaves the backhaul 1.2 ms
    // after the grant. Neither lands on a TTI boundary, which keeps the schedule exact.
    cfg.pdcp_c = pdcp_config(
        max_rtx, 0.0, 0.0025, 0.0, 0.0, 0.0, backhaul_d, 0.0, order_packets,
        harq_model::legacy_bler);
    cfg.log_traffic = true;
    cfg.log_quality = false;
    return cfg;
}

const float SINR_FAIL = -20.0f;
const float SINR_PASS = 40.0f;

// One TTI of the UE: step, release, then one grant. The accounting has to close after both.
class tti_runner
{
public:
    explicit tti_runner(const packet_handler_config &cfg) : layer(cfg, 1)
    {
        layer.init(0, 1, 1);
        // Every block sent at SINR_FAIL fails; a block at SINR_PASS never draws.
        layer.set_harq_scripted_outcomes(std::vector<double>(16, 0.0));
    }

    void run(float grant_bits = 0.0f, float sinr = SINR_PASS)
    {
        layer.step(tti * 0.001);
        layer.release();
        assert(layer.conservation_residual_bits() == 0);
        if (grant_bits > 0.0f)
            layer.handle_pkt(grant_bits, 27, sinr, 0.0f, 1);
        assert(layer.conservation_residual_bits() == 0);
        tti++;
    }

    pdcp_layer layer;
    int tti = 0;
};

const object_counters &object_of(const pdcp_layer &layer, std::uint32_t tag)
{
    const std::unordered_map<std::uint32_t, object_counters>::const_iterator it =
        layer.objects().find(tag);
    assert(it != layer.objects().end());
    return it->second;
}

void assert_object_closed(const pdcp_layer &layer, std::uint32_t tag)
{
    const object_counters &object = object_of(layer, tag);
    assert(
        object.delivered_bits + object.dropped_bits() + object.expired_bits
        == object.injected_bits);
}

// Three packets of 1000 bits, one tag. The first fails its first transmission, so it is
// retried and crosses the backhaul after the other two.
void send_three_packets_first_one_retried(tti_runner &run)
{
    assert(run.layer.inject_bits(3000, 1));
    run.run(1000.0f, SINR_FAIL);   // t=0: uid 0 fails, its retry is due at 2.5 ms
    run.run(1000.0f);              // t=1: uid 1
    run.run(1000.0f);              // t=2: uid 2
    run.run(1000.0f);              // t=3: the retry of uid 0 succeeds
}

void test_ordered_delivery_holds_a_packet_that_arrives_early()
{
    recorded_fates.clear();
    tti_runner run(layer_config(100, true, 4));
    send_three_packets_first_one_retried(run);

    // uid 1 has crossed the backhaul, uid 0 and uid 2 are still in it. Nothing is handed
    // over, and the bits of uid 1 are held rather than counted anywhere else.
    assert(run.layer.delivered_bits_total() == 0);
    assert(object_of(run.layer, 1).delivered_bits == 0);
    assert(delivered_uids().empty());
    assert(run.layer.pending_bits() == 3000);

    run.run();
    assert(run.layer.delivered_bits_total() == 3000);
    assert(object_of(run.layer, 1).delivered_bits == 3000);
    assert(delivered_uids() == std::vector<std::uint32_t>({0, 1, 2}));
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 1);
}

void test_unordered_delivery_does_not_wait_for_older_packets()
{
    recorded_fates.clear();
    tti_runner run(layer_config(101, false, 4));
    send_three_packets_first_one_retried(run);

    assert(run.layer.delivered_bits_total() == 1000);
    assert(object_of(run.layer, 1).delivered_bits == 1000);
    assert(delivered_uids() == std::vector<std::uint32_t>({1}));

    run.run();
    assert(run.layer.delivered_bits_total() == 3000);
    assert(delivered_uids() == std::vector<std::uint32_t>({1, 2, 0}));
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 1);
}

// An older packet that is dropped after the younger ones have crossed the backhaul releases
// them, instead of holding them forever.
void test_a_dropped_packet_does_not_block_the_ones_behind_it()
{
    recorded_fates.clear();
    tti_runner run(layer_config(102, true, 1));
    assert(run.layer.inject_bits(4000, 2));
    run.run(1000.0f, SINR_FAIL);   // t=0: uid 0 fails, retry due at 2.5 ms
    run.run(1000.0f);              // t=1: uid 1
    run.run(1000.0f);              // t=2: uid 2
    run.run(1000.0f, SINR_FAIL);   // t=3: the retry of uid 0 fails too, no retry is left
    assert(run.layer.radio_dropped_bits_total() == 1000);
    assert(uids_with_fate(bit_fate::radio_dropped) == std::vector<std::uint32_t>({0}));
    // uid 1 is held until the drop is known, and uid 2 is still in the backhaul.
    assert(run.layer.delivered_bits_total() == 0);

    run.run(1000.0f);              // t=4: uids 1 and 2 are handed over, uid 3 is granted
    assert(run.layer.delivered_bits_total() == 2000);
    run.run();
    run.run();
    assert(run.layer.delivered_bits_total() == 3000);
    assert(delivered_uids() == std::vector<std::uint32_t>({1, 2, 3}));
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 2);
    assert(object_of(run.layer, 2).radio_dropped_bits == 1000);
}

// A packet that expires in the IP buffer never reaches HARQ. It is as lost as a radio drop
// and as unable to block.
void test_an_expired_packet_does_not_block_the_ones_behind_it()
{
    recorded_fates.clear();
    tti_runner run(layer_config(103, true, 4));
    run.layer.set_pkt_delay_budget(0.0025);
    assert(run.layer.inject_bits(1000, 3));
    run.run();                    // t=0: uid 0 is queued, nothing is granted
    run.run();
    assert(run.layer.inject_bits(1000, 4));
    run.run();                    // t=2: uid 0 has waited 2 ms, uid 1 is queued
    run.run(1000.0f);             // t=3: uid 0 expires; the grant goes to uid 1
    assert(run.layer.expired_bits_total() == 1000);
    run.run();
    run.run();
    assert(delivered_uids() == std::vector<std::uint32_t>({1}));
    assert(run.layer.delivered_bits_total() == 1000);
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 3);
    assert_object_closed(run.layer, 4);
    assert(object_of(run.layer, 3).expired_bits == 1000);
    assert(object_of(run.layer, 4).delivered_bits == 1000);
}

// A packet cut in two by the grants. The first half arrives and is held, the second is
// dropped: the packet is lost, its first half is reported delivered per fragment once its
// turn comes, and the packet behind it is not blocked.
void test_a_packet_that_loses_one_fragment_is_not_delivered_whole()
{
    recorded_fates.clear();
    tti_runner run(layer_config(104, true, 1));
    assert(run.layer.inject_bits(3000, 4));
    run.run(600.0f);               // t=0: first 600 bits of uid 0
    run.run(1400.0f, SINR_FAIL);   // t=1: last 400 bits of uid 0 and all of uid 1 fail
    run.run(1000.0f);              // t=2: uid 2; the first half of uid 0 has crossed
    // The first half is held with nothing delivered, and it still counts as pending.
    assert(run.layer.delivered_bits_total() == 0);
    assert(run.layer.pending_bits() == 3000);
    run.run();                     // t=3
    run.run(1400.0f, SINR_FAIL);   // t=4: the retry fails and is dropped, two packets lost
    assert(run.layer.radio_dropped_bits_total() == 1400);
    assert(uids_with_fate(bit_fate::radio_dropped) == std::vector<std::uint32_t>({0, 1}));
    assert(run.layer.delivered_bits_total() == 0);

    run.run();                     // t=5: the lost packets pass, uid 2 follows
    assert(run.layer.delivered_bits_total() == 1600);
    assert(delivered_uids() == std::vector<std::uint32_t>({2}));
    assert(run.layer.pending_bits() == 0);
    const object_counters &object = object_of(run.layer, 4);
    assert(object.delivered_bits == 1600);
    assert(object.radio_dropped_bits == 1400);
    assert_object_closed(run.layer, 4);
}

// The packet is lost first, then the fragment that survived arrives after the turn of the
// packet has passed. Nothing is waiting for it, so it is counted on arrival.
void test_a_fragment_that_arrives_after_its_packet_was_lost()
{
    recorded_fates.clear();
    tti_runner run(layer_config(105, true, 1));
    assert(run.layer.inject_bits(2000, 5));
    run.run(600.0f, SINR_FAIL);    // t=0: first 600 bits of uid 0 fail
    run.run();
    run.run();
    run.run(600.0f, SINR_FAIL);    // t=3: the retry fails and is dropped
    assert(run.layer.radio_dropped_bits_total() == 600);
    run.run(1400.0f);              // t=4: the rest of uid 0 and uid 1; uid 0 passes its turn
    assert(run.layer.delivered_bits_total() == 0);
    run.run();                     // t=5: still in the backhaul
    assert(run.layer.delivered_bits_total() == 0);
    run.run();                     // t=6: the late half is counted, uid 1 is whole
    assert(run.layer.delivered_bits_total() == 1400);
    assert(delivered_uids() == std::vector<std::uint32_t>({1}));
    assert(uids_with_fate(bit_fate::radio_dropped) == std::vector<std::uint32_t>({0}));
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 5);
}

// Whatever order the blocks arrive in, the packets come out in uid order and the bits
// always add up. Random grants cut the packets, a bad channel retries and drops blocks, and
// a short budget expires some of what waits.
void test_ordered_delivery_under_random_losses()
{
    recorded_fates.clear();
    tti_runner run(layer_config(106, true, 2));
    run.layer.set_harq_scripted_outcomes(std::vector<double>());
    run.layer.set_pkt_delay_budget(0.03);

    std::mt19937 random(2026);
    const std::uint32_t tags = 5;
    const int traffic_ttis = 1500;
    std::uint64_t injected = 0;
    for (int i = 0; i < traffic_ttis; i++)
    {
        if (random() % 4 == 0)
        {
            const std::uint64_t bits = 500 + random() % 4000;
            assert(run.layer.inject_bits(bits, 1 + random() % tags));
            injected += bits;
        }
        const float grants[] = {0.0f, 300.0f, 700.0f, 1000.0f, 2500.0f};
        const float grant = grants[random() % 5];
        run.run(grant, random() % 100 < 35 ? SINR_FAIL : SINR_PASS);
    }
    for (int i = 0; i < 200; i++)
        run.run(5000.0f);

    assert(run.layer.pending_bits() == 0);
    assert(run.layer.conservation_residual_bits() == 0);
    std::uint64_t closed = 0;
    for (std::uint32_t tag = 1; tag <= tags; tag++)
    {
        if (run.layer.objects().find(tag) == run.layer.objects().end())
            continue;
        assert_object_closed(run.layer, tag);
        closed += object_of(run.layer, tag).injected_bits;
    }
    assert(closed == injected);

    const std::vector<std::uint32_t> uids = delivered_uids();
    assert(!uids.empty());
    assert(std::is_sorted(uids.begin(), uids.end()));
    assert(std::adjacent_find(uids.begin(), uids.end()) == uids.end());
    // The losses are real, or the test would not be testing them.
    assert(run.layer.radio_dropped_bits_total() > 0);
    assert(run.layer.expired_bits_total() > 0);
}

// The same stream, unordered, must still deliver everything it did before and close.
void test_unordered_delivery_under_random_losses()
{
    recorded_fates.clear();
    tti_runner run(layer_config(107, false, 2));
    run.layer.set_harq_scripted_outcomes(std::vector<double>());
    run.layer.set_pkt_delay_budget(0.03);

    std::mt19937 random(2026);
    for (int i = 0; i < 1500; i++)
    {
        if (random() % 4 == 0)
            assert(run.layer.inject_bits(500 + random() % 4000, 1 + random() % 5));
        const float grants[] = {0.0f, 300.0f, 700.0f, 1000.0f, 2500.0f};
        const float grant = grants[random() % 5];
        run.run(grant, random() % 100 < 35 ? SINR_FAIL : SINR_PASS);
    }
    for (int i = 0; i < 200; i++)
        run.run(5000.0f);
    assert(run.layer.pending_bits() == 0);
    assert(run.layer.conservation_residual_bits() == 0);
    const std::vector<std::uint32_t> uids = delivered_uids();
    assert(!std::is_sorted(uids.begin(), uids.end()));
}

// The events a client reads follow the same order as the verdicts do.
void test_object_events_follow_the_delivery_order()
{
    recorded_fates.clear();
    tti_runner run(layer_config(108, true, 4));
    run.layer.enable_object_events();
    assert(run.layer.inject_bits(1000, 1));
    assert(run.layer.inject_bits(1000, 2));
    assert(run.layer.inject_bits(1000, 3));
    run.run(1000.0f, SINR_FAIL);   // uid 0, tag 1: retried
    run.run(1000.0f);              // uid 1, tag 2
    run.run(1000.0f);              // uid 2, tag 3
    run.run(1000.0f);              // the retry of uid 0

    // Tag 2 has crossed the backhaul, but it waits for tag 1: the client sees no event yet.
    assert(run.layer.take_object_events().empty());

    run.run();
    run.run();
    std::unordered_map<std::uint32_t, object_counters> events = run.layer.take_object_events();
    assert(events.size() == 3);
    for (std::uint32_t tag = 1; tag <= 3; tag++)
        assert(events[tag].delivered_bits == 1000);
    assert(delivered_uids() == std::vector<std::uint32_t>({0, 1, 2}));
}

//--------------------------------------------------------------------------------------------------
// Detach
//--------------------------------------------------------------------------------------------------
// Packets held for an older one are lost with everything else when the UE is detached, and
// the UE comes back with an empty queue and a working order.
void test_detach_with_packets_held_for_ordering()
{
    recorded_fates.clear();
    tti_runner run(layer_config(109, true, 4));
    send_three_packets_first_one_retried(run);
    assert(run.layer.pending_bits() == 3000);
    assert(run.layer.delivered_bits_total() == 0);

    run.layer.drop_all();
    assert(run.layer.conservation_residual_bits() == 0);
    assert(run.layer.pending_bits() == 0);
    assert(run.layer.delivered_bits_total() == 0);
    assert(run.layer.queue_dropped_bits_total() == 3000);
    assert(object_of(run.layer, 1).queue_dropped_bits == 3000);
    assert_object_closed(run.layer, 1);
    const std::vector<std::uint32_t> lost = uids_with_fate(bit_fate::queue_dropped);
    assert(std::set<std::uint32_t>(lost.begin(), lost.end()) == std::set<std::uint32_t>({0, 1, 2}));
    assert(lost.size() == 3);

    // Detaching again changes nothing.
    run.layer.drop_all();
    assert(run.layer.conservation_residual_bits() == 0);
    assert(run.layer.queue_dropped_bits_total() == 3000);

    // The UE is back: new packets are ordered among themselves and delivered.
    assert(run.layer.inject_bits(2000, 2));
    run.run(1000.0f);
    run.run(1000.0f);
    run.run();
    run.run();
    assert(run.layer.delivered_bits_total() == 2000);
    assert(delivered_uids() == std::vector<std::uint32_t>({3, 4}));
    assert(run.layer.pending_bits() == 0);
    assert_object_closed(run.layer, 2);
}

// Half of an older packet is held, the other half is still in HARQ and the rest waits in the
// IP buffer when the detach comes.
void test_detach_with_a_held_fragment()
{
    recorded_fates.clear();
    tti_runner run(layer_config(110, true, 4));
    assert(run.layer.inject_bits(3000, 4));
    run.run(600.0f);               // first 600 bits of uid 0
    run.run(1400.0f, SINR_FAIL);   // the rest of uid 0 and uid 1 fail and wait for a retry
    run.run();                     // the first part has crossed the backhaul and is held
    assert(run.layer.pending_bits() == 3000);

    run.layer.drop_all();
    assert(run.layer.conservation_residual_bits() == 0);
    assert(run.layer.pending_bits() == 0);
    assert(run.layer.delivered_bits_total() == 0);
    assert(run.layer.queue_dropped_bits_total() == 3000);
    assert_object_closed(run.layer, 4);
    assert(uids_with_fate(bit_fate::queue_dropped).size() == 3);
}

// Every TTI of a detached UE drops what it ingests. The order state must not grow or leak.
void test_a_detached_ue_keeps_nothing()
{
    recorded_fates.clear();
    tti_runner run(layer_config(111, true, 4));
    for (int i = 0; i < 50; i++)
    {
        assert(run.layer.inject_bits(1500, 6));
        run.layer.step(i * 0.001);
        run.layer.drop_all();
        assert(run.layer.pending_bits() == 0);
        assert(run.layer.conservation_residual_bits() == 0);
    }
    assert(run.layer.queue_dropped_bits_total() == 75000);
    assert(run.layer.delivered_bits_total() == 0);
    assert_object_closed(run.layer, 6);
}

// With DualPI2 enabled and order_packets on, L4S packets overtake classic packets in the
// queue and in HARQ without being held behind older classic uids, while preserving strict
// uid order within the L4S class itself.
void test_l4s_packets_do_not_wait_behind_classic_packets()
{
    recorded_fates.clear();
    packet_handler_config cfg = layer_config(112, true, 4);
    cfg.pdcp_c.rtx_period = 0.005;      // HARQ retry due at t=5 ms
    cfg.l4s_c.enabled = true;
    cfg.l4s_c.classic_guard_s = 0.0f;   // strict L4S priority
    tti_runner run(cfg);
    run.layer.enable_object_events();

    // TTI 0: inject two classic packets (uids 0, 1) and send uid 0 into a HARQ failure.
    assert(run.layer.inject_bits(2000, 10, ECN_NOT_ECT));
    run.run(1000.0f, SINR_FAIL);   // t=0: classic uid 0 fails HARQ (retry at 5.0 ms); uid 1 stays in c_queue

    // TTI 1: inject two L4S ECT(1) packets (uids 2, 3); DualPI2 serves them ahead of classic uid 1.
    assert(run.layer.inject_bits(2000, 11, ECN_ECT1));
    run.run(1000.0f);              // t=1: L4S uid 2 transmitted (leaves backhaul at 2.2 ms)
    run.run(1000.0f);              // t=2: L4S uid 3 transmitted (leaves backhaul at 3.2 ms)

    // TTI 3 (t=3 ms): uid 2 has crossed the backhaul (2.2 <= 3.0) while classic uid 0 is still
    // waiting in HARQ and classic uid 1 is still queued in DualPI2 c_queue!
    run.run(0.0f);
    assert(delivered_uids() == std::vector<std::uint32_t>{2});
    assert(object_of(run.layer, 11).delivered_bits == 1000);
    assert(object_of(run.layer, 10).delivered_bits == 0);

    // TTI 4 (t=4 ms): uid 3 crosses backhaul (3.2 <= 4.0) — full L4S object closes before any classic packet!
    run.run(0.0f);
    assert((delivered_uids() == std::vector<std::uint32_t>{2, 3}));
    assert_object_closed(run.layer, 11);
    assert(object_of(run.layer, 10).delivered_bits == 0);

    // TTI 5..8: classic uid 0 retries at t=5 ms and classic uid 1 transmits at t=6 ms.
    run.run(1000.0f);              // t=5: HARQ retry of classic uid 0 succeeds
    run.run(1000.0f);              // t=6: classic uid 1 transmitted
    run.run();
    run.run();
    assert((delivered_uids() == std::vector<std::uint32_t>{2, 3, 0, 1}));
    assert_object_closed(run.layer, 10);
    assert(run.layer.pending_bits() == 0);
}

void test_l4s_packets_stay_ordered_within_l4s_queue()
{
    recorded_fates.clear();
    packet_handler_config cfg = layer_config(113, true, 4);
    cfg.pdcp_c.rtx_period = 0.004;
    cfg.l4s_c.enabled = true;
    cfg.l4s_c.classic_guard_s = 0.0f;
    tti_runner run(cfg);

    // Queue one classic packet (uid 0) and two L4S packets (uids 1, 2).
    assert(run.layer.inject_bits(1000, 20, ECN_NOT_ECT));
    assert(run.layer.inject_bits(2000, 21, ECN_ECT1));

    // DualPI2 serves L4S uid 1 first; make uid 1 fail HARQ, while L4S uid 2 succeeds next TTI.
    run.run(1000.0f, SINR_FAIL);   // t=0: L4S uid 1 fails HARQ (retry due at 4.0 ms)
    run.run(1000.0f);              // t=1: L4S uid 2 succeeds (backhaul out at 2.2 ms)
    run.run(0.0f);                 // t=2
    run.run(0.0f);                 // t=3: uid 2 crossed backhaul at 2.2 ms, MUST wait for L4S uid 1!
    assert(delivered_uids().empty());
    assert(run.layer.delivered_bits_total() == 0);

    // At t=4 ms, uid 1's retry succeeds (backhaul out at 5.2 ms -> TTI 6); classic uid 0 stays in c_queue.
    run.run(1000.0f);              // t=4: retry of L4S uid 1 succeeds
    run.run(0.0f);                 // t=5
    run.run(0.0f);                 // t=6: uids 1, 2 delivered in order without waiting for classic uid 0!
    assert((delivered_uids() == std::vector<std::uint32_t>{1, 2}));
    assert_object_closed(run.layer, 21);
    assert(object_of(run.layer, 20).delivered_bits == 0);

    run.run(1000.0f);              // t=7: classic uid 0 transmitted
    run.run();
    run.run();
    assert((delivered_uids() == std::vector<std::uint32_t>{1, 2, 0}));
    assert_object_closed(run.layer, 20);
}

void test_l4s_backhaul_fifo_is_independent_of_classic_jitter()
{
    recorded_fates.clear();
    const double backhaul_d = 0.010;
    const double backhaul_d_var = 0.009;
    simulated_packet_handler handler(
        114,
        silent_traffic(),
        pdcp_config(4, 0.0, 0.004, 0.0, 0.001, 0.0, backhaul_d, backhaul_d_var, true),
        42,
        0,
        true);
    handler.set_fate_identity(114, TX_DL, -1);

    // Push 20 classic blocks at t=0 so the classic FIFO clamp reaches ~18-19 ms,
    // then push 1 L4S block with zero jitter at t=0.001 (t_out = 1 + 2 = 3 ms):
    // L4S must depart at ~3 ms without inheriting the classic FIFO tail!
    for (int i = 0; i < 20; i++)
    {
        handler.step(0.0);
        harq_pkt c_blk = block_of_one_packet(i, 0.0, i, 1000, backhaul_d, backhaul_d_var);
        c_blk.pkts.front().ecn = ECN_NOT_ECT;
        c_blk.pkts.front().original_ecn = ECN_NOT_ECT;
        handler.push(std::move(c_blk));
    }
    handler.step(0.001);
    harq_pkt l_blk = block_of_one_packet(20, 0.001, 100, 1000, 0.002, 0.0);
    l_blk.pkts.front().ecn = ECN_ECT1;
    l_blk.pkts.front().original_ecn = ECN_ECT1;
    handler.push(std::move(l_blk));

    handler.step(0.0035);
    handler.release();
    assert((delivered_uids() == std::vector<std::uint32_t>{100}));
}
} // namespace

int main()
{
    test_backhaul_is_fifo_in_the_simulated_handler();
    test_backhaul_is_fifo_in_the_captured_handler();
    test_detach_forgets_the_departure_time_of_the_last_block();
    test_ordered_delivery_holds_a_packet_that_arrives_early();
    test_unordered_delivery_does_not_wait_for_older_packets();
    test_a_dropped_packet_does_not_block_the_ones_behind_it();
    test_an_expired_packet_does_not_block_the_ones_behind_it();
    test_a_packet_that_loses_one_fragment_is_not_delivered_whole();
    test_a_fragment_that_arrives_after_its_packet_was_lost();
    test_ordered_delivery_under_random_losses();
    test_unordered_delivery_under_random_losses();
    test_object_events_follow_the_delivery_order();
    test_detach_with_packets_held_for_ordering();
    test_detach_with_a_held_fragment();
    test_a_detached_ue_keeps_nothing();
    test_l4s_packets_do_not_wait_behind_classic_packets();
    test_l4s_packets_stay_ordered_within_l4s_queue();
    test_l4s_backhaul_fifo_is_independent_of_classic_jitter();
    std::cout << "simulated_reordering_test: ok" << std::endl;
    return 0;
}
