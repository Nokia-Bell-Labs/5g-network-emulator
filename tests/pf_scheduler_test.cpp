#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <common/direction.h>
#include <simulator/simulator.h>

namespace
{
void write_config(
    const std::string &path,
    int cqi_period,
    int metric_type,
    const std::string &intra_tti_update,
    bool near_far = false,
    bool asymmetric_priority = false)
{
    std::ofstream out(path);
    out << "[Global]\n"
        << "duration: 1.0\n"
        << "period: -1\n"
        << "multithreading: false\n"
        << "threads: 0\n"
        << "verbose: false\n";
    auto write_ue = [&](const char *name, int count, int position, int priority) {
        out << "[UE]\n"
            << "ue_id: " << name << "\n"
            << "ue_type: 1\n"
            << "n_ues: " << count << "\n"
            << "n_antennas: 1\n"
            << "ul_power_mode: fixed\n"
            << "tx_power_ul: 23\n"
            << "cqi_period: " << cqi_period << "\n"
            << "ri_period: 5\n"
            << "random_v: false\n"
            << "traffic_type: 0\n"
            << "ul_target: 1000\n"
            << "dl_target: 1000\n"
            << "var_perc: 0\n"
            << "pkt_size: 12000\n"
            << "mobility_type: 0\n"
            << "pos_x: " << position << "\n"
            << "pos_y: 0\n"
            << "random_init: false\n"
            << "speed: 0\n"
            << "max_distance: 1000\n"
            << "priority: " << priority << "\n"
            << "pkt_delay_budget: 10\n"
            << "ue_height: 1.5\n"
            << "ue_location_type: outdoor\n";
    };
    if (near_far)
    {
        write_ue("schedulerNear", 1, 50, 1);
        write_ue("schedulerFar", 1, 500, 1);
    }
    else if (asymmetric_priority)
    {
        write_ue("schedulerHighPriority", 1, 200, 100);
        write_ue("schedulerLowPriority", 1, 200, 1);
    }
    else
    {
        write_ue("schedulerEqual", 16, 200, 1);
    }
    out << "[Scenario]\n"
        << "scenario_type: 1\n"
        << "[eNBConfig]\n"
        << "modulation_m: 1\n"
        << "target_ber: 0.00005\n"
        << "cqi_mode: 1\n"
        << "tx_power: 46\n"
        << "eNB_gain: 8.7\n"
        << "UT_gain: 0\n"
        << "frequency: 3500000000\n"
        << "bandwidth: 20000000\n"
        << "[MACLayer]\n"
        << "metric_type: " << metric_type << "\n";
    if (metric_type == METRIC_PF)
        out << "pf_alpha: 1\n";
    out << "throughput_time_window_ms: 100\n"
        << "throughput_intra_tti_update: " << intra_tti_update << "\n"
        << "mimo_layers: 1\n"
        << "n_ofdm_syms: 14\n"
        << "n_re_freq: 12\n"
        << "numerology: 1\n"
        << "harq_model: disabled\n"
        << "mcs_tables: true\n"
        << "scheduling_mode: 1\n"
        << "scheduling_type: 0\n"
        << "scheduling_config: 1\n"
        << "duplexing_type: 0\n"
        << "n_dl_slots: 7\n"
        << "n_ul_slots: 3\n"
        << "transition_c: 54\n"
        << "[PHYLayer]\n"
        << "interference_ues: 0\n"
        << "interference_eNBs: 0\n"
        << "interfered_bandwidth_ratio: 0\n"
        << "thermal_noise: -174\n"
        << "enb_noise_figure: 2\n"
        << "ut_noise_figure: 9\n";
}

struct scheduler_result
{
    std::vector<float> throughputs;
    std::vector<float> history_averages;
    int maximum_service_gap_ttis = 0;
};

scheduler_result run_scheduler(
    const std::string &path,
    int cqi_period,
    int metric_type,
    const std::string &intra_tti_update = "none",
    bool near_far = false,
    bool asymmetric_priority = false)
{
    write_config(
        path,
        cqi_period,
        metric_type,
        intra_tti_update,
        near_far,
        asymmetric_priority);
    simulator sim(path);
    sim.run_steps(1000);

    scheduler_result result;
    for (ue &terminal : *sim.ue_list())
    {
        result.throughputs.push_back(terminal.get_avg_tp(TX_DL));
        result.history_averages.push_back(
            terminal.get_throughput_average_bits_per_tti(TX_DL));
        result.maximum_service_gap_ttis = std::max(
            result.maximum_service_gap_ttis,
            terminal.get_max_service_gap_ttis(TX_DL));
        assert(terminal.get_pkt_delay_budget() == 10.0f);
    }
    return result;
}

void assert_detach_resets_history(int metric_type)
{
    const std::string path =
        "build/tests/throughput_detach_reset_"
        + std::to_string(metric_type) + ".ini";
    write_config(path, 5, metric_type, "none");
    simulator sim(path);
    sim.run_steps(10);
    ue &terminal = sim.ue_list()->front();
    assert(terminal.get_throughput_average_bits_per_tti(TX_DL) > 0.0f);
    terminal.set_enabled(false);
    assert(terminal.get_throughput_average_bits_per_tti(TX_DL) == 0.0f);
}

float jain_index(const std::vector<float> &values)
{
    float sum = 0.0f;
    float squared_sum = 0.0f;
    for (float value : values)
    {
        sum += value;
        squared_sum += value * value;
    }
    return sum * sum
           / (static_cast<float>(values.size()) * squared_sum);
}
} // namespace

