// Test of the observability example. It is built and run by `make test-examples`, not by
// `make test`. The example's registrations are file-scope objects in the object files that this
// binary links, so every registry already has its entries when main() starts.
//
// The tests cover, in this order: what was registered; the annotator on hand-built packets; the
// whole live path (capture, annotation, ingest, fate, counters) through a captured_packet_handler
// with a fake capture; the instrumented metric against an independent copy of the PF expression,
// single-threaded and from several threads; the runtime parameters; the controller's reports; and
// a simulated run that must schedule exactly like the built-in PF.

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common/direction.h>
#include <mac_layer/metric_handler.h>
#include <netfilter/capture_annotation.h>
#include <netfilter/pkt_capture.h>
#include <pdcp_layer/captured_packet_handler.h>
#include <pdcp_layer/packet_handler.h>
#include <pdcp_layer/pkt_fate.h>
#include <simulator/controller_registry.h>
#include <simulator/simulator.h>
#include <ue/ue.h>
#include <utils/control/param_registry.h>

#include "../observability.h"

namespace
{
using namespace observability;

const char *const WORK_DIR = "build/examples";

void report(const char *name)
{
    std::cout << "ok   " << name << "\n";
}

//--------------------------------------------------------------------------------------------------
// Packets and the fake capture
//--------------------------------------------------------------------------------------------------

// A 40-byte IPv4 packet: version 4, header length 5, the given DSCP and ECN, protocol 17.
std::vector<std::uint8_t> ipv4_packet(int dscp, std::uint8_t ecn)
{
    std::vector<std::uint8_t> payload(40, 0);
    payload[0] = 0x45;
    payload[1] = static_cast<std::uint8_t>((dscp << 2) | (ecn & 0x03));
    payload[3] = 40;
    payload[8] = 64;
    payload[9] = 17;
    return payload;
}

nfq_packet_metadata metadata_of(const std::vector<std::uint8_t> &payload)
{
    nfq_packet_metadata meta;
    meta.bytes = payload.size();
    meta.payload = payload;
    return meta;
}

struct fake_capture : public pkt_capture
{
    explicit fake_capture(int queue_num) : pkt_capture(queue_num) {}

    void start() override {}
    void close() override {}

    bool verdict(std::uint32_t pkt_id, packet_capture_action) override
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        payloads.erase(pkt_id);
        return true;
    }

    // Goes through pkt_capture::handle_capture, as the netfilter callback does, so the packet is
    // annotated on its way in.
    void capture(std::uint32_t id, std::uint8_t ecn, std::vector<std::uint8_t> payload)
    {
        nfq_packet_metadata meta;
        meta.bytes = payload.size();
        meta.pkt_id = id;
        meta.ecn = ecn;
        meta.payload = payload;
        payloads[id] = std::move(payload);
        handle_capture_from_double(meta);
    }

    mutable std::mutex fake_mutex;
    std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> payloads;
};

// The six packets of the live scenario. Each is 40 bytes, 320 bits.
//
//   id   dscp  ecn      fate
//   100   46   ECT1     delivered
//   101   46   ECT1     delivered with a CE mark
//   102   10   ECT0     dropped
//   103    0   Not-ECT  delivered with a CE mark
//   104   34   ECT1     delivered
//   105   -    -        dropped, and not IPv4, so the annotator declines it
constexpr std::uint64_t packet_bits = 320;

