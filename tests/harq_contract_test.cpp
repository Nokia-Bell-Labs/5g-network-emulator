#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include <mac_layer/harq_handler.h>

namespace
{
bool near(double actual, double expected, double tolerance = 1e-12)
{
    return std::fabs(actual - expected) <= tolerance;
}

template <typename Function>
bool throws(Function function)
{
    try
    {
        function();
    }
    catch (const std::exception &)
    {
        return true;
    }
    return false;
}

harq_pkt block(int id, std::uint64_t bits)
{
    return harq_pkt(id, 0.0f, 0.0f, 0, 100, bits, 0.0f, 0.0f);
}
} // namespace

int main()
{
    assert(near(harq_handler::legacy_failure_probability(0.0, 0), 0.0));
    assert(near(harq_handler::legacy_failure_probability(0.0, 4), 0.0));
    assert(near(harq_handler::legacy_failure_probability(0.1, 0), 0.125));
    assert(near(harq_handler::legacy_failure_probability(0.1, 1), 0.19));
    assert(near(harq_handler::legacy_failure_probability(0.1, 2), 0.2168));
    assert(near(harq_handler::legacy_failure_probability(1.0, 0), 1.0));
    assert(near(harq_handler::legacy_failure_probability(1.0, 1), 1.0));
    assert(near(harq_handler::legacy_failure_probability(1.0, 2), 0.8));
    assert(throws([] {
        (void)harq_handler::legacy_failure_probability(
            std::numeric_limits<double>::quiet_NaN(),
            0);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_failure_probability(0.1, -1);
    }));

    const double table_value =
        harq_handler::legacy_bler(0, 1, 1, 0, 0.0f);
    assert(std::isfinite(table_value));
    assert(table_value >= 0.0 && table_value <= 1.0);
    assert(throws([] {
        (void)harq_handler::legacy_bler(-1, 1, 1, 0, 0.0f);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_bler(0, 3, 1, 0, 0.0f);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_bler(0, 1, 0, 0, 0.0f);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_bler(0, 1, 1, -1, 0.0f);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_bler(0, 1, 1, 28, 0.0f);
    }));
    assert(throws([] {
        (void)harq_handler::legacy_bler(
            0,
            1,
            1,
            0,
            std::numeric_limits<float>::infinity());
    }));

    harq_handler disabled(
        4,
        0.0f,
        0.004f,
        0.0f,
        0.001f,
        0.0f,
        1,
        0,
        harq_model::disabled);
    assert(!disabled.get_rtx(28, 0.0f, 0, 9));

    harq_handler active(
        4,
        0.0f,
        0.004f,
        0.0f,
        0.001f,
        0.0f,
        2,
        0,
        harq_model::legacy_bler);
    active.init(0, 1);
    active.set_scripted_outcomes({0.0, 1.0});
    assert(active.get_rtx(0, -20.0f, 0, 1));
    assert(!active.get_rtx(0, 20.0f, 0, 1));
    assert(active.retry_available_after(0));
    assert(active.retry_available_after(3));
    assert(!active.retry_available_after(4));

    active.step(0.0f);
    harq_pkt first_block = block(1, 100);
    assert(active.enqueue_retry(first_block, 100.0f, 1));
    assert(!active.is_pkt_ready());
    active.step(0.006f);
    assert(active.is_pkt_ready());
    const harq_pkt first = active.get_pkt();
    assert(first.id == 1);
    assert(first.attempt_ordinal == 1);
    assert(active.size() == 0);
    assert(active.high_water_mark() == 1);

    harq_handler bounded(
        1,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        3,
        0,
        harq_model::legacy_bler,
        1);
    bounded.init(0, 1);
    bounded.step(0.0f);
    harq_pkt second_block = block(2, 100);
    harq_pkt third_block = block(3, 100);
    assert(bounded.enqueue_retry(second_block, 0.0f, 1));
    assert(!bounded.enqueue_retry(third_block, 0.0f, 1));
    assert(third_block.bits == 100);

    harq_handler timed(
        2,
        0.0f,
        0.005f,
        0.0f,
        0.0f,
        0.0f,
        4,
        0,
        harq_model::legacy_bler);
    timed.init(0, 1);
    timed.step(0.0f);
    harq_pkt earlier = block(4, 100);
    assert(timed.enqueue_retry(earlier, 0.0f, 1));
    timed.step(0.002f);
    harq_pkt later = block(5, 100);
    assert(timed.enqueue_retry(later, 0.0f, 1));
    timed.step(0.006f);
    assert(timed.is_pkt_ready());
    assert(timed.get_pkt().id == 4);
    assert(!timed.is_pkt_ready());
    timed.step(0.008f);
    assert(timed.is_pkt_ready());
    assert(timed.get_pkt().id == 5);

    return 0;
}