int main()
{
    assert_detach_resets_history(METRIC_PF);
    assert_detach_resets_history(METRIC_BET);

    const scheduler_result fast_cqi = run_scheduler(
        "build/tests/pf_scheduler_cqi_1.ini", 1, METRIC_PF);
    const scheduler_result slow_cqi = run_scheduler(
        "build/tests/pf_scheduler_cqi_20.ini", 20, METRIC_PF);

    assert(fast_cqi.throughputs.size() == 16);
    assert(slow_cqi.throughputs.size() == fast_cqi.throughputs.size());
    for (size_t index = 0; index < fast_cqi.throughputs.size(); index++)
    {
        assert(fast_cqi.throughputs[index] > 0.0f);
        assert(slow_cqi.throughputs[index] > 0.0f);
        assert(
            std::fabs(
                fast_cqi.history_averages[index]
                - slow_cqi.history_averages[index])
            < 1e-3f);
    }
    assert(jain_index(fast_cqi.throughputs) > 0.98f);
    assert(jain_index(slow_cqi.throughputs) > 0.98f);

    const scheduler_result bet_fast_cqi = run_scheduler(
        "build/tests/bet_scheduler_cqi_1.ini", 1, METRIC_BET);
    const scheduler_result bet_slow_cqi = run_scheduler(
        "build/tests/bet_scheduler_cqi_20.ini", 20, METRIC_BET);
    assert(jain_index(bet_fast_cqi.throughputs) > 0.98f);
    assert(jain_index(bet_slow_cqi.throughputs) > 0.98f);
    for (size_t index = 0; index < bet_fast_cqi.throughputs.size(); index++)
    {
        assert(
            std::fabs(
                bet_fast_cqi.history_averages[index]
                - bet_slow_cqi.history_averages[index])
            < 1e-3f);
    }

    const scheduler_result bet_without_reranking = run_scheduler(
        "build/tests/bet_scheduler_no_reranking.ini",
        5,
        METRIC_BET,
        "none");
    const scheduler_result bet_with_reranking = run_scheduler(
        "build/tests/bet_scheduler_reranking.ini",
        5,
        METRIC_BET,
        "allocation_unit");
    assert(
        bet_with_reranking.maximum_service_gap_ttis
        <= bet_without_reranking.maximum_service_gap_ttis);

    for (const int metric : {METRIC_MAX_TP, METRIC_RR})
    {
        const scheduler_result result = run_scheduler(
            "build/tests/no_history_scheduler_"
                + std::to_string(metric) + ".ini",
            5,
            metric);
        for (float average : result.history_averages)
            assert(average == 0.0f);
        assert(jain_index(result.throughputs) > 0.98f);
    }

    const scheduler_result max_throughput_near_far = run_scheduler(
        "build/tests/max_throughput_near_far.ini",
        5,
        METRIC_MAX_TP,
        "none",
        true);
    assert(max_throughput_near_far.throughputs.size() == 2);
    assert(
        max_throughput_near_far.throughputs[0]
        > max_throughput_near_far.throughputs[1]);

    for (const int metric : {METRIC_BET, METRIC_MAX_TP, METRIC_PF})
    {
        const scheduler_result weighted = run_scheduler(
            "build/tests/weighted_throughput_scheduler_"
                + std::to_string(metric) + ".ini",
            5,
            metric,
            "none",
            false,
            true);
        assert(weighted.throughputs.size() == 2);
        assert(weighted.throughputs[0] > weighted.throughputs[1]);
    }

    const scheduler_result round_robin_priorities = run_scheduler(
        "build/tests/round_robin_priority_noop.ini",
        5,
        METRIC_RR,
        "none",
        false,
        true);
    assert(round_robin_priorities.throughputs.size() == 2);
    assert(jain_index(round_robin_priorities.throughputs) > 0.98f);
    return 0;
}