// Pushes the scenario through a captured_packet_handler that belongs to this UE and direction.
void run_live_scenario(int ue_id, int tx_dir, int queue_num)
{
    pdcp_config config(4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    std::unique_ptr<fake_capture> fake(new fake_capture(queue_num));
    fake_capture *fake_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler handler(std::move(capture), nullptr, config, 1);
    handler.init();
    handler.set_fate_identity(ue_id, tx_dir, queue_num);

    const int dscps[6] = {46, 46, 10, 0, 34, 0};
    const std::uint8_t ecns[6] = {ECN_ECT1, ECN_ECT1, ECN_ECT0, ECN_NOT_ECT, ECN_ECT1, ECN_NOT_ECT};
    const char fates[6] = {'A', 'C', 'D', 'C', 'A', 'D'};
    for (std::uint32_t i = 0; i < 6; i++)
    {
        std::vector<std::uint8_t> payload = ipv4_packet(dscps[i], ecns[i]);
        if (i == 5)
            payload[0] = 0x60; // IPv6
        fake_ptr->capture(100 + i, ecns[i], payload);
    }

    handler.step(0.0);
    handler.ingest(tx_dir, 0.0);
    for (std::uint32_t i = 0; i < 6; i++)
    {
        assert(handler.has_ingress_pkts());
        ip_pkt pkt = handler.pop_ingress_pkt();
        assert(pkt.uid == 100 + i);
        harq_pkt harq(static_cast<int>(i), 0.0, 0.0, 0, 0, pkt.size, 0.0, 0.0);
        if (fates[i] == 'C')
        {
            pkt.ecn = ECN_CE;
            pkt.ce_marked = true;
        }
        harq.pkts.push_back(pkt);
        if (fates[i] == 'D')
        {
            harq.bits = pkt.size;
            handler.record_error(pkt.size, bit_fate::queue_dropped);
            handler.drop(std::move(harq), bit_fate::queue_dropped);
        }
        else
        {
            handler.push(std::move(harq));
        }
    }
    handler.step(0.01);
    handler.release();
    handler.release();
}

//--------------------------------------------------------------------------------------------------
// Registration
//--------------------------------------------------------------------------------------------------
void test_registration()
{
    assert(annotator_count() == 1);
    assert(ingest_observer_count() == 1);
    assert(fate_observer_count() == 1);

    const std::vector<std::string> names = registered_controller_names();
    assert(names.size() == 1 && names[0] == "observability");

    const std::optional<metric_entry> entry = metric_registry::find(counted_pf_metric_id);
    assert(entry && entry->name == "pf_counted");
    assert(counted_pf_metric_id >= METRIC_EXTERNAL_FIRST);

    const param_registry &params = param_registry::instance();
    const param_entry *report_entry = params.find("example.dscp_report");
    const param_entry *filter_entry = params.find("example.dscp_filter");
    assert(report_entry && report_entry->type == param_type::boolean);
    assert(filter_entry && filter_entry->type == param_type::text);
    assert(filter_entry->max_length == 256);
    report("registration");
}

//--------------------------------------------------------------------------------------------------
// Annotator
//--------------------------------------------------------------------------------------------------
void test_annotator()
{
    for (int dscp : {0, 10, 34, 46, 63})
    {
        ext_blob blob;
        assert(annotate_ipv4(metadata_of(ipv4_packet(dscp, ECN_ECT1)), blob));
        assert(blob.kind == blob_kind);
        assert(blob.data[0] == dscp);
        assert(blob.data[1] == 17);
    }

    // The ECN bits do not change the class.
    for (std::uint8_t ecn : {ECN_NOT_ECT, ECN_ECT0, ECN_ECT1, ECN_CE})
    {
        ext_blob blob;
        assert(annotate_ipv4(metadata_of(ipv4_packet(46, ecn)), blob));
        assert(blob.data[0] == 46);
    }

    // A header longer than 20 bytes is fine as long as it fits in the packet.
    {
        std::vector<std::uint8_t> long_header = ipv4_packet(26, ECN_NOT_ECT);
        long_header[0] = 0x46;
        ext_blob blob;
        assert(annotate_ipv4(metadata_of(long_header), blob));
        assert(blob.data[0] == 26);
    }

    // What it declines: too short, not IPv4, a header length below 5 or beyond the packet.
    std::vector<std::vector<std::uint8_t>> bad;
    bad.push_back(std::vector<std::uint8_t>());
    bad.push_back(std::vector<std::uint8_t>(19, 0x45));
    std::vector<std::uint8_t> v6 = ipv4_packet(46, ECN_NOT_ECT);
    v6[0] = 0x65;
    bad.push_back(v6);
    std::vector<std::uint8_t> short_header = ipv4_packet(46, ECN_NOT_ECT);
    short_header[0] = 0x44;
    bad.push_back(short_header);
    std::vector<std::uint8_t> beyond = ipv4_packet(46, ECN_NOT_ECT);
    beyond[0] = 0x4f;
    beyond.resize(40);
    bad.push_back(beyond); // 60 header bytes in a 40-byte packet
    for (const std::vector<std::uint8_t> &payload : bad)
    {
        ext_blob blob;
        assert(!annotate_ipv4(metadata_of(payload), blob));
        assert(blob.kind == 0);
    }
    report("annotator");
}

//--------------------------------------------------------------------------------------------------
// Live path
//--------------------------------------------------------------------------------------------------
void test_live_path()
{
    reset_for_tests();
    run_live_scenario(2, TX_DL, 77);

    const class_counters c46 = read_counters(TX_DL, 2, 46);
    assert(c46.packets == 2);
    assert(c46.delivered_bits == 2 * packet_bits);
    assert(c46.dropped_bits == 0);
    assert(c46.ce_packets == 1);

    const class_counters c10 = read_counters(TX_DL, 2, 10);
    assert(c10.packets == 1);
    assert(c10.delivered_bits == 0);
    assert(c10.dropped_bits == packet_bits);
    assert(c10.ce_packets == 0);

    const class_counters c0 = read_counters(TX_DL, 2, 0);
    assert(c0.packets == 1);
    assert(c0.delivered_bits == packet_bits);
    assert(c0.ce_packets == 1);

    const class_counters c34 = read_counters(TX_DL, 2, 34);
    assert(c34.packets == 1);
    assert(c34.delivered_bits == packet_bits);

    // The IPv6 packet was not annotated, so it lands in the unclassified class.
    const class_counters none = read_counters(TX_DL, 2, unclassified);
    assert(none.packets == 1);
    assert(none.dropped_bits == packet_bits);

    // Delays are measured from arrival (0.0) to the moment of the fate (release at 0.01 s for a
    // delivered packet), so they are positive and bounded by the length of the run.
    assert(c46.delay_ns > 0 && c46.delay_ns <= 2 * 10000000ull);
    assert(c34.delay_ns > 0 && c34.delay_ns <= 10000000ull);

    // Nothing leaked into the other UE, the other direction or the other classes.
    std::uint64_t total = 0;
    for (int dir : {TX_DL, TX_UL})
        for (int ue = 0; ue < 4; ue++)
            for (int cls = 0; cls < class_count; cls++)
                total += read_counters(dir, ue, cls).packets;
    assert(total == 6);

    // Every ingested packet has left the pending table, and nothing fell outside the table.
    assert(pending_packets() == 0);
    assert(untracked_packets() == 0);
    assert(out_of_range() == 0);
    assert(annotator_failures() == 0);
    assert(ingest_observer_failures() == 0);
    assert(fate_observer_failures() == 0);

    // A UE beyond the table is counted as out of range, but its packets still leave the pending
    // table, and the table of the other UEs does not change.
    run_live_scenario(max_ues + 5, TX_UL, 78);
    assert(out_of_range() == 6);
    assert(pending_packets() == 0);
    assert(read_counters(TX_DL, 2, 46).packets == 2);
    assert(read_counters(TX_UL, 2, 46).packets == 0);

    // The same scenario on the uplink of another UE.
    run_live_scenario(3, TX_UL, 79);
    assert(read_counters(TX_UL, 3, 46).packets == 2);
    assert(read_counters(TX_DL, 3, 46).packets == 0);
    report("live path");
}

//--------------------------------------------------------------------------------------------------
// Instrumented metric
//--------------------------------------------------------------------------------------------------

// The PF expression written out again, apart from the registry, so that the comparison is not the
// metric against itself.
float reference_pf(float alpha, const metric_info &info)
{
    const float rate = std::max(info.current_tp, 0.0f);
    const float numerator = alpha == 0.0f ? 1.0f : std::pow(rate, alpha);
    const float denominator = std::pow(std::max(info.avrg_tp, 1e-6f), 1.0f);
    return numerator / denominator;
}

bool same_bits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

void test_metric_equals_pf()
{
    reset_for_tests();
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> rate(-5.0f, 5000.0f);
    std::uniform_real_distribution<float> average(0.0f, 3000.0f);

    std::uint64_t expected_calls = 0;
    for (float alpha : {1.0f, 2.0f, 0.5f, 0.0f})
    {
        metric_handler pf(METRIC_PF, 0.9f, 0.001, 0.8f, alpha);
        metric_handler counted(counted_pf_metric_id, 0.9f, 0.001, 0.8f, alpha);

        const throughput_metric_recipe &a = pf.get_throughput_recipe();
        const throughput_metric_recipe &b = counted.get_throughput_recipe();
        assert(a.rate_exponent == b.rate_exponent);
        assert(a.history_exponent == b.history_exponent);
        assert(a.uses_history == b.uses_history);
        assert(a.supports_provisional_history == b.supports_provisional_history);
        assert(counted.is_throughput_metric() && !counted.is_rr());

        for (int i = 0; i < 2000; i++)
        {
            metric_info info;
            info.current_tp = rate(rng);
            info.avrg_tp = i % 7 == 0 ? 0.0f : average(rng);
            info.ue_id = 3;
            info.tx_dir = TX_UL;
            const float want = pf.get_metric(info, 1.0, i % 25);
            const float got = counted.get_metric(info, 1.0, i % 25);
            assert(same_bits(want, got));
            assert(same_bits(want, reference_pf(alpha, info)));
            expected_calls++;
        }
    }
    // The counter saw each call of the instrumented metric, for this UE and direction only.
    assert(score_calls(TX_UL, 3) == expected_calls);
    assert(score_calls(TX_DL, 3) == 0);
    assert(score_calls(TX_UL, 2) == 0);
    assert(out_of_range() == 0);

    // A metric_info that names no UE is scored all the same, and noted as out of range.
    metric_handler counted(counted_pf_metric_id);
    metric_info anonymous;
    anonymous.current_tp = 10.0f;
    anonymous.avrg_tp = 5.0f;
    assert(same_bits(counted.get_metric(anonymous, 0.0, 0), 2.0f));
    assert(out_of_range() == 1);
    report("metric equals PF");
}

// Several threads score the same UE at once, as the grid does. Run under TSAN for the data race
// check; here the total must be exact.
void test_metric_concurrent()
{
    reset_for_tests();
    metric_handler pf(METRIC_PF);
    metric_handler counted(counted_pf_metric_id);
    const int threads = 4;
    const int per_thread = 20000;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++)
    {
        workers.emplace_back(
            [&, t]()
            {
                for (int i = 0; i < per_thread; i++)
                {
                    metric_info info;
                    info.current_tp = static_cast<float>(1 + (i * 7 + t) % 997);
                    info.avrg_tp = static_cast<float>(1 + (i * 13) % 211);
                    info.ue_id = 1;
                    info.tx_dir = TX_DL;
                    if (!same_bits(pf.get_metric(info, 0.0, i % 25), counted.get_metric(info, 0.0, i % 25)))
                        mismatches++;
                }
            });
    }
    for (std::thread &worker : workers)
        worker.join();
    assert(mismatches == 0);
    assert(score_calls(TX_DL, 1) == static_cast<std::uint64_t>(threads) * per_thread);
    report("metric concurrent");
}

