// Packet annotation with annotators and ingest observers registered. The registries are
// process-wide and freeze at first use, so every registration is a file-scope object below and
// all of them run before main(). The empty-registry behaviour is checked by
// capture_annotation_default_test.cpp, which registers nothing.

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
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
// Payload layout used by the annotators: a 20-byte IPv4 header, then one marker byte that selects
// the behaviour, then a counting pattern.
const std::uint8_t marker_first = 0xA1;
const std::uint8_t marker_second = 0xA2;
const std::uint8_t marker_dirty = 0xDD;
const std::uint8_t marker_throw = 0xEE;
const std::uint8_t marker_plain = 0x00;

std::uint8_t marker_of(const nfq_packet_metadata &meta)
{
    return meta.payload.size() > 20 ? meta.payload[20] : 0;
}

struct annotator_calls
{
    std::atomic<int> dirty{0};
    std::atomic<int> first{0};
    std::atomic<int> thrower{0};
    std::atomic<int> second{0};
    std::atomic<int> second_saw_first_marker{0};
};
annotator_calls calls;

// Writes to the blob and then declines: the core must discard the write.
bool dirty_annotator(const nfq_packet_metadata &meta, ext_blob &blob)
{
    calls.dirty++;
    if (marker_of(meta) != marker_dirty)
        return false;
    blob.data[0] = 0x55;
    blob.kind = 9;
    return false;
}

// Copies the first 32 bytes of the packet into the blob.
bool first_annotator(const nfq_packet_metadata &meta, ext_blob &blob)
{
    calls.first++;
    if (marker_of(meta) != marker_first)
        return false;
    std::memcpy(blob.data, meta.payload.data(), sizeof(blob.data));
    blob.kind = 1;
    return true;
}

bool throwing_annotator(const nfq_packet_metadata &meta, ext_blob &blob)
{
    calls.thrower++;
    if (marker_of(meta) != marker_throw)
        return false;
    blob.data[1] = 0x77;
    blob.kind = 7;
    throw std::runtime_error("annotator failure");
}

bool second_annotator(const nfq_packet_metadata &meta, ext_blob &blob)
{
    calls.second++;
    const std::uint8_t marker = marker_of(meta);
    if (marker == marker_first)
        calls.second_saw_first_marker++;
    if (marker != marker_first && marker != marker_second)
        return false;
    blob.data[0] = 0x22;
    blob.kind = 2;
    return true;
}

struct observation
{
    std::uint32_t uid;
    int tx_dir;
    int queue_num;
    ext_blob blob;
};

std::vector<observation> observations;
std::atomic<int> throwing_observer_calls{0};

// Registered first and throwing for every uid divisible by 5: the observer after it must still run.
void throwing_observer(std::uint32_t uid, int, int, const ext_blob &)
{
    throwing_observer_calls++;
    if (uid % 5 == 0)
        throw std::runtime_error("observer failure");
}

void recording_observer(std::uint32_t uid, int tx_dir, int queue_num, const ext_blob &blob)
{
    observations.push_back(observation{uid, tx_dir, queue_num, blob});
}

struct registrar
{
    registrar()
    {
        register_annotator(&dirty_annotator);
        register_annotator(&first_annotator);
        register_annotator(&throwing_annotator);
        register_annotator(&second_annotator);
        register_ingest_observer(&throwing_observer);
        register_ingest_observer(&recording_observer);
    }
};
const registrar registration;

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

// Same scenario as capture_annotation_default_test.cpp, with the same expected literal. Six packets,
// fates accept, CE, drop, CE, accept, drop, through a captured_packet_handler. The digest covers
// the verdicts that reach the capture, the ECN written into the released payloads and the counters
// of the handler.
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
        std::vector<uint8_t> payload = make_payload(ecns[i], marker_first);
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

bool blob_is_zero(const ext_blob &blob)
{
    for (std::size_t i = 0; i < sizeof(blob.data); i++)
        if (blob.data[i] != 0)
            return false;
    return blob.kind == 0;
}

struct packet_spec
{
    std::uint32_t id;
    std::uint8_t marker;
};

