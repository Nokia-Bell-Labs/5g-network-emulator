#include <simulator/configuration_loader.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <libgen.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#include <utils/terminal_logging.h>
#include <utils/monitoring/monitoring_config.h>

std::string getBaseMapPath()
{
    const std::string map_dir = "include/maps_scenarios/";
    const std::string prefix = "macroscopic_fading_map_";

    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1)
    {
        exe_path[len] = '\0';
        std::string beside_binary = std::string(dirname(exe_path)) + "/../" + map_dir;

        // bin/fikore sits next to include/, which is what a copied or containerised
        // deployment relies on. Binaries built elsewhere, such as the test ones under
        // build/tests/, do not, so fall back to the working directory for them.
        if (access(beside_binary.c_str(), F_OK) == 0)
        {
            return beside_binary + prefix;
        }
    }

    return map_dir + prefix;
}

ue_full_config::ue_full_config()
{
    // UE CONFIG
    ue_c.ue_m.n_antennas = N_ANTENNAS_DEFAULT;
    ue_c.ue_m.alpha_ul = ALPHA_UL_DEFAULT;
    ue_c.ue_m.nominal_pusch_p0 = NOMINAL_PUSCH_P0_DEFAULT;
    ue_c.ue_m.power_mode = ul_power_mode::fixed;
    ue_c.ue_m.tx_power_ul = TX_POWER_UL_DEFAULT;
    ue_c.ue_m.cqi_period = CQI_PERIOD_DEFAULT;
    ue_c.ue_m.ri_period = RI_PERIOD_DEFAULT;
    // TRAFFIC MODEL CONFIG
    ue_c.traffic_c.type = TRAFFIC_MODEL_DEFAULT;
    ue_c.traffic_c.ul_target = UL_TARGET_DEFAULT;
    ue_c.traffic_c.dl_target = DL_TARGET_DEFAULT;
    ue_c.traffic_c.ul_traffic_file = "";
    ue_c.traffic_c.dl_traffic_file = "";
    ue_c.traffic_c.var_perc = VAR_PERCENT_DEFAULT;
    ue_c.traffic_c.pkt_size = PKT_SIZE_DEFAULT;
    // MOBILITY MODEL CONFIG
    ue_c.mobility_c.type = MOBILITY_MODEL_DEFAULT;
    ue_c.mobility_c.init_pos = pos2d(0.0, 0.0);
    ue_c.mobility_c.random_init = true;
    ue_c.mobility_c.speed = 0.0;
    ue_c.mobility_c.speed_var = 0.0;
    ue_c.mobility_c.max_distance = MAX_DISTANCE_DEFAULT;
    ue_c.mobility_c.time_target = T_TARGET_DEFAULT;
    ue_c.mobility_c.time_target_var = T_TARGET_VAR_DEFAULT;

    // METRICS
    ue_c.delay_t_metric = DELAY_METRIC_DEFAULT;
    ue_c.beta_metric = BETA_METRIC_DEFAULT;
    ue_c.delta_metric = DELTA_METRIC_DEFAULT;
    ue_c.pkt_delay_budget = PKT_DELAY_BUDGET_DEFAULT;

    // PRIORITY
    ue_c.priority = DEFAULT_UE_PRIORITY;
    ue_c.ue_m.ue_h = UE_HEIGHT_DEFAULT;
    ue_c.ue_m.o2i = O2I_DEFAULT;
    ue_c.ue_m.penetration_profile = PENETRATION_NONE;
    ue_c.ue_m.vehicle_profile = VEHICLE_STANDARD;
}

enb_config::enb_config(phy_enb_config _phy_config, pdcp_config _pdcp_ul, pdcp_config _pdcp_dl)
{
    phy_config = _phy_config;
    pdcp_ul = _pdcp_ul;
    pdcp_dl = _pdcp_dl;
}

configuration_loader::configuration_loader()
{
}

configuration_loader::configuration_loader(std::string cfg_file)
{
    load(cfg_file);
}

