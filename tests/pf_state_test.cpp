#include <cassert>
#include <cmath>

#include <mac_layer/throughput_state.h>

namespace
{
bool near(float actual, float expected, float tolerance = 1e-4f)
{
    return std::fabs(actual - expected) <= tolerance;
}
} // namespace

int main()
{
    throughput_history_state state(100.0f);
    assert(!state.initialized());

    state.prepare(1000.0f);
    assert(state.initialized());
    assert(near(state.average_throughput(), 1000.0f));

    const float coefficient = 1.0f - std::exp(-1.0f / 100.0f);
    assert(near(
        state.projected_average(2000.0f),
        (1.0f - coefficient) * 1000.0f + coefficient * 2000.0f));

    state.update(0.0f, false);
    assert(near(state.average_throughput(), 1000.0f));

    state.update(0.0f, true);
    assert(near(
        state.average_throughput(),
        (1.0f - coefficient) * 1000.0f));

    const float previous = state.average_throughput();
    state.update(500.0f, true);
    assert(near(
        state.average_throughput(),
        (1.0f - coefficient) * previous + coefficient * 500.0f));

    state.reset();
    assert(!state.initialized());
    assert(near(state.average_throughput(), 0.0f));

    return 0;
}
