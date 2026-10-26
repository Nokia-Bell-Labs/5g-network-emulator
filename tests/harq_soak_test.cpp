#include <cassert>
#include <cstdint>

#include <mac_layer/harq_handler.h>

int main()
{
    harq_handler first(
        4,
        0.0f,
        0.004f,
        0.001f,
        0.001f,
        0.0005f,
        20260927,
        0,
        harq_model::legacy_bler,
        128);
    harq_handler replay(
        4,
        0.0f,
        0.020f,
        0.010f,
        0.015f,
        0.005f,
        20260927,
        0,
        harq_model::legacy_bler,
        128);
    first.init(1, 16);
    replay.init(1, 16);
    harq_pkt first_timing_probe(
        1, 0.0f, 0.0f, 0, 100.0f, 1, 0.0f, 0.0f);
    harq_pkt replay_timing_probe(
        1, 0.0f, 0.0f, 0, 100.0f, 1, 0.0f, 0.0f);
    first.step(0.0f);
    replay.step(0.0f);
    assert(first.enqueue_retry(first_timing_probe, 100.0f, 1));
    assert(replay.enqueue_retry(replay_timing_probe, 100.0f, 1));
    first.step(1.0f);
    replay.step(1.0f);
    assert(first.is_pkt_ready());
    assert(replay.is_pkt_ready());
    (void)first.get_pkt();
    (void)replay.get_pkt();

    std::uint64_t first_hash = 1469598103934665603ULL;
    std::uint64_t replay_hash = 1469598103934665603ULL;
    for (int index = 0; index < 1000000; index++)
    {
        const int mcs = index % 28;
        const float sinr = -20.0f + static_cast<float>(index % 60);
        const int attempt = index % 5;
        const int layers = 1 + index % 4;
        const bool first_nack =
            first.get_rtx(mcs, sinr, attempt, layers);
        const bool replay_nack =
            replay.get_rtx(mcs, sinr, attempt, layers);
        first_hash =
            (first_hash ^ static_cast<std::uint64_t>(first_nack))
            * 1099511628211ULL;
        replay_hash =
            (replay_hash ^ static_cast<std::uint64_t>(replay_nack))
            * 1099511628211ULL;
    }
    assert(first_hash == replay_hash);

    harq_handler bounded(
        4,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1,
        0,
        harq_model::legacy_bler,
        32);
    bounded.init(0, 1);
    bounded.step(0.0f);
    for (int index = 0; index < 100000; index++)
    {
        harq_pkt pkt(
            index,
            0.0f,
            0.0f,
            0,
            0.0f,
            1,
            0.0f,
            0.0f);
        assert(bounded.enqueue_retry(pkt, 0.0f, 1));
        assert(bounded.is_pkt_ready());
        (void)bounded.get_pkt();
    }
    assert(bounded.size() == 0);
    assert(bounded.high_water_mark() == 1);

    return 0;
}