void configuration_loader::load(std::string cfg_file)
{
    std::ifstream infile(cfg_file);
    if (infile.good())
    {
        std::string line;
        std::string mode = "None";
        while (std::getline(infile, line))
        {
            if (std::isalpha(line.front()))
            {
                std::istringstream iss(line);
                std::string key, value;
                if (iss >> key >> value)
                {
                    if (key.back() == ':')
                    {
                        key.pop_back();
                        if (mode == "UE")
                        {
                            if (key == "random_v")
                            {
                                if (value == "true" || value == "1")
                                    random_v = true;
                                if (value == "false" || value == "0")
                                    random_v = false;
                            }
                            if (key == "ue_id")
                            {
                                LOG_INFO_I("configuration_loader::load") << "Adding UEs with config [" << value << "]: " << END();
                                ue_full_config ue_c;
                                ue_c.id = value;
                                ue_c_list.push_back(ue_c);
                                ue_c_list.back().ue_c.mobility_c.random_v = random_v;
                                ue_c_list.back().ue_c.traffic_c.random_v = random_v;
                            }
                            if (key == "n_ues")
                                ue_c_list.back().n_ues = std::stoi(value);
                            if (key == "ue_type")
                                ue_c_list.back().ue_type = std::stoi(value);
                            // QUEUE_NUM
                            if (key == "ul_queue_n")
                                ue_c_list.back().ue_c.ul_queue_n = std::stoi(value);
                            if (key == "dl_queue_n")
                                ue_c_list.back().ue_c.dl_queue_n = std::stoi(value);
                            // UE CONFIG
                            if (key == "n_antennas")
                                ue_c_list.back().ue_c.ue_m.n_antennas = std::stoi(value);
                            if (key == "alpha_ul")
                                ue_c_list.back().ue_c.ue_m.alpha_ul = std::stof(value);
                            if (key == "nominal_pusch_p0")
                                ue_c_list.back().ue_c.ue_m.nominal_pusch_p0 = std::stof(value);
                            if (key == "tx_power_ul")
                                ue_c_list.back().ue_c.ue_m.tx_power_ul = std::stof(value);
                            if (key == "ul_power_mode")
                            {
                                if (value == "fixed")
                                    ue_c_list.back().ue_c.ue_m.power_mode = ul_power_mode::fixed;
                                else if (value == "fractional")
                                    ue_c_list.back().ue_c.ue_m.power_mode = ul_power_mode::fractional;
                                else
                                    throw std::invalid_argument(
                                        "ul_power_mode must be fixed or fractional");
                            }
                            if (key == "cqi_period")
                                ue_c_list.back().ue_c.ue_m.cqi_period = std::stoi(value);
                            if (key == "ri_period")
                                ue_c_list.back().ue_c.ue_m.ri_period = std::stoi(value);
                            if (key == "scaling_factor")
                                ue_c_list.back().ue_c.ue_m.scaling_factor = std::stof(value);
                            // PRIORITY
                            if (key == "priority")
                                ue_c_list.back().ue_c.priority = std::stof(value);
                            // TRAFFIC MODEL CONFIG
                            if (key == "ul_target")
                                ue_c_list.back().ue_c.traffic_c.ul_target = std::stof(value);
                            if (key == "dl_target")
                                ue_c_list.back().ue_c.traffic_c.dl_target = std::stof(value);
                            if (key == "ul_traffic_file")
                                ue_c_list.back().ue_c.traffic_c.ul_traffic_file = value;
                            if (key == "dl_traffic_file")
                                ue_c_list.back().ue_c.traffic_c.dl_traffic_file = value;
                            if (key == "delay")
                                ue_c_list.back().ue_c.traffic_c.delay = std::stof(value);
                            if (key == "var_perc")
                                ue_c_list.back().ue_c.traffic_c.var_perc = std::stof(value);
                            if (key == "pkt_size")
                                ue_c_list.back().ue_c.traffic_c.pkt_size = std::stoi(value);
                            if (key == "traffic_type")
                                ue_c_list.back().ue_c.traffic_c.type = std::stoi(value);
                            // MOBILITY MODEL CONFIG
                            if (key == "mobility_type")
                                ue_c_list.back().ue_c.mobility_c.type = std::stof(value);
                            if (key == "pos_x")
                                ue_c_list.back().ue_c.mobility_c.init_pos._x(std::stof(value));
                            if (key == "pos_y")
                                ue_c_list.back().ue_c.mobility_c.init_pos._y(std::stof(value));
                            if (key == "random_init")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.mobility_c.random_init = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.mobility_c.random_init = false;
                            }
                            if (key == "speed")
                                ue_c_list.back().ue_c.mobility_c.speed = TOMS * std::stof(value);
                            if (key == "speed_var")
                                ue_c_list.back().ue_c.mobility_c.speed_var = TOMS * std::stof(value);
                            if (key == "max_distance")
                                ue_c_list.back().ue_c.mobility_c.max_distance = std::stof(value);
                            if (key == "time_target_var")
                                ue_c_list.back().ue_c.mobility_c.time_target_var = std::stof(value);
                            if (key == "time_target")
                                ue_c_list.back().ue_c.mobility_c.time_target = std::stof(value);
                            if (key == "delta_metric")
                                ue_c_list.back().ue_c.delta_metric = std::stof(value);
                            if (key == "delay_t_metric")
                                ue_c_list.back().ue_c.delay_t_metric = std::stof(value);
                            if (key == "beta_metric")
                            {
                                ue_c_list.back().ue_c.beta_metric = std::stof(value);
                                ue_c_list.back().ue_c.beta_metric_configured = true;
                            }
                            if (key == "pkt_delay_budget")
                                ue_c_list.back().ue_c.pkt_delay_budget = std::stof(value);
                            if (key == "l4s_dual_queue")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.l4s_c.enabled = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.l4s_c.enabled = false;
                            }
                            if (key == "l4s_target_ms")
                                ue_c_list.back().ue_c.l4s_c.target_s = std::stof(value) * 0.001f;
                            if (key == "l4s_rtt_max_ms")
                                ue_c_list.back().ue_c.l4s_c.rtt_max_s = std::stof(value) * 0.001f;
                            if (key == "l4s_min_th_us")
                                ue_c_list.back().ue_c.l4s_c.min_th_s = std::stof(value) * 0.000001f;
                            if (key == "l4s_range_us")
                                ue_c_list.back().ue_c.l4s_c.range_s = std::stof(value) * 0.000001f;
                            if (key == "l4s_k")
                                ue_c_list.back().ue_c.l4s_c.k = std::stof(value);
                            if (key == "l4s_classic_guard_ms")
                                ue_c_list.back().ue_c.l4s_c.classic_guard_s = std::stof(value) * 0.001f;
                            // SCENARIO
                            if (key == "ue_height")
                                ue_c_list.back().ue_c.ue_m.ue_h = std::stof(value);
                            if (key == "o2i")
                            {
                                LOG_WARNING_I("configuration_loader::load")
                                    << "o2i is deprecated; use ue_location_type, "
                                    << "building_penetration, and vehicle_penetration"
                                    << END();
                                ue_c_list.back().ue_c.ue_m.o2i = std::stoi(value);
                                ue_c_list.back().ue_c.ue_m.penetration_profile =
                                    ue_c_list.back().ue_c.ue_m.o2i == IN_BUILDING
                                        ? PENETRATION_LEGACY_AUTO
                                        : PENETRATION_NONE;
                            }
                            if (key == "location")
                                throw std::invalid_argument(
                                    "location is not supported; use ue_location_type");
                            if (key == "ue_location_type")
                            {
                                if (value == "outdoor")
                                    ue_c_list.back().ue_c.ue_m.o2i = OUTDOOR;
                                else if (value == "indoor")
                                    ue_c_list.back().ue_c.ue_m.o2i = IN_BUILDING;
                                else if (value == "vehicle")
                                    ue_c_list.back().ue_c.ue_m.o2i = IN_CAR;
                                else if (value == "random")
                                    ue_c_list.back().ue_c.ue_m.o2i = LOCATION_RANDOM;
                                else
                                    throw std::invalid_argument(
                                        "ue_location_type must be outdoor, indoor, vehicle, or random");
                            }
                            if (key == "building_penetration")
                            {
                                if (value == "none")
                                    ue_c_list.back().ue_c.ue_m.penetration_profile =
                                        PENETRATION_NONE;
                                else if (value == "low_loss")
                                    ue_c_list.back().ue_c.ue_m.penetration_profile =
                                        PENETRATION_LOW_LOSS;
                                else if (value == "high_loss")
                                    ue_c_list.back().ue_c.ue_m.penetration_profile =
                                        PENETRATION_HIGH_LOSS;
                                else
                                    throw std::invalid_argument(
                                        "building_penetration must be none, low_loss, or high_loss");
                            }
                            if (key == "vehicle_penetration")
                            {
                                if (value == "standard")
                                    ue_c_list.back().ue_c.ue_m.vehicle_profile =
                                        VEHICLE_STANDARD;
                                else if (value == "metallized")
                                    ue_c_list.back().ue_c.ue_m.vehicle_profile =
                                        VEHICLE_METALLIZED;
                                else
                                    throw std::invalid_argument(
                                        "vehicle_penetration must be standard or metallized");
                            }
                            // LOGGING
                            if (key == "log_freq")
                                ue_c_list.back().ue_c.log_freq = std::stoi(value);
                            if (key == "log_ue")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.log_ue = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.log_ue = false;
                            }
                            if (key == "log_traffic")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.log_traffic = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.log_traffic = false;
                            }
                            if (key == "log_mobility")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.log_mobility = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.log_mobility = false;
                            }
                            if (key == "log_quality")
                            {
                                if (value == "true" || value == "1")
                                    ue_c_list.back().ue_c.log_quality = true;
                                if (value == "false" || value == "0")
                                    ue_c_list.back().ue_c.log_quality = false;
                            }
                            LOG_INFO_I("configuration_loader::load") << "               Configuration - " << key << " = " << value << " added." << END();
                        }
                        else
                        {
                            if (mode == "Global")
                            {
                                if (key == "duration")
                                    duration = std::stof(value);
                                if (key == "period")
                                    period = std::stof(value);
                                if (key == "seed")
                                    seed = (std::uint64_t)std::stoull(value);
                                if (key == "run_id")
                                    unique_id = value;
                                if (key == "progress_log_period_s")
                                    progress_log_period_s = std::stof(value);
                                if (key == "multithreading")
                                {
                                    if (value == "true" || value == "1")
                                        multithreading = true;
                                    if (value == "false" || value == "0")
                                        multithreading = false;
                                }

                                if (key == "threads")
                                    threads = std::stoi(value);
                                if (key == "verbose")
                                {
                                    if (value == "true" || value == "1")
                                        verbose = true;
                                    if (value == "false" || value == "0")
                                        verbose = false;
                                }
                            }
                            if (mode == "Scenario")
                            {
                                // SCENARIO
                                if (key == "scenario_type")
                                {
                                    scenario_type = std::stoi(value);
                                }
                                if (key == "map_file")
                                {
                                    map_file = value;
                                    map_file_explicit = true;
                                }
                                if (key == "allow_nearest_map_fallback")
                                {
                                    if (value == "true" || value == "1")
                                        allow_nearest_map_fallback = true;
                                    else if (value == "false" || value == "0")
                                        allow_nearest_map_fallback = false;
                                    else
                                        throw std::invalid_argument(
                                            "allow_nearest_map_fallback must be true or false");
                                }
                            }
                            if (mode == "eNBConfig")
                            {
                                // ENB CONFIG
                                if (key == "modulation_m")
                                    modulation_m = std::stoi(value);
                                if (key == "target_ber")
                                    target_ber = std::stof(value);
                                if (key == "cqi_mode")
                                    cqi_mode = std::stoi(value);
                                if (key == "tx_power")
                                    tx_power = std::stof(value);
                                if (key == "eNB_gain")
                                    eNB_gain = std::stof(value);
                                if (key == "UT_gain")
                                    UT_gain = std::stoi(value);
                                if (key == "power_boost")
                                    power_boost = std::stoi(value);
                                if (key == "bandwidth")
                                    bandwidth = std::stoi(value);
                                if (key == "frequency")
                                    frequency = std::stod(value);
                            }
                            if (mode == "PDCP_RLC")
                            {
                                // PDCP CONFIG
                                if (key == "backhaul_d")
                                    backhaul_d = std::stof(value);
                                if (key == "backhaul_d_var")
                                    backhaul_d_var = std::stof(value);
                                if (key == "order_pkts")
                                {
                                    if (value == "true" || value == "1")
                                        order_pkts = true;
                                    if (value == "false" || value == "0")
                                        order_pkts = false;
                                }
                            }
                            if (mode == "MACLayer")
                            {
                                // MAC LAYER CONFIG

                                // METRIC CONFIG
                                if (key == "metric_type")
                                    metric_type = std::stoi(value);
                                if (key == "pf_alpha")
                                {
                                    pf_alpha = std::stof(value);
                                    pf_alpha_configured = true;
                                }
                                if (key == "pf_time_window_ms"
                                    || key == "pf_intra_tti_update")
                                    throw std::invalid_argument(
                                        key
                                        + " was removed; use the corresponding "
                                          "throughput_* key");
                                if (key == "throughput_time_window_ms")
                                    throughput_time_window_ms =
                                        std::stof(value);
                                if (key == "throughput_intra_tti_update")
                                {
                                    if (value == "none")
                                        throughput_intra_tti_update = false;
                                    else if (value == "allocation_unit")
                                        throughput_intra_tti_update = true;
                                    else
                                        throw std::invalid_argument(
                                            "throughput_intra_tti_update must "
                                            "be none or allocation_unit");
                                }

                                // LOG DATA CONFIG
                                if (key == "log_freq")
                                    log_freq = std::stoi(value);
                                if (key == "log_mac")
                                {
                                    if (value == "true" || value == "1")
                                        log_mac = true;
                                    if (value == "false" || value == "0")
                                        log_mac = false;
                                }
                                if (key == "mimo_layers")
                                    mimo_layers = std::stoi(value);
                                if (key == "numerology")
                                    numerology = std::stoi(value);
                                if (key == "n_ofdm_syms")
                                    n_ofdm_syms = std::stoi(value);
                                if (key == "n_re_freq")
                                    n_re_freq = std::stoi(value);
                                if (key == "max_rtx_ul")
                                    max_rtx_ul = std::stoi(value);
                                if (key == "max_rtx_dl")
                                    max_rtx_dl = std::stoi(value);
                                if (key == "harq_model")
                                {
                                    if (value == "legacy_bler")
                                        harq_model_value =
                                            harq_model::legacy_bler;
                                    else if (value == "disabled")
                                        harq_model_value =
                                            harq_model::disabled;
                                    else
                                        throw std::invalid_argument(
                                            "harq_model must be legacy_bler "
                                            "or disabled");
                                }
                                if (key == "mcs_tables")
                                {
                                    if (value == "true" || value == "1")
                                        mcs_tables = true;
                                    if (value == "false" || value == "0")
                                        mcs_tables = false;
                                }
                                if (key == "scheduling_mode")
                                    scheduling_mode = std::stoi(value);
                                if (key == "scheduling_type")
                                    scheduling_type = std::stoi(value);
                                if (key == "scheduling_config")
                                    scheduling_config = std::stoi(value);
                                if (key == "duplexing_type")
                                    duplexing_type = std::stoi(value);
                                if (key == "n_dl_slots")
                                    n_dl_slots = std::stoi(value);
                                if (key == "n_ul_slots")
                                    n_ul_slots = std::stoi(value);
                                if (key == "transition_c")
                                    transition_c = std::stoi(value);
                                if (key == "ratio_DL_UL")
                                    ratio_DL_UL = std::stof(value);
                            }
                            if (mode == "Control")
                            {
                                if (key == "enabled")
                                {
                                    if (value == "true" || value == "1") control_c.enabled = true;
                                    else if (value == "false" || value == "0") control_c.enabled = false;
                                    else throw std::invalid_argument(
                                        "[Control] enabled must be true or false");
                                }
                                if (key == "transport") control_c.transport = value;
                                if (key == "address") control_c.address = value;
                                if (key == "port") control_c.port = std::stoi(value);
                                if (key == "sync_mode") control_c.sync_mode = value;
                                if (key == "credit_timeout_ms") control_c.credit_timeout_ms = std::stoi(value);
                                if (key == "on_timeout") control_c.on_timeout = value;
                                if (key == "on_peer_loss") control_c.on_peer_loss = value;
                                if (key == "timeline_file") control_c.timeline_file = value;
                                if (key == "journal_file") control_c.journal_file = value;
                                if (key == "max_cmds_per_tick") control_c.max_cmds_per_tick = std::stoi(value);
                                if (key == "max_object_events") control_c.max_object_events = std::stoi(value);
                            }
                            if (mode == "Monitoring")
                            {
                                if (key == "enabled")
                                {
                                    if (value == "true" || value == "1") monitoring_c.enabled = true;
                                    if (value == "false" || value == "0") monitoring_c.enabled = false;
                                }
                                if (key == "aggregation_window_ms") monitoring_c.aggregation_window_ms = std::stoi(value);
                                if (key == "default_metric_type") monitoring_c.default_metric_type = value;
                                if (key == "emit_ue_phy")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_ue_phy = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_ue_phy = false;
                                }
                                if (key == "emit_ue_pdcp")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_ue_pdcp = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_ue_pdcp = false;
                                }
                                if (key == "emit_ue_queue")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_ue_queue = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_ue_queue = false;
                                }
                                if (key == "emit_ue_mobility")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_ue_mobility = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_ue_mobility = false;
                                }
                                if (key == "emit_l4s")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_l4s = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_l4s = false;
                                }
                                if (key == "emit_mac_scheduler")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_mac_scheduler = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_mac_scheduler = false;
                                }
                                if (key == "emit_emulator_runtime")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_emulator_runtime = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_emulator_runtime = false;
                                }
                                if (key == "emit_control")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_control = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_control = false;
                                }
                                if (key == "emit_runtime_debug_logs")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_runtime_debug_logs = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_runtime_debug_logs = false;
                                }
                                if (key == "emit_text_logs_compat")
                                {
                                    if (value == "true" || value == "1") monitoring_c.emit_text_logs_compat = true;
                                    if (value == "false" || value == "0") monitoring_c.emit_text_logs_compat = false;
                                }
                                if (key == "outputs")
                                {
                                    monitoring_c.outputs.clear();
                                    std::istringstream ss(value);
                                    std::string token;
                                    while(std::getline(ss, token, ','))
                                    {
                                        monitoring_output_config oc;
                                        oc.name = token;
                                        monitoring_c.outputs.push_back(oc);
                                    }
                                }
                                if (key.rfind("output_",0) == 0)
                                {
                                    const std::string prefix = "output_";
                                    const std::string type_suffix = "_type";
                                    const std::string address_suffix = "_address";
                                    const std::string port_suffix = "_port";
                                    std::string name;
                                    std::string prop;

                                    if(key.size() > prefix.size() + type_suffix.size() &&
                                       key.compare(key.size() - type_suffix.size(), type_suffix.size(), type_suffix) == 0)
                                    {
                                        name = key.substr(prefix.size(), key.size() - prefix.size() - type_suffix.size());
                                        prop = "type";
                                    }
                                    else if(key.size() > prefix.size() + address_suffix.size() &&
                                            key.compare(key.size() - address_suffix.size(), address_suffix.size(), address_suffix) == 0)
                                    {
                                        name = key.substr(prefix.size(), key.size() - prefix.size() - address_suffix.size());
                                        prop = "address";
                                    }
                                    else if(key.size() > prefix.size() + port_suffix.size() &&
                                            key.compare(key.size() - port_suffix.size(), port_suffix.size(), port_suffix) == 0)
                                    {
                                        name = key.substr(prefix.size(), key.size() - prefix.size() - port_suffix.size());
                                        prop = "port";
                                    }

                                    if(!name.empty())
                                    {
                                        for(auto &oc : monitoring_c.outputs)
                                        {
                                            if(oc.name == name)
                                            {
                                                if(prop == "type") oc.type = value;
                                                else if(prop == "address") oc.address = value;
                                                else if(prop == "port") oc.port = std::stoi(value);
                                            }
                                        }
                                    }
                                }
                            }
                            if (mode == "PHYLayer")
                            {
                                // NOISE INTERFERENCE
                                if (key == "interference_ues")
                                    interference_ues = std::stoi(value);
                                if (key == "interference_eNBs")
                                    interference_eNBs = std::stoi(value);
                                if (key == "distance_interference")
                                    distance_interference = std::stof(value);
                                if (key == "interfered_bandwidth_ratio")
                                    interfered_bandwidth_ratio = std::stof(value);
                                if (key == "thermal_noise")
                                    thermal_noise = std::stof(value);
                                if (key == "enb_noise_figure")
                                    enb_noise_figure = std::stoi(value);
                                if (key == "ut_noise_figure")
                                    ut_noise_figure = std::stoi(value);
                                if (key == "air_delay_var_ul")
                                    air_delay_var_ul = std::stof(value);
                                if (key == "rtx_period_ul")
                                    rtx_period_ul = std::stof(value);
                                if (key == "rtx_period_var_ul")
                                    rtx_period_var_ul = std::stof(value);
                                if (key == "rtx_proc_delay_ul")
                                    rtx_proc_delay_ul = std::stof(value);
                                if (key == "rtx_proc_delay_var_ul")
                                    rtx_proc_delay_var_ul = std::stof(value);
                                if (key == "air_delay_var_dl")
                                    air_delay_var_dl = std::stof(value);
                                if (key == "rtx_period_dl")
                                    rtx_period_dl = std::stof(value);
                                if (key == "rtx_period_var_dl")
                                    rtx_period_var_dl = std::stof(value);
                                if (key == "rtx_proc_delay_dl")
                                    rtx_proc_delay_dl = std::stof(value);
                                if (key == "rtx_proc_delay_var_dl")
                                    rtx_proc_delay_var_dl = std::stof(value);
                            }
                            LOG_INFO_I("configuration_loader::load") << " Configuration - " << key << " = " << value << " added." << END();
                        }
                    }
                    else
                    {
                        LOG_WARNING_I("configuration_loader::load") << " Wrong file format for key [" << key << "] with \':\' missing" << END();
                    }
                }
            }
            else if (line.front() == '[')
            {
                mode = line;
                mode.erase(0, 1).pop_back();
            }
        }
    }
    else
    {
        LOG_WARNING_I("configuration_loader::load") << "File doesn't exit...falling back to defaults" << END();
    }

    // Resolve the map only after all sections have been parsed. This removes the
    // historical dependency on Scenario appearing before eNBConfig.
    if (!map_file_explicit)
        map_file = getClosestMapFile(scenario_type, frequency);
    else
        LOG_INFO_I("configuration_loader::load")
            << "Selected explicit map " << map_file << END();
    if (map_file.empty())
        throw std::invalid_argument(
            "no map is available for the configured scenario and frequency");

    // Once the whole file has been read, so that run_id decides the name no matter
    // where it appears in it.
    for (ue_full_config &ue_c : ue_c_list)
        ue_c.ue_c.log_id = get_unique_id();

    std::set<int> captured_queues;
    for (const ue_full_config &ue : ue_c_list)
    {
        if (ue.ue_type != REAL_UE)
            continue;
        const int ul_queue = ue.ue_c.ul_queue_n;
        const int dl_queue = ue.ue_c.dl_queue_n;
        const std::string label = ue.id.empty() ? "<unnamed>" : ue.id;
        if (ul_queue < 0 || dl_queue < 0)
            throw std::invalid_argument(
                "captured UE " + label + " has a negative NFQUEUE id");
        if (ul_queue == dl_queue)
            throw std::invalid_argument(
                "captured UE " + label
                + " must use different UL and DL NFQUEUE ids");
        if (!captured_queues.insert(ul_queue).second)
            throw std::invalid_argument(
                "captured UE " + label + " reuses NFQUEUE "
                + std::to_string(ul_queue));
        if (!captured_queues.insert(dl_queue).second)
            throw std::invalid_argument(
                "captured UE " + label + " reuses NFQUEUE "
                + std::to_string(dl_queue));
    }

    const bool uses_throughput_history =
        metric_type == METRIC_PF || metric_type == METRIC_BET;
    if (metric_type != METRIC_PF && pf_alpha_configured)
        throw std::invalid_argument(
            "pf_alpha is only valid for metric_type: 6 (PF)");
    if (uses_throughput_history)
    {
        if (throughput_time_window_ms <= 0.0f)
            throw std::invalid_argument(
                "PF and BET require throughput_time_window_ms > 0");
        if (metric_type == METRIC_PF && pf_alpha < 0.0f)
            throw std::invalid_argument("PF requires pf_alpha >= 0");
        for (const ue_full_config &ue_c : ue_c_list)
        {
            if (ue_c.ue_c.beta_metric_configured)
                throw std::invalid_argument(
                    "beta_metric is not valid for PF or BET; use "
                    "throughput_time_window_ms and keep UE priority as the "
                    "per-UE weight");
        }
    }
    else
    {
        if (throughput_intra_tti_update)
            throw std::invalid_argument(
                "throughput_intra_tti_update: allocation_unit is only valid "
                "for PF and BET");
        for (const ue_full_config &ue_c : ue_c_list)
        {
            if (ue_c.ue_c.beta_metric_configured)
                LOG_WARNING_I("configuration_loader::load")
                    << "beta_metric is ignored by metric_type "
                    << metric_type << END();
        }
    }

    const auto require_nonnegative_finite =
        [](const char *name, float value) {
            if (!std::isfinite(value) || value < 0.0f)
                throw std::invalid_argument(
                    std::string(name)
                    + " must be finite and non-negative seconds");
        };
    if (max_rtx_ul < 0 || max_rtx_dl < 0)
        throw std::invalid_argument("max_rtx_ul and max_rtx_dl must be >= 0");
    require_nonnegative_finite("backhaul_d", backhaul_d);
    require_nonnegative_finite("backhaul_d_var", backhaul_d_var);
    require_nonnegative_finite("air_delay_var_ul", air_delay_var_ul);
    require_nonnegative_finite("rtx_period_ul", rtx_period_ul);
    require_nonnegative_finite("rtx_period_var_ul", rtx_period_var_ul);
    require_nonnegative_finite("rtx_proc_delay_ul", rtx_proc_delay_ul);
    require_nonnegative_finite(
        "rtx_proc_delay_var_ul", rtx_proc_delay_var_ul);
    require_nonnegative_finite("air_delay_var_dl", air_delay_var_dl);
    require_nonnegative_finite("rtx_period_dl", rtx_period_dl);
    require_nonnegative_finite("rtx_period_var_dl", rtx_period_var_dl);
    require_nonnegative_finite("rtx_proc_delay_dl", rtx_proc_delay_dl);
    require_nonnegative_finite(
        "rtx_proc_delay_var_dl", rtx_proc_delay_var_dl);
    if (harq_model_value == harq_model::legacy_bler && !mcs_tables)
        throw std::invalid_argument(
            "harq_model: legacy_bler requires mcs_tables: true");
}

