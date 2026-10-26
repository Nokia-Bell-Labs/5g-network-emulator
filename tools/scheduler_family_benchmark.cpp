#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <common/direction.h>
#include <simulator/simulator.h>

namespace
{
double jain_index(const std::vector<float> &values)
{
    double sum = 0.0;
    double squared_sum = 0.0;
    for (float value : values)
    {
        sum += value;
        squared_sum += static_cast<double>(value) * value;
    }
    if (values.empty() || squared_sum == 0.0)
        return 0.0;
    return sum * sum
           / (static_cast<double>(values.size()) * squared_sum);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4)
    {
        std::cerr
            << "Usage: scheduler_family_benchmark CONFIG STEPS "
            << "[WARMUP_STEPS]\n";
        return 2;
    }

    const std::string config_path = argv[1];
    const unsigned int steps =
        static_cast<unsigned int>(std::strtoul(argv[2], nullptr, 10));
    const unsigned int warmup_steps =
        argc == 4
            ? static_cast<unsigned int>(
                  std::strtoul(argv[3], nullptr, 10))
            : 0;
    simulator sim(config_path);

    sim.run_steps(warmup_steps);
    std::vector<float> initial_dl_bits;
    std::vector<float> initial_ul_bits;
    for (ue &terminal : *sim.ue_list())
    {
        initial_dl_bits.push_back(
            terminal.get_delivered_bits_total(TX_DL));
        initial_ul_bits.push_back(
            terminal.get_delivered_bits_total(TX_UL));
        terminal.reset_scheduler_statistics();
    }
    std::vector<double> tti_us;
    tti_us.reserve(steps);
    double wall_us = 0.0;
    for (unsigned int step = 0; step < steps; step++)
    {
        const auto start = std::chrono::steady_clock::now();
        sim.run_steps(1);
        const double elapsed_us =
            std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start)
                .count();
        tti_us.push_back(elapsed_us);
        wall_us += elapsed_us;
    }

    std::vector<float> dl_throughputs;
    std::vector<float> ul_throughputs;
    int max_dl_gap = 0;
    int max_ul_gap = 0;
    size_t ue_index = 0;
    for (ue &terminal : *sim.ue_list())
    {
        const float measurement_seconds =
            static_cast<float>(steps) * 0.001f;
        dl_throughputs.push_back(
            (terminal.get_delivered_bits_total(TX_DL)
             - initial_dl_bits[ue_index])
            / measurement_seconds
            / 1e6f);
        ul_throughputs.push_back(
            (terminal.get_delivered_bits_total(TX_UL)
             - initial_ul_bits[ue_index])
            / measurement_seconds
            / 1e6f);
        max_dl_gap =
            std::max(max_dl_gap, terminal.get_max_service_gap_ttis(TX_DL));
        max_ul_gap =
            std::max(max_ul_gap, terminal.get_max_service_gap_ttis(TX_UL));
        ue_index++;
    }

    double dl_total = 0.0;
    double ul_total = 0.0;
    for (float value : dl_throughputs)
        dl_total += value;
    for (float value : ul_throughputs)
        ul_total += value;

    std::cout
        << "BENCHMARK {"
        << "\"steps\":" << steps
        << ",\"warmup_steps\":" << warmup_steps
        << ",\"ues\":" << dl_throughputs.size()
        << ",\"wall_ms\":" << wall_us / 1000.0
        << ",\"us_per_tti\":" << wall_us / steps
        << ",\"dl_total_mbps\":" << dl_total
        << ",\"ul_total_mbps\":" << ul_total
        << ",\"dl_jain\":" << jain_index(dl_throughputs)
        << ",\"ul_jain\":" << jain_index(ul_throughputs)
        << ",\"dl_max_service_gap_ttis\":" << max_dl_gap
        << ",\"ul_max_service_gap_ttis\":" << max_ul_gap
        << ",\"tti_us\":[";
    for (size_t index = 0; index < tti_us.size(); index++)
    {
        if (index > 0)
            std::cout << ",";
        std::cout << tti_us[index];
    }
    std::cout << "]}\n";
    return 0;
}