//--------------------------------------------------------------------------------------------------
// Runtime parameters
//--------------------------------------------------------------------------------------------------
void test_parameters()
{
    reset_for_tests();
    std::string reason;

    // Default: everything is reported.
    assert(report_enabled(0));
    assert(filter_text(0).empty());
    assert(class_selected(0, 46) && class_selected(0, unclassified));

    // A list is stored as a set and read back in canonical form: ascending, none last.
    assert(set_filter(0, " 46 , 10,none,46", reason));
    assert(filter_text(0) == "10,46,none");
    assert(class_selected(0, 10) && class_selected(0, 46) && class_selected(0, unclassified));
    assert(!class_selected(0, 0) && !class_selected(0, 34));
    // It belongs to the UE.
    assert(filter_text(1).empty() && class_selected(1, 0));

    // A value that is not accepted leaves the state as it was, and says why.
    const char *const rejected[] = {"64", "-1", "4x", "46,,10", ",", "46,", "NONE", "100", "4 6"};
    for (const char *text : rejected)
    {
        reason.clear();
        assert(!set_filter(0, text, reason));
        assert(!reason.empty());
        assert(filter_text(0) == "10,46,none");
    }
    assert(!set_filter(max_ues, "46", reason));
    assert(!set_filter(-1, "46", reason));

    // Empty text restores "every class".
    assert(set_filter(0, "", reason));
    assert(filter_text(0).empty());
    assert(set_filter(0, "   ", reason));
    assert(filter_text(0).empty());

    // The switch is independent from the filter and survives it.
    assert(set_report_enabled(0, false, reason));
    assert(!report_enabled(0) && report_enabled(1));
    assert(set_filter(0, "46", reason));
    assert(!report_enabled(0));
    assert(set_report_enabled(0, true, reason));
    assert(report_enabled(0) && filter_text(0) == "46");
    assert(!set_report_enabled(max_ues, true, reason));

    // The registry validates the text before the entry sees it: type and length.
    const param_registry &params = param_registry::instance();
    const param_entry *filter_entry = params.find("example.dscp_filter");
    assert(filter_entry != nullptr);
    assert(params.check(*filter_entry, param_value(std::string("46,none")), reason));
    assert(!params.check(*filter_entry, param_value(std::string(300, '1')), reason));
    assert(!params.check(*filter_entry, param_value(1.0), reason));
    const param_entry *report_entry = params.find("example.dscp_report");
    assert(params.check(*report_entry, param_value(true), reason));
    assert(!params.check(*report_entry, param_value(std::string("yes")), reason));
    reset_for_tests();
    report("parameters");
}