float configuration_loader::get_period()
{
    return period;
}

std::uint64_t configuration_loader::get_seed()
{
    return seed;
}

float configuration_loader::get_duration()
{
    return duration;
}

float configuration_loader::get_progress_log_period_s()
{
    return progress_log_period_s;
}

std::list<ue_full_config> configuration_loader::get_ue_c_list()
{
    return ue_c_list;
}

phy_enb_config configuration_loader::get_phy_enb_config()
{
    return phy_enb_config(tx_power, modulation_m, target_ber,
                          cqi_mode, frequency, bandwidth, mimo_layers,
                          metric_type, interference_ues, interference_eNBs, distance_interference, interfered_bandwidth_ratio,
                          thermal_noise, enb_noise_figure, ut_noise_figure, eNB_gain, UT_gain, power_boost, numerology,
                          pf_alpha, throughput_time_window_ms,
                          throughput_intra_tti_update);
}

pdcp_config configuration_loader::get_pdcp_config_ul()
{
    return pdcp_config(
        max_rtx_ul,
        air_delay_var_ul,
        rtx_period_ul,
        rtx_period_var_ul,
        rtx_proc_delay_ul,
        rtx_proc_delay_var_ul,
        backhaul_d,
        backhaul_d_var,
        order_pkts,
        harq_model_value);
}