void test_registration_before_first_use()
{
    assert(annotator_count() == 4);
    assert(ingest_observer_count() == 2);
    assert(annotator_failures() == 0);
    assert(ingest_observer_failures() == 0);

    bool threw = false;
    try
    {
        register_annotator(nullptr);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    assert(threw);
    threw = false;
    try
    {
        register_ingest_observer(nullptr);
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    assert(threw);
    assert(calls.dirty == 0 && calls.first == 0 && calls.thrower == 0 && calls.second == 0);
    report("registrations made before main are all present and null is rejected");
}

void test_annotators_fill_the_capture_record()
{
    fake_capture capture(5);
    const std::vector<uint8_t> first_payload = make_payload(ECN_ECT1, marker_first);
    capture.capture_via_handle(31, ECN_ECT1, first_payload);
    capture.capture_via_handle(32, ECN_ECT0, make_payload(ECN_ECT0, marker_second));
    capture.capture_via_handle(33, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_plain));

    captured_packet_info info;
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 31 && info.bytes == 40 && info.ecn == ECN_ECT1);
    assert(info.blob.kind == 1);
    assert(std::memcmp(info.blob.data, first_payload.data(), sizeof(info.blob.data)) == 0);

    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 32 && info.bytes == 40 && info.ecn == ECN_ECT0);
    assert(info.blob.kind == 2);
    assert(info.blob.data[0] == 0x22);
    for (std::size_t i = 1; i < sizeof(info.blob.data); i++)
        assert(info.blob.data[i] == 0);

    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 33 && info.ecn == ECN_NOT_ECT);
    assert(blob_is_zero(info.blob));
    assert(!capture.pop_captured_packet(info));
    report("annotators fill the blob of the capture record and leave the other fields alone");
}

void test_observer_sees_each_uid_once_with_the_same_bytes()
{
    pdcp_config config(4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    std::unique_ptr<fake_capture> dl_fake(new fake_capture(41));
    std::unique_ptr<fake_capture> ul_fake(new fake_capture(42));
    fake_capture *dl_ptr = dl_fake.get();
    fake_capture *ul_ptr = ul_fake.get();
    std::unique_ptr<pkt_capture> dl_capture(std::move(dl_fake));
    std::unique_ptr<pkt_capture> ul_capture(std::move(ul_fake));
    captured_packet_handler dl_handler(std::move(dl_capture), nullptr, config, 1);
    captured_packet_handler ul_handler(std::move(ul_capture), nullptr, config, 2);

    const packet_spec dl_packets[] = {
        {11, marker_first}, {12, marker_second}, {13, marker_plain}, {14, marker_first}, {16, marker_second}};
    const packet_spec ul_packets[] = {
        {21, marker_second}, {22, marker_first}, {23, marker_plain}, {24, marker_first}};
    std::unordered_map<std::uint32_t, std::vector<uint8_t>> sent;
    for (const packet_spec &p : dl_packets)
    {
        sent[p.id] = make_payload(ECN_NOT_ECT, p.marker);
        dl_ptr->capture_via_handle(p.id, ECN_NOT_ECT, sent[p.id]);
    }
    for (const packet_spec &p : ul_packets)
    {
        sent[p.id] = make_payload(ECN_ECT1, p.marker);
        ul_ptr->capture_via_handle(p.id, ECN_ECT1, sent[p.id]);
    }

    observations.clear();
    dl_handler.ingest(TX_DL, 0.0);
    assert(observations.size() == 5);
    ul_handler.ingest(TX_UL, 0.0);
    assert(observations.size() == 9);
    // A second ingest with nothing new must not report anything again.
    dl_handler.ingest(TX_DL, 0.001);
    ul_handler.ingest(TX_UL, 0.001);
    assert(observations.size() == 9);

    std::unordered_map<std::uint32_t, int> seen;
    for (const observation &o : observations)
    {
        seen[o.uid]++;
        const bool dl = o.uid < 20;
        assert(o.tx_dir == (dl ? TX_DL : TX_UL));
        assert(o.queue_num == (dl ? 41 : 42));
        const std::vector<uint8_t> &payload = sent.at(o.uid);
        const std::uint8_t marker = payload[20];
        if (marker == marker_first)
        {
            assert(o.blob.kind == 1);
            assert(std::memcmp(o.blob.data, payload.data(), sizeof(o.blob.data)) == 0);
        }
        else if (marker == marker_second)
        {
            assert(o.blob.kind == 2 && o.blob.data[0] == 0x22);
        }
        else
        {
            assert(blob_is_zero(o.blob));
        }
    }
    assert(seen.size() == 9);
    for (const std::pair<const std::uint32_t, int> &entry : seen)
        assert(entry.second == 1);

    // Handler order is preserved: DL uids first in arrival order, then UL.
    const std::uint32_t order[] = {11, 12, 13, 14, 16, 21, 22, 23, 24};
    for (std::size_t i = 0; i < 9; i++)
        assert(observations[i].uid == order[i]);
    report("observer is called once per uid with the annotated bytes, in both directions");
}

void test_first_recogniser_wins()
{
    fake_capture capture(8);
    const int first_before = calls.first;
    const int second_before = calls.second;
    const int thrower_before = calls.thrower;
    const int saw_before = calls.second_saw_first_marker;
    for (std::uint32_t i = 0; i < 4; i++)
        capture.capture_via_handle(60 + i, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_first));
    assert(calls.first - first_before == 4);
    assert(calls.thrower - thrower_before == 0);
    assert(calls.second - second_before == 0);
    assert(calls.second_saw_first_marker == saw_before);

    // A packet that only the second annotator knows is offered to every annotator in order.
    capture.capture_via_handle(70, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_second));
    assert(calls.first - first_before == 5);
    assert(calls.thrower - thrower_before == 1);
    assert(calls.second - second_before == 1);

    captured_packet_info info;
    for (std::uint32_t i = 0; i < 4; i++)
    {
        assert(capture.pop_captured_packet(info));
        assert(info.blob.kind == 1);
    }
    assert(capture.pop_captured_packet(info));
    assert(info.blob.kind == 2);
    report("the first annotator that recognises a packet wins and later ones are not called");
}

