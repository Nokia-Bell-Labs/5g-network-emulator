// Packet annotation with nothing registered. The registries are process-wide and freeze on first
// use, so the default behaviour is checked in a binary of its own that registers nothing.
// capture_annotation_test.cpp checks the same scenario with hooks registered and compares the
// result against the same literal.

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common/direction.h>
#include <netfilter/capture_annotation.h>
#include <netfilter/pkt_capture.h>
#include <pdcp_layer/captured_packet_handler.h>
#include <pdcp_layer/packet_handler.h>

namespace
{
const char *const expected_scenario =
    "released=100,101,103,104;dropped=102,105;"
    "verdicts=100:A,101:C,102:D,103:C,104:A,105:D;tos=1,3,3,1;"
    "accept=2;accept_ce=2;drop=2;delivered_bits=1280;queue_dropped_bits=640";

struct fake_capture : public pkt_capture
{
    explicit fake_capture(int queue_num) : pkt_capture(queue_num) {}

    void start() override {}
    void close() override {}

    bool verdict(uint32_t pkt_id, packet_capture_action action) override
    {
        std::lock_guard<std::mutex> lock(fake_mutex);
        verdicts.emplace_back(pkt_id, action);
        if (action == packet_capture_action::DROP)
        {
            dropped.push_back(pkt_id);
            payloads.erase(pkt_id);
            return true;
        }
        released.push_back(pkt_id);
        std::vector<uint8_t> payload = payloads[pkt_id];
        if (action == packet_capture_action::ACCEPT_CE)
            payload[1] = (payload[1] & 0xfc) | ECN_CE;
        released_tos.push_back(payload[1] & 0x03);
        payloads.erase(pkt_id);
        return true;
    }

    // Goes through pkt_capture::handle_capture, as the netfilter callback does.
    void capture_via_handle(uint32_t id, uint8_t ecn, std::vector<uint8_t> payload)
    {
        nfq_packet_metadata meta;
        meta.bytes = payload.size();
        meta.pkt_id = id;
        meta.ecn = ecn;
        meta.payload = payload;
        payloads[id] = std::move(payload);
        handle_capture_from_double(meta);
    }

    // Bypasses it: the record is enqueued with a default blob.
    void capture_direct(uint32_t id, uint8_t ecn, std::vector<uint8_t> payload)
    {
        captured_packet_info info;
        info.bytes = payload.size();
        info.pkt_id = id;
        info.ecn = ecn;
        payloads[id] = payload;
        enqueue_captured_packet(info, std::move(payload), ecn);
    }

    std::vector<uint32_t> released;
    std::vector<uint32_t> dropped;
    std::vector<int> released_tos;
    std::vector<std::pair<uint32_t, packet_capture_action>> verdicts;

    mutable std::mutex fake_mutex;
    std::unordered_map<uint32_t, std::vector<uint8_t>> payloads;
};

std::vector<uint8_t> make_payload(uint8_t ecn, uint8_t marker)
{
    std::vector<uint8_t> payload(40, 0);
    payload[0] = 0x45;
    payload[1] = ecn & 0x03;
    payload[3] = 40;
    payload[8] = 64;
    payload[9] = 17;
    payload[20] = marker;
    for (std::size_t i = 21; i < payload.size(); i++)
        payload[i] = static_cast<uint8_t>(i);
    return payload;
}

std::string verdict_letter(packet_capture_action action)
{
    if (action == packet_capture_action::ACCEPT)
        return "A";
    if (action == packet_capture_action::ACCEPT_CE)
        return "C";
    return "D";
}

template <typename T>
std::string join(const std::vector<T> &values)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); i++)
        out << (i ? "," : "") << values[i];
    return out.str();
}