pdcp_config configuration_loader::get_pdcp_config_dl()
{
    return pdcp_config(
        max_rtx_dl,
        air_delay_var_dl,
        rtx_period_dl,
        rtx_period_var_dl,
        rtx_proc_delay_dl,
        rtx_proc_delay_var_dl,
        backhaul_d,
        backhaul_d_var,
        order_pkts,
        harq_model_value);
}

enb_config configuration_loader::get_enb_config()
{
    return enb_config(get_phy_enb_config(), get_pdcp_config_ul(), get_pdcp_config_dl());
}

scenario_config configuration_loader::get_scenario_config()
{
    return scenario_config(scenario_type, street_width, antenna_height, enb_height, map_file, mcs_tables);
}

int configuration_loader::get_threads()
{
    return threads;
}

bool configuration_loader::get_ue_threading()
{
    return threads > 0;
}

bool configuration_loader::get_threading()
{
    return multithreading;
}

mac_config configuration_loader::get_mac_config()
{
    if (
        duplexing_type == FDD
        && std::fabs(ratio_DL_UL - 0.5f) > 1e-6f)
    {
        throw std::invalid_argument(
            "asymmetric FDD bandwidth is not supported; "
            "use ratio_DL_UL: 0.5 or TDD");
    }
    return mac_config(mimo_layers, numerology, n_re_freq, n_ofdm_syms, bandwidth, scheduling_mode,
                      scheduling_type, scheduling_config, metric_type,
                      duplexing_type, ratio_DL_UL, pf_alpha,
                      throughput_time_window_ms,
                      throughput_intra_tti_update);
}

