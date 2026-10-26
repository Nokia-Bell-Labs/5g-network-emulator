#include <cassert>
#include <cmath>
#include <fstream>
#include <string>

#include <common/direction.h>
#include <simulator/simulator.h>

namespace
{
const char *MAP = "build/tests/grouping_invariance_map.json";

void write_config(const std::string &path, int scheduling_type)
{
    std::ofstream out(path);
    out << "[Global]\n"
        << "duration: 0.01\n"
        << "period: -1\n"
        << "multithreading: false\n"
        << "verbose: false\n"
        << "[UE]\n"
        << "ue_id: grouping\n"
        << "ue_type: 1\n"
        << "n_ues: 1\n"
        << "n_antennas: 1\n"
        << "cqi_period: 1\n"
        << "ri_period: 1\n"
        << "random_v: false\n"
        << "traffic_type: 0\n"
        << "ul_target: 1000\n"
        << "dl_target: 1000\n"
        << "var_perc: 0\n"
        << "pkt_size: 12000\n"
        << "mobility_type: 0\n"
        << "pos_x: 100\n"
        << "pos_y: 0\n"
        << "random_init: false\n"
        << "max_distance: 500\n"
        << "priority: 1\n"
        << "pkt_delay_budget: 10\n"
        << "ue_height: 1.5\n"
        << "ue_location_type: outdoor\n"
        << "[Scenario]\n"
        << "scenario_type: 0\n"
        << "map_file: " << MAP << "\n"
        << "[eNBConfig]\n"
        << "modulation_m: 1\n"
        << "target_ber: 0.00005\n"
        << "cqi_mode: 1\n"
        << "tx_power: 43\n"
        << "eNB_gain: 8.7\n"
        << "UT_gain: 0\n"
        << "frequency: 2380000000\n"
        << "bandwidth: 20000000\n"
        << "[MACLayer]\n"
        << "metric_type: 5\n"
        << "mimo_layers: 1\n"
        << "n_ofdm_syms: 14\n"
        << "n_re_freq: 12\n"
        << "numerology: 1\n"
        << "harq_model: disabled\n"
        << "mcs_tables: true\n"
        << "scheduling_mode: 1\n"
        << "scheduling_type: " << scheduling_type << "\n"
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

float run(const std::string &path, int scheduling_type)
{
    write_config(path, scheduling_type);
    simulator simulation(path);
    simulation.run_steps(5);
    return simulation.ue_list()->front().get_mean_sinr(TX_DL);
}
} // namespace

int main()
{
    std::ofstream map(MAP);
    map
        << "{\"schema_version\":2,"
        << "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        << "\"cell_number\":3,\"cell_size\":500,"
        << "\"map\":[[-100,-100,-100],[-100,-100,-100],"
        << "[-100,-100,-100]]}\n";
    map.close();

    const float grouped = run(
        "build/tests/grouping_invariance_grouped.ini", 0);
    const float per_prb = run(
        "build/tests/grouping_invariance_per_prb.ini", 1);
    assert(std::fabs(grouped - per_prb) < 1e-4f);
    return 0;
}