// Six packets, fates accept, CE, drop, CE, accept, drop, through a captured_packet_handler. The
// digest covers the verdicts that reach the capture, the ECN written into the released payloads
// and the counters of the handler.
std::string run_scenario(bool through_handle_capture)
{
    pdcp_config config(4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    std::unique_ptr<fake_capture> fake(new fake_capture(77));
    fake_capture *fake_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler handler(std::move(capture), nullptr, config, 1);
    handler.init();

    const char fates[6] = {'A', 'C', 'D', 'C', 'A', 'D'};
    const uint8_t ecns[6] = {ECN_ECT1, ECN_ECT1, ECN_ECT0, ECN_NOT_ECT, ECN_ECT1, ECN_NOT_ECT};
    for (uint32_t i = 0; i < 6; i++)
    {
        std::vector<uint8_t> payload = make_payload(ecns[i], 0xA1);
        if (through_handle_capture)
            fake_ptr->capture_via_handle(100 + i, ecns[i], payload);
        else
            fake_ptr->capture_direct(100 + i, ecns[i], payload);
    }

    handler.step(0.0);
    handler.ingest(TX_DL, 0.0);
    for (uint32_t i = 0; i < 6; i++)
    {
        assert(handler.has_ingress_pkts());
        ip_pkt pkt = handler.pop_ingress_pkt();
        assert(pkt.uid == static_cast<int>(100 + i));
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

    pdcp_queue_status status;
    handler.fill_queue_status(status, 0.01);
    std::vector<std::string> verdicts;
    for (std::size_t i = 0; i < fake_ptr->verdicts.size(); i++)
        verdicts.push_back(
            std::to_string(fake_ptr->verdicts[i].first) + ":"
            + verdict_letter(fake_ptr->verdicts[i].second));

    std::ostringstream out;
    out << "released=" << join(fake_ptr->released)
        << ";dropped=" << join(fake_ptr->dropped)
        << ";verdicts=" << join(verdicts)
        << ";tos=" << join(fake_ptr->released_tos)
        << ";accept=" << status.final_accept_packets
        << ";accept_ce=" << status.final_accept_ce_packets
        << ";drop=" << status.final_drop_packets
        << ";delivered_bits=" << handler.delivered_bits_total()
        << ";queue_dropped_bits=" << handler.queue_dropped_bits_total();
    return out.str();
}

void report(const char *name)
{
    std::cout << "ok   " << name << "\n";
}

void test_nothing_registered()
{
    assert(annotator_count() == 0);
    assert(ingest_observer_count() == 0);
    assert(annotator_failures() == 0);
    assert(ingest_observer_failures() == 0);
    report("nothing is registered by default");
}

void test_blob_is_zero_without_annotators()
{
    fake_capture capture(5);
    capture.capture_via_handle(7, ECN_ECT0, make_payload(ECN_ECT0, 0xA1));
    captured_packet_info info;
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 7);
    assert(info.bytes == 40);
    assert(info.ecn == ECN_ECT0);
    assert(info.blob.kind == 0);
    for (std::size_t i = 0; i < sizeof(info.blob.data); i++)
        assert(info.blob.data[i] == 0);
    assert(!capture.pop_captured_packet(info));
    report("captured_packet_info keeps a zero blob when no annotator exists");
}

void test_hooks_are_frozen_after_first_use()
{
    // The capture above reached annotate_packet, and the handler runs ingest below.
    bool threw = false;
    try
    {
        register_annotator([](const nfq_packet_metadata &, ext_blob &) { return false; });
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);
    assert(annotator_count() == 0);

    std::unique_ptr<fake_capture> fake(new fake_capture(6));
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    pdcp_config config(4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    captured_packet_handler handler(std::move(capture), nullptr, config, 1);
    handler.ingest(TX_DL, 0.0);

    threw = false;
    try
    {
        register_ingest_observer([](std::uint32_t, int, int, const ext_blob &) {});
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);
    assert(ingest_observer_count() == 0);
    report("registries freeze at first use even when empty");
}

void test_scenario_matches_literal()
{
    const std::string direct = run_scenario(false);
    const std::string via_handle = run_scenario(true);
    assert(direct == expected_scenario);
    assert(via_handle == expected_scenario);
    report("verdict and counter digest with nothing registered");
}
}

int main()
{
    test_nothing_registered();
    test_blob_is_zero_without_annotators();
    test_hooks_are_frozen_after_first_use();
    test_scenario_matches_literal();
    return 0;
}