tdd_config configuration_loader::get_tdd_config()
{
    return tdd_config(n_dl_slots, n_ul_slots, transition_c);
}

std::string configuration_loader::get_unique_id()
{
    // Stored on first use: the UE logs and the MAC logs ask for it at different
    // moments, and a fresh timestamp each time would split one run across two
    // directories whenever the calls fall either side of a second.
    if (unique_id == "")
    {
        time_t now = std::time(0);
        tm *ltm = std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(ltm, "%Y_%m_%d_%H_%M_%S");
        unique_id = oss.str();
    }

    return unique_id;
}

log_config configuration_loader::get_log_config()
{
    int verbosity = 0;
    if (verbose && log_mac)
        verbosity = 2;
    if (!verbose && log_mac)
        verbosity = 1;
    if (!verbose && !log_mac)
        verbosity = 0;
    return log_config(log_mac, log_freq, get_unique_id(), verbosity);
}

bool configuration_loader::get_stochastics()
{
    return random_v;
}

int configuration_loader::get_log_freq()
{
    return log_freq;
}

bool configuration_loader::get_log_mac()
{
    return log_mac;
}

float configuration_loader::get_freq()
{
    return frequency;
}

std::string configuration_loader::getClosestMapFile(int scenario_type, double frequency)
{
    constexpr double frequency_tolerance_ghz = 1e-4;
    double freqGHz = frequency / 1e9;

    auto scenarioIt = SCENARIO_TYPE_MAP.find(scenario_type);
    if (scenarioIt == SCENARIO_TYPE_MAP.end())
    {
        LOG_ERROR_I("configuration_loader::getClosestMapFile")
            << "Unknown scenario_type: " << scenario_type << END();
        return "";
    }

    const std::string &scenarioName = scenarioIt->second;

    auto freqIt = AVAILABLE_FREQUENCIES.find(scenarioName);
    if (freqIt == AVAILABLE_FREQUENCIES.end())
    {
        LOG_ERROR_I("configuration_loader::getClosestMapFile")
            << "No frequencies available for scenario: " << scenarioName << END();
        return "";
    }

    const std::vector<double> &scenarioFrequencies = freqIt->second;

    auto closestFreq = *std::min_element(
        scenarioFrequencies.begin(), scenarioFrequencies.end(),
        [freqGHz](double a, double b)
        {
            return std::abs(a - freqGHz) < std::abs(b - freqGHz);
        });

    std::ostringstream freqStream;
    freqStream << std::fixed << std::setprecision(2) << closestFreq;
    std::string freqStr = freqStream.str();

    if (freqStr.find('.') != std::string::npos)
    {
        freqStr.erase(freqStr.find_last_not_of('0') + 1, std::string::npos);
        if (freqStr.back() == '.')
        {
            freqStr.pop_back();
        }
    }

    std::string mapFilePath = getBaseMapPath() + scenarioName + "_" + freqStr + ".json";
    if (std::abs(closestFreq - freqGHz) > frequency_tolerance_ghz)
    {
        if (!allow_nearest_map_fallback)
        {
            throw std::invalid_argument(
                "no exact map for requested scenario/frequency; "
                "set allow_nearest_map_fallback: true or map_file explicitly");
        }
        LOG_WARNING_I("configuration_loader::getClosestMapFile")
            << "Requested " << freqGHz << " GHz for " << scenarioName
            << "; selected nearest map at " << closestFreq << " GHz" << END();
    }
    LOG_INFO_I("configuration_loader::getClosestMapFile")
        << "Selected map " << mapFilePath << END();

    return mapFilePath;
}

monitoring_config configuration_loader::get_monitoring_config()
{
    return monitoring_c;
}

control_config configuration_loader::get_control_config()
{
    return control_c;
}
