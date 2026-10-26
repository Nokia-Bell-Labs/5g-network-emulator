#include <cassert>
#include <cmath>

#include <mac_layer/metric_handler.h>

namespace
{
bool near(float actual, float expected, float tolerance = 1e-5f)
{
    return std::fabs(actual - expected) <= tolerance;
}
} // namespace

int main()
{
    metric_info info;
    info.current_tp = 20.0f;
    info.avrg_tp = 100.0f;

    metric_handler max_throughput(
        METRIC_MAX_TP, 0.5f, 0.1f, 0.001f, 7.0f);
    assert(max_throughput.is_throughput_metric());
    assert(!max_throughput.uses_throughput_history());
    assert(!max_throughput.supports_provisional_history());
    assert(near(max_throughput.get_metric(info, 0.0f, 0), 20.0f));

    metric_handler proportional_fair(
        METRIC_PF, 0.5f, 0.1f, 0.001f, 1.0f);
    assert(proportional_fair.is_pf());
    assert(proportional_fair.uses_throughput_history());
    assert(proportional_fair.supports_provisional_history());
    assert(near(proportional_fair.get_metric(info, 0.0f, 0), 0.2f));

    metric_handler throughput_fair(
        METRIC_PF, 0.5f, 0.1f, 0.001f, 0.0f);
    assert(near(throughput_fair.get_metric(info, 0.0f, 0), 0.01f));

    metric_handler bet(
        METRIC_BET, 0.5f, 0.1f, 0.001f, 1.0f);
    assert(!bet.is_pf());
    assert(bet.is_throughput_metric());
    assert(bet.uses_throughput_history());
    assert(bet.supports_provisional_history());
    assert(near(bet.get_metric(info, 0.0f, 0), 0.01f));

    metric_info cold;
    cold.current_tp = 4.0f;
    cold.avrg_tp = 0.0f;
    assert(near(
        proportional_fair.get_metric(cold, 0.0f, 0),
        4.0e6f,
        1.0f));
    assert(near(bet.get_metric(cold, 0.0f, 0), 1.0e6f, 1.0f));

    return 0;
}
