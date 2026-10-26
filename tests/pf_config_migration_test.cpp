#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

#include <simulator/configuration_loader.h>

namespace
{
void write_config(
    const std::string &path,
    int metric_type,
    bool beta_metric,
    const std::string &time_window_key,
    const std::string &intra_tti_update,
    const std::string &intra_tti_key =
        "throughput_intra_tti_update",
    bool force_pf_alpha = false)
{
    std::ofstream out(path);
    out << "[Global]\n"
        << "duration: 0.001\n"
        << "period: -1\n"
        << "[UE]\n"
        << "ue_id: migration\n"
        << "ue_type: 1\n"
        << "n_ues: 1\n";
    if (beta_metric)
        out << "beta_metric: 0.5\n";
    out << "[Scenario]\n"
        << "scenario_type: 1\n"
        << "[eNBConfig]\n"
        << "frequency: 3500000000\n"
        << "bandwidth: 20000000\n"
        << "[MACLayer]\n"
        << "metric_type: " << metric_type << "\n";
    if (metric_type == METRIC_PF || force_pf_alpha)
        out << "pf_alpha: 1.0\n";
    if (!time_window_key.empty())
        out << time_window_key << ": 100\n";
    if (!intra_tti_update.empty())
        out << intra_tti_key << ": "
            << intra_tti_update << "\n";
}

bool rejected(const std::string &path)
{
    try
    {
        configuration_loader loader(path);
        (void)loader;
    }
    catch (const std::invalid_argument &)
    {
        return true;
    }
    return false;
}
} // namespace

int main()
{
    const std::string current_pf =
        "build/tests/throughput_config_pf.ini";
    write_config(
        current_pf,
        METRIC_PF,
        false,
        "throughput_time_window_ms",
        "allocation_unit");
    configuration_loader pf_loader(current_pf);
    const mac_config pf = pf_loader.get_mac_config();
    assert(pf.metric_type == METRIC_PF);
    assert(pf.pf_alpha == 1.0f);
    assert(pf.throughput_time_window_ms == 100.0f);
    assert(pf.throughput_intra_tti_update);

    const std::string current_bet =
        "build/tests/throughput_config_bet.ini";
    write_config(
        current_bet,
        METRIC_BET,
        false,
        "throughput_time_window_ms",
        "allocation_unit");
    configuration_loader bet_loader(current_bet);
    const mac_config bet = bet_loader.get_mac_config();
    assert(bet.metric_type == METRIC_BET);
    assert(bet.throughput_intra_tti_update);

    const std::string old_time_key =
        "build/tests/throughput_config_old_time_key.ini";
    write_config(
        old_time_key,
        METRIC_PF,
        false,
        "pf_time_window_ms",
        "none");
    assert(rejected(old_time_key));

    const std::string old_update_key =
        "build/tests/throughput_config_old_update_key.ini";
    write_config(
        old_update_key,
        METRIC_PF,
        false,
        "throughput_time_window_ms",
        "none",
        "pf_intra_tti_update");
    assert(rejected(old_update_key));

    const std::string beta_pf =
        "build/tests/throughput_config_beta_pf.ini";
    write_config(
        beta_pf,
        METRIC_PF,
        true,
        "throughput_time_window_ms",
        "none");
    assert(rejected(beta_pf));

    const std::string beta_bet =
        "build/tests/throughput_config_beta_bet.ini";
    write_config(
        beta_bet,
        METRIC_BET,
        true,
        "throughput_time_window_ms",
        "none");
    assert(rejected(beta_bet));

    const std::string pf_alpha_bet =
        "build/tests/throughput_config_pf_alpha_bet.ini";
    write_config(
        pf_alpha_bet,
        METRIC_BET,
        false,
        "throughput_time_window_ms",
        "none",
        "throughput_intra_tti_update",
        true);
    assert(rejected(pf_alpha_bet));

    for (const int metric : {METRIC_MAX_TP, METRIC_RR})
    {
        const std::string path =
            "build/tests/throughput_config_no_history_"
            + std::to_string(metric) + ".ini";
        write_config(
            path,
            metric,
            false,
            "throughput_time_window_ms",
            "allocation_unit");
        assert(rejected(path));
    }

    return 0;
}