void test_declined_writes_are_discarded()
{
    fake_capture capture(9);
    capture.capture_via_handle(80, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_dirty));
    captured_packet_info info;
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 80);
    assert(blob_is_zero(info.blob));
    report("a blob written by an annotator that declines is reset");
}

void test_throwing_annotator_leaves_kind_zero()
{
    fake_capture capture(10);
    const std::uint64_t before = annotator_failures();
    capture.capture_via_handle(90, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_throw));
    capture.capture_via_handle(91, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_throw));
    capture.capture_via_handle(92, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_first));
    assert(annotator_failures() - before == 2);

    captured_packet_info info;
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 90 && blob_is_zero(info.blob));
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 91 && blob_is_zero(info.blob));
    // The packets are still enqueued and the next one is annotated normally.
    assert(capture.pop_captured_packet(info));
    assert(info.pkt_id == 92 && info.blob.kind == 1);
    report("an annotator that throws leaves kind 0 and is counted");
}

void test_throwing_observer_is_contained()
{
    pdcp_config config(4, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
    std::unique_ptr<fake_capture> fake(new fake_capture(51));
    fake_capture *fake_ptr = fake.get();
    std::unique_ptr<pkt_capture> capture(std::move(fake));
    captured_packet_handler handler(std::move(capture), nullptr, config, 1);
    for (std::uint32_t id = 200; id < 210; id++)
        fake_ptr->capture_via_handle(id, ECN_NOT_ECT, make_payload(ECN_NOT_ECT, marker_first));

    observations.clear();
    const std::uint64_t before = ingest_observer_failures();
    const int calls_before = throwing_observer_calls;
    handler.ingest(TX_DL, 0.0);
    // 200 and 205 are divisible by 5.
    assert(ingest_observer_failures() - before == 2);
    assert(throwing_observer_calls - calls_before == 10);
    assert(observations.size() == 10);
    assert(handler.has_ingress_pkts());
    report("an observer that throws is counted and neither stops the others nor the ingest");
}

void test_registration_after_freeze_throws()
{
    // Both registries have been used by now.
    const std::size_t annotators_before = annotator_count();
    const std::size_t observers_before = ingest_observer_count();

    bool threw = false;
    try
    {
        register_annotator(&first_annotator);
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        register_ingest_observer(&recording_observer);
    }
    catch (const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);
    assert(annotator_count() == annotators_before);
    assert(ingest_observer_count() == observers_before);
    report("registering after the freeze throws std::logic_error");
}

void test_hooks_do_not_change_verdicts()
{
    observations.clear();
    const std::string direct = run_scenario(false);
    const std::string annotated = run_scenario(true);
    assert(direct == annotated);
    assert(annotated == expected_scenario);
    // The observer ran for both feeds, six packets each, and only the annotated feed carried a blob.
    assert(observations.size() == 12);
    for (std::size_t i = 0; i < 6; i++)
        assert(blob_is_zero(observations[i].blob));
    for (std::size_t i = 6; i < 12; i++)
        assert(observations[i].blob.kind == 1);
    report("verdicts, ECN and counters are the same with annotators and observers registered");
}
}

int main()
{
    test_registration_before_first_use();
    test_annotators_fill_the_capture_record();
    test_observer_sees_each_uid_once_with_the_same_bytes();
    test_first_recogniser_wins();
    test_declined_writes_are_discarded();
    test_throwing_annotator_leaves_kind_zero();
    test_throwing_observer_is_contained();
    test_registration_after_freeze_throws();
    test_hooks_do_not_change_verdicts();
    return 0;
}