//--------------------------------------------------------------------------------------------------
// Reporter
//--------------------------------------------------------------------------------------------------
struct recorder
{
    std::vector<metric_point> points;
    point_sink sink()
    {
        return [this](const metric_point &p) { points.push_back(p); };
    }
    // The points of one measurement, keyed by the tag that tells them apart.
    std::map<std::string, metric_point> by_dscp(const std::string &ue, const std::string &dir) const
    {
        std::map<std::string, metric_point> out;
        for (const metric_point &p : points)
            if (p.measurement == "ex_dscp" && p.tags.at("ue") == ue && p.tags.at("dir") == dir)
                out[p.tags.at("dscp")] = p;
        return out;
    }
    std::size_t count(const std::string &measurement) const
    {
        std::size_t n = 0;
        for (const metric_point &p : points)
            if (p.measurement == measurement)
                n++;
        return n;
    }
};

double field(const metric_point &p, const std::string &name)
{
    return p.fields.at(name).value;
}

void test_reporter()
{
    reset_for_tests();
    recorder rec;
    dscp_reporter reporter(10, 4, rec.sink());
    std::string reason;

    // Nothing has happened: nothing to report.
    reporter.report(4);
    assert(rec.points.empty());

    run_live_scenario(1, TX_DL, 80);
    reporter.report(4);
    {
        const std::map<std::string, metric_point> dl = rec.by_dscp("1", "dl");
        assert(dl.size() == 5);
        assert(rec.points.size() == 5);
        const metric_point &p46 = dl.at("46");
        assert(field(p46, "packets") == 2);
        assert(field(p46, "delivered_bits") == 2 * packet_bits);
        assert(field(p46, "dropped_bits") == 0);
        assert(field(p46, "ce_packets") == 1);
        assert(field(p46, "delay_ms") > 0.0 && field(p46, "delay_ms") <= 10.0);
        assert(p46.fields.at("packets").aggregation == field_aggregation::sum);
        assert(p46.fields.at("delay_ms").aggregation == field_aggregation::mean);
        assert(field(dl.at("10"), "dropped_bits") == packet_bits);
        assert(field(dl.at("none"), "dropped_bits") == packet_bits);
        assert(p46.tags.size() == 3);
    }

    // A second report in a row covers nothing new.
    const std::size_t before = rec.points.size();
    reporter.report(4);
    assert(rec.points.size() == before);

    // A filter limits the classes that are reported.
    rec.points.clear();
    assert(set_filter(1, "46,none", reason));
    run_live_scenario(1, TX_DL, 81);
    reporter.report(4);
    {
        const std::map<std::string, metric_point> dl = rec.by_dscp("1", "dl");
        assert(dl.size() == 2 && dl.count("46") == 1 && dl.count("none") == 1);
        // Same totals as before: one scenario per report.
        assert(field(dl.at("46"), "packets") == 2);
    }

    // With the report off nothing goes out, and the interval is not carried over to the next one.
    rec.points.clear();
    assert(set_filter(1, "", reason));
    assert(set_report_enabled(1, false, reason));
    run_live_scenario(1, TX_DL, 82);
    reporter.report(4);
    assert(rec.points.empty());
    assert(set_report_enabled(1, true, reason));
    run_live_scenario(1, TX_DL, 83);
    reporter.report(4);
    {
        const std::map<std::string, metric_point> dl = rec.by_dscp("1", "dl");
        assert(dl.size() == 5);
        assert(field(dl.at("46"), "packets") == 2);
    }

    // Directions are reported apart, and a UE beyond the reporter's list is ignored.
    rec.points.clear();
    run_live_scenario(2, TX_UL, 84);
    run_live_scenario(9, TX_DL, 85);
    reporter.report(4);
    assert(rec.by_dscp("2", "ul").size() == 5);
    assert(rec.by_dscp("2", "dl").empty());
    assert(rec.points.size() == 5);

    // The scheduling counter is reported as the change since the previous report.
    rec.points.clear();
    metric_handler counted(counted_pf_metric_id);
    metric_info info;
    info.current_tp = 4.0f;
    info.avrg_tp = 2.0f;
    info.ue_id = 0;
    info.tx_dir = TX_DL;
    for (int i = 0; i < 25; i++)
        counted.get_metric(info, 0.0, i);
    reporter.report(4);
    assert(rec.count("ex_sched") == 1);
    assert(rec.points[0].tags.at("ue") == "0" && rec.points[0].tags.at("dir") == "dl");
    assert(field(rec.points[0], "score_calls") == 25);
    reporter.report(4);
    assert(rec.count("ex_sched") == 1);

    // The period is applied by on_tick: only every tenth TTI, and never at TTI 0. With no UE in
    // the list there is nothing to report either way, so this only checks that it does not throw.
    const std::vector<ue> no_ues;
    for (std::uint64_t tti = 0; tti < 30; tti++)
        reporter.on_tick(tick_context{0.0, tti, no_ues});

    bool threw = false;
    try
    {
        dscp_reporter bad(0, 1, rec.sink());
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    assert(threw);
    reset_for_tests();
    report("reporter");
}

//--------------------------------------------------------------------------------------------------
// The [Observability] section
//--------------------------------------------------------------------------------------------------
std::string write_ini(const std::string &body)
{
    const std::string path = std::string(WORK_DIR) + "/observability_section.ini";
    std::ofstream out(path);
    assert(out.is_open());
    out << body;
    return path;
}

bool make_throws(const std::string &body)
{
    try
    {
        make_reporter(write_ini(body), 2, point_sink());
    }
    catch (const std::invalid_argument &)
    {
        return true;
    }
    return false;
}

void test_section()
{
    assert(make_reporter("build/examples/does_not_exist.ini", 2, point_sink()) == nullptr);
    assert(make_reporter(write_ini("[Global]\nduration: 1\n"), 2, point_sink()) == nullptr);
    assert(make_reporter(write_ini("[Observability]\nenabled: false\n"), 2, point_sink()) == nullptr);
    assert(make_reporter(write_ini("[Observability]\n"), 2, point_sink()) != nullptr);
    assert(make_reporter(write_ini("; comment\n[Observability]\nperiod_tti: 5\nenabled: true\n[Other]\nx: y\n"), 2,
                         point_sink()) != nullptr);
    assert(make_throws("[Observability]\nperiod_tti: 0\n"));
    assert(make_throws("[Observability]\nperiod_tti: ten\n"));
    assert(make_throws("[Observability]\nperiod_tti: -5\n"));
    assert(make_throws("[Observability]\nenabled: maybe\n"));
    assert(make_throws("[Observability]\nunknown_key: 1\n"));
    assert(make_throws("[Observability]\nno separator here\n"));
    // Keys of other sections are not this section's business.
    assert(make_reporter(write_ini("[Other]\nunknown_key: 1\n"), 2, point_sink()) == nullptr);
    report("section");
}

//--------------------------------------------------------------------------------------------------
// Simulated run
//--------------------------------------------------------------------------------------------------

// examples/observability/config.ini with the metric, the threads and the length chosen here and
// no pacing and no telemetry: it is a test of results, not of the clock.
std::string write_scenario(int metric, bool threaded, bool observability_section)
{
    std::ifstream base("examples/observability/config.ini");
    assert(base.is_open());
    std::string text, line;
    bool skip = false;
    while (std::getline(base, line))
    {
        if (!line.empty() && line[0] == '[')
            skip = (line == "[Monitoring]") || (line == "[Observability]" && !observability_section);
        if (skip)
            continue;
        if (line.rfind("metric_type:", 0) == 0)
            line = "metric_type: " + std::to_string(metric);
        else if (line.rfind("duration:", 0) == 0)
            line = "duration: 0.2";
        else if (line.rfind("period:", 0) == 0)
            line = "period: -1";
        else if (line.rfind("multithreading:", 0) == 0)
            line = std::string("multithreading: ") + (threaded ? "true" : "false");
        else if (line.rfind("threads:", 0) == 0)
            line = std::string("threads: ") + (threaded ? "2" : "0");
        text += line + "\n";
    }
    const std::string path = std::string(WORK_DIR) + "/observability_scenario.ini";
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
    return path;
}

struct outcome
{
    std::vector<class_counters> counters;
    std::vector<std::uint64_t> delivered;
    std::vector<float> averages;
    std::uint64_t scores = 0;
    std::uint64_t packets = 0;
};

bool same(const class_counters &a, const class_counters &b)
{
    return a.packets == b.packets && a.delivered_bits == b.delivered_bits
           && a.dropped_bits == b.dropped_bits && a.ce_packets == b.ce_packets && a.delay_ns == b.delay_ns;
}

outcome run_scenario(int metric, bool threaded, bool observability_section)
{
    reset_for_tests();
    simulator sim(write_scenario(metric, threaded, observability_section));
    assert(sim.ue_list()->size() == 2);
    sim.run_steps(300);

    outcome out;
    for (int dir : {TX_DL, TX_UL})
    {
        for (int ue_id = 0; ue_id < 2; ue_id++)
        {
            out.scores += score_calls(dir, ue_id);
            for (int cls = 0; cls < class_count; cls++)
            {
                out.counters.push_back(read_counters(dir, ue_id, cls));
                out.packets += out.counters.back().packets;
            }
            const ue &u = (*sim.ue_list())[static_cast<std::size_t>(ue_id)];
            out.delivered.push_back(u.get_delivered_bits_total(dir));
            out.averages.push_back(u.get_throughput_average_bits_per_tti(dir));
        }
    }
    return out;
}

void test_simulated_run()
{
    const outcome plain = run_scenario(METRIC_PF, false, false);
    const outcome counted = run_scenario(counted_pf_metric_id, false, true);

    // The reference run exercised the fate path with the instrumented metric absent.
    assert(plain.packets > 50);
    assert(plain.scores == 0);
    // Simulated packets do not pass the capture, so they all fall in one class.
    for (std::size_t i = 0; i < plain.counters.size(); i++)
        if (i % class_count != static_cast<std::size_t>(unclassified))
            assert(plain.counters[i].packets == 0);
    std::uint64_t delivered_bits = 0;
    for (const class_counters &c : plain.counters)
        delivered_bits += c.delivered_bits;
    assert(delivered_bits > 0);

    // The instrumented metric was used, and the cell did the same as with PF, bit for bit.
    assert(counted.scores > 1000);
    assert(plain.counters.size() == counted.counters.size());
    for (std::size_t i = 0; i < plain.counters.size(); i++)
        assert(same(plain.counters[i], counted.counters[i]));
    assert(plain.delivered == counted.delivered);
    assert(plain.averages == counted.averages);
    assert(pending_packets() == 0);
    assert(out_of_range() == 0);

    // With the grid and the UEs on worker threads, the counters of the cell are the same.
    const outcome threaded = run_scenario(counted_pf_metric_id, true, true);
    assert(threaded.scores == counted.scores);
    for (std::size_t i = 0; i < plain.counters.size(); i++)
        assert(same(plain.counters[i], threaded.counters[i]));
    assert(plain.delivered == threaded.delivered);
    report("simulated run");
}

// The parameter entries applied to the UEs of a real simulator, which is what the control plane
// does at the quiescent point.
void test_parameters_on_ues()
{
    reset_for_tests();
    simulator sim(write_scenario(counted_pf_metric_id, false, true));
    assert(sim.ue_list()->size() == 2);
    ue &u1 = (*sim.ue_list())[1];

    const param_registry &params = param_registry::instance();
    const param_entry *filter_entry = params.find("example.dscp_filter");
    const param_entry *report_entry = params.find("example.dscp_report");
    std::string reason;

    assert(filter_entry->apply(u1, param_value(std::string("46,none")), reason));
    param_value value;
    assert(params.read_value(*filter_entry, u1, value));
    assert(std::get<std::string>(value) == "46,none");
    assert(filter_text(0).empty());

    assert(!filter_entry->apply(u1, param_value(std::string("99")), reason));
    assert(!reason.empty());
    assert(filter_text(1) == "46,none");
    assert(!filter_entry->apply(u1, param_value(1.0), reason));

    assert(report_entry->apply(u1, param_value(false), reason));
    assert(params.read_value(*report_entry, u1, value));
    assert(std::get<bool>(value) == false);
    assert(!report_enabled(1) && report_enabled(0));
    assert(report_entry->apply(u1, param_value(true), reason));
    assert(report_enabled(1));

    // The catalogue that the control plane describes lists both.
    const std::string json = params.describe_json();
    assert(json.find("example.dscp_report") != std::string::npos);
    assert(json.find("example.dscp_filter") != std::string::npos);
    reset_for_tests();
    report("parameters on UEs");
}
} // namespace

int main()
{
    std::filesystem::create_directories(WORK_DIR);

    test_registration();
    test_annotator();
    test_live_path();
    test_metric_equals_pf();
    test_metric_concurrent();
    test_parameters();
    test_reporter();
    test_section();
    test_simulated_run();
    test_parameters_on_ues();

    std::remove((std::string(WORK_DIR) + "/observability_section.ini").c_str());
    std::remove((std::string(WORK_DIR) + "/observability_scenario.ini").c_str());
    std::cout << "observability example: all tests passed\n";
    return 0;
}
