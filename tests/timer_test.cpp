// The ticker's timestamps are 64-bit microseconds and keep growing past 2^32 us.
//
// A 32-bit count wraps after 71.6 minutes. The emulator takes the timestamp as simulated
// time, so in a live run the wrap sent simulated time back to zero and traffic stopped
// being forwarded for good. The timer can start from a time that has already elapsed, which
// lets this test cross the wrap in a fraction of a second instead of waiting for it.
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <timer/timer.h>

namespace
{
constexpr std::uint64_t WRAP_US = 1ULL << 32;

static_assert(sizeof(timer::ts_us_t) == 8, "timer timestamps must be 64-bit");

struct run_result
{
    std::vector<std::uint64_t> stamps;
};

// Runs a real-time timer (1 ms period) that starts at `start_us`, for `run_ms`.
run_result run_timer(float goal_s, std::uint64_t start_us, int run_ms)
{
    run_result result;
    timer t(1.0f, goal_s);
    t.start(
        [&result](std::uint64_t ts) { result.stamps.push_back(ts); },
        std::chrono::microseconds(start_us));
    std::this_thread::sleep_for(std::chrono::milliseconds(run_ms));
    t.stop();
    return result;
}

void check_stream(const run_result &r, std::uint64_t start_us, std::uint64_t must_pass_us)
{
    // About 1 per ms; the bounds are loose on purpose, this is not a timing test. The
    // upper one is what catches a loop that stopped sleeping.
    assert(r.stamps.size() >= 50);
    assert(r.stamps.size() <= 400);
    assert(r.stamps.front() >= start_us);
    for (std::size_t i = 1; i < r.stamps.size(); i++)
        assert(r.stamps[i] > r.stamps[i - 1]);   // never goes back, wrap or not
    assert(r.stamps.back() > must_pass_us);
}

// Open-ended run (live mode: duration -1) across the 32-bit wrap.
void test_open_ended_crosses_32_bit_wrap()
{
    const std::uint64_t start = WRAP_US - 50000;          // 50 ms before the wrap
    const run_result r = run_timer(-1.0f, start, 300);
    check_stream(r, start, WRAP_US + 100000);
}

// Finite duration across the wrap.
void test_finite_duration_crosses_32_bit_wrap()
{
    const std::uint64_t start = WRAP_US - 50000;
    const run_result r = run_timer(10000.0f, start, 300);
    check_stream(r, start, WRAP_US + 100000);
}

// Finite duration after 2^31 us (35.8 minutes): period * (count + 1) and the goal in
// microseconds overflowed an int there.
void test_finite_duration_past_signed_32_bit()
{
    const std::uint64_t start = (1ULL << 31) + 1000000;
    const run_result r = run_timer(10000.0f, start, 300);
    check_stream(r, start, start + 100000);
}

// A goal beyond what an int holds in microseconds (2147 s) must not end the run at once.
void test_long_goal_is_not_truncated()
{
    const run_result r = run_timer(5000.0f, 0, 200);
    check_stream(r, 0, 100000);
}

// An exception thrown by the callback must not reach std::terminate on the ticker thread:
// the loop ends and wait_finished() rethrows it once, on the waiting thread. Simulated
// mode (period < 0) runs as fast as it can, so the failure comes at once; the open-ended
// loop is the one that a live run uses.
void expect_failure_is_rethrown(double goal_s, double period_ms)
{
    int calls = 0;
    timer t(period_ms, goal_s);
    t.start([&calls](std::uint64_t)
    {
        if (++calls == 5)
            throw std::runtime_error("callback failed");
    });
    bool caught = false;
    try
    {
        t.wait_finished();
    }
    catch (const std::runtime_error &e)
    {
        caught = std::string(e.what()) == "callback failed";
    }
    assert(caught);
    assert(calls == 5);          // the loop ended at the failure
    t.wait_finished();           // reported once; a second wait is quiet
    t.stop();
}

void test_callback_exception_reaches_the_waiting_thread()
{
    expect_failure_is_rethrown(100.0, -1.0);   // finite goal, simulated mode
    expect_failure_is_rethrown(-1.0, -1.0);    // open ended, simulated mode
    expect_failure_is_rethrown(100.0, 1.0);    // finite goal, real time
    expect_failure_is_rethrown(-1.0, 1.0);     // open ended, real time
}

// stop() and the destructor never throw, even with a failure pending.
void test_stop_does_not_rethrow()
{
    timer t(-1.0, 100.0);
    t.start([](std::uint64_t) { throw std::runtime_error("boom"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.stop();
}
} // namespace

int main()
{
    test_open_ended_crosses_32_bit_wrap();
    test_finite_duration_crosses_32_bit_wrap();
    test_finite_duration_past_signed_32_bit();
    test_long_goal_is_not_truncated();
    test_callback_exception_reaches_the_waiting_thread();
    test_stop_does_not_rethrow();
    std::printf("timer_test: ok\n");
    return 0;
}
