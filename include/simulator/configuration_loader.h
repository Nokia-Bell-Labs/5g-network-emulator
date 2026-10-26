/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/

#ifndef CONFIGURATION_LOADER_H
#define CONFIGURATION_LOADER

#include <list>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

#include <mac_layer/mac_definitions.h>
#include <phy_layer/phy_l_definitions.h>
#include <mobility_models/mobility_config.h>
#include <traffic_models/traffic_config.h>
#include <pdcp_layer/pdcp_config.h>
#include <ue/ue_config.h>
#include <mac_layer/mac_config.h>
#include <phy_layer/phy_config.h>
#include <mac_layer/tdd_handler.h>
#include <utils/logging/log_handler.h>
#include <utils/control/control_config.h>
#include <utils/monitoring/monitoring_config.h>

std::string getBaseMapPath();

#define DURATION_DEFAULT 10.0f
#define PERIOD_DEFAULT 1.0f
#define PROGRESS_LOG_PERIOD_S_DEFAULT 10.0f
//
// MAC LAYER
//

// SCHEDULER DEFINITIONS
#define DEFAULT_METRIC SCH_METRIC_DEFAULT // Round Robin
#define DELTA_METRIC_DEFAULT 1.0
#define DELAY_METRIC_DEFAULT 0.1
#define BETA_METRIC_DEFAULT 0.5
#define PKT_DELAY_BUDGET_DEFAULT 0.35f
#define PF_ALPHA_DEFAULT 1.0f
#define THROUGHPUT_TIME_WINDOW_MS_DEFAULT 100.0f

// UE PRIORITY
#define DEFAULT_UE_PRIORITY 1

// DEFAULT TDD
#define DFLT_CONFIG 45 // Balanced between UL/DL

//
// MOBILITY MODELS
//

#define MOBILITY_MODEL_DEFAULT STATIC_MODEL // STATIC MODEL
#define MAX_DISTANCE_DEFAULT 1000           // METERS
#define T_TARGET_DEFAULT 10                 // S
#define T_TARGET_VAR_DEFAULT 0              //

//
// PDCP LAYER
//

#define BACKHAUL_D_DEFAULT 0.005
#define BACKHAUL_D_VAR_DEFAULT 0.001
#define DEFAULT_ORDER_PKTS true
#define MAX_RTX_DEFAULT 4
#define AIR_DELAY_VAR_DEFAULT 0.0f
#define RTX_PERIOD_DEFAULT 0.004f
#define RTX_PERIOD_VAR_DEFAULT 0.0f
#define RTX_PROC_DELAY_DEFAULT 0.001f
#define RTX_PROC_DELAY_VAR_DEFAULT 0.001f

//
// SCENARIO
//

#define SCENARIO_TYPE_DEFAULT 0
#define STREET_WIDTH_DEFAULT 10
#define ANTENNA_HEIGHT_DEFAULT 10
#define ENB_HEIGHT_DEFAULT 15
#define MCS_TABLES_DEFAULT true
#define MAP_FILE_DEFAULT "";
#define O2I_DEFAULT 10

//
// MAC LAYER
//

#define NUMEROLOGY_DEFAULT SCH_NUMEROLOGY_DEFAULT
#define N_RE_DEFAULT SCH_N_RE_DEFAULT
#define N_OFDM_DEFAULT SCH_N_OFDM_DEFAULT
#define BANDWIDTH_DEFAULT 75000000
#define FREQUENCY_DEFAULT 3500000000
#define OFDM_SYMBOLS_DEFAULT 14
#define SCHEDULING_MODE_DEFAULT SCH_LOCALIZED_MODE
#define SCHEDULING_TYPE_DEFAULT 0
#define SCHEDULING_CONFIG_DEFAULT 1
#define DUPLEXING_TYPE_DEFAULT FDD
#define N_DL_SLOTS_DEFAULT 1
#define N_UL_SLOTS_DEFAULT 1
#define N_TRANSITION_C_DEFAULT DFLT_CONFIG
#define RATIO_DL_UL 0.5
//
// PHY LAYER
//

#define MIMO_LAYERS_DEFAULT 1
#define ENB_TX_POWER_DEFAULT 40.0
#define MODULATION_DEFAULT MODULATION_256
#define TARGET_BER_DEFAULT 0.00005
#define CQI_MODE_DEFAULT CQI_WIDEBAND
#define CQI_PERIOD_DEFAULT 5
#define ENB_ANTENNAS_DEFAULT 4

//
// INTERFERENCE & NOISE
//
#define INTERF_UES_DEFAULT 10
#define INTERF_ENBS_DEFAULT 2
#define INTERF_DISTANCE_DEFAULT 1500
#define INTERF_RATIO_DEFAULT 0.1
#define THERMAL_N_DEFAULT -174
#define ENB_NOISE_FIGURE_DEFAULT 1
#define UT_NOISE_FIGURE_DEFAULT 10
#define ENB_GAIN_DEFAULT 18
#define UT_GAIN_DEFAULT 0
#define POWER_BOOST_DEFAULT 0

// SINR CONFIG
#define RI_PERIOD_DEFAULT 5

//
// TRAFFIC MODEL
//

#define TRAFFIC_MODEL_DEFAULT CONSTANT_TRAFFIC_MODEL

#define UL_TARGET_DEFAULT 10.0
#define DL_TARGET_DEFAULT 50.0

#define VAR_PERCENT_DEFAULT 50.0
#define PKT_SIZE_DEFAULT 500

//
// UE
//
#define ALPHA_UL_DEFAULT 0.7
#define NOMINAL_PUSCH_P0_DEFAULT -60
#define TX_POWER_UL_DEFAULT 23
#define N_UES_DEFAULT 10
#define SIM_UE 1
#define REAL_UE 0

#define N_ANTENNAS_DEFAULT 1

#define UE_HEIGHT_DEFAULT 1.5

#define TOMS 1000.0f / 3600.0f

//
// UE CONFIG
//
struct ue_full_config
{
    ue_full_config();

    int n_ues = N_UES_DEFAULT;
    int ue_type = SIM_UE;
    std::string id;
    ue_config ue_c;
};

//
// GENERAL ENB CONFIG
//
struct enb_config
{
    enb_config(phy_enb_config _phy_config, pdcp_config _pdcp_ul, pdcp_config _pdcp_dl);

    phy_enb_config phy_config;
    pdcp_config pdcp_ul;
    pdcp_config pdcp_dl;
};

class configuration_loader
{
public:
    configuration_loader();
    configuration_loader(std::string cfg_file);

    void load(std::string cfg_file);

    float get_period();
    float get_duration();
    std::uint64_t get_seed();
    float get_progress_log_period_s();
    std::list<ue_full_config> get_ue_c_list();
    phy_enb_config get_phy_enb_config();
    pdcp_config get_pdcp_config_ul();
    pdcp_config get_pdcp_config_dl();
    enb_config get_enb_config();
    scenario_config get_scenario_config();
    int get_threads();
    bool get_ue_threading();
    bool get_threading();
    mac_config get_mac_config();
    tdd_config get_tdd_config();
    std::string get_unique_id();
    log_config get_log_config();
    bool get_stochastics();
    int get_log_freq();
    bool get_log_mac();
    float get_freq();
    std::string getClosestMapFile(int scenario_type, double frequency);
    // monitoring config
    monitoring_config get_monitoring_config();

    // runtime control config
    control_config get_control_config();

private:
    float duration = DURATION_DEFAULT;
    float period = PERIOD_DEFAULT;
    // Run seed. 0 means "not set": random_v then decides between the wall clock and a
    // reproducible run. See utils/rng_seed.h.
    std::uint64_t seed = 0;
    float progress_log_period_s = PROGRESS_LOG_PERIOD_S_DEFAULT;
    // UE CONFIG
    std::list<ue_full_config> ue_c_list;
    // ENB CONFIG
    float tx_power = ENB_TX_POWER_DEFAULT;
    float alpha_ul = ALPHA_UL_DEFAULT;
    float nominal_pusch_p0 = NOMINAL_PUSCH_P0_DEFAULT;
    float tx_power_ul = TX_POWER_UL_DEFAULT;
    int modulation_m = MODULATION_DEFAULT;
    float target_ber = TARGET_BER_DEFAULT;
    int cqi_mode = CQI_MODE_DEFAULT;
    int interference_ues = INTERF_UES_DEFAULT;
    int interference_eNBs = INTERF_ENBS_DEFAULT;
    float distance_interference = INTERF_DISTANCE_DEFAULT;
    float interfered_bandwidth_ratio = INTERF_RATIO_DEFAULT;
    float thermal_noise = THERMAL_N_DEFAULT;
    float enb_noise_figure = ENB_NOISE_FIGURE_DEFAULT;
    float ut_noise_figure = UT_NOISE_FIGURE_DEFAULT;
    float eNB_gain = ENB_GAIN_DEFAULT;
    float UT_gain = UT_GAIN_DEFAULT;
    float power_boost = POWER_BOOST_DEFAULT;
    // PDCP_CONFIG
    float backhaul_d = BACKHAUL_D_DEFAULT;
    float backhaul_d_var = BACKHAUL_D_VAR_DEFAULT;
    bool order_pkts = DEFAULT_ORDER_PKTS;
    harq_model harq_model_value = harq_model::legacy_bler;
    // PDCP UL CONFIG
    int max_rtx_ul = MAX_RTX_DEFAULT;
    float air_delay_var_ul = AIR_DELAY_VAR_DEFAULT;
    float rtx_period_ul = RTX_PERIOD_DEFAULT;
    float rtx_period_var_ul = RTX_PERIOD_VAR_DEFAULT;
    float rtx_proc_delay_ul = RTX_PROC_DELAY_DEFAULT;
    float rtx_proc_delay_var_ul = RTX_PROC_DELAY_VAR_DEFAULT;
    // PDCP DL CONFIG
    int max_rtx_dl = MAX_RTX_DEFAULT;
    float air_delay_var_dl = AIR_DELAY_VAR_DEFAULT;
    float rtx_period_dl = RTX_PERIOD_DEFAULT;
    float rtx_period_var_dl = RTX_PERIOD_VAR_DEFAULT;

    // Monitoring
    monitoring_config monitoring_c;
    control_config control_c;
    float rtx_proc_delay_dl = RTX_PROC_DELAY_DEFAULT;
    float rtx_proc_delay_var_dl = RTX_PROC_DELAY_VAR_DEFAULT;
    // METRIC COFIGURATION
    int metric_type = DEFAULT_METRIC;
    float pf_alpha = PF_ALPHA_DEFAULT;
    bool pf_alpha_configured = false;
    float throughput_time_window_ms = THROUGHPUT_TIME_WINDOW_MS_DEFAULT;
    bool throughput_intra_tti_update = false;
    // LOG DATA
    int log_freq = -1;
    bool log_mac = false;
    bool verbose = false;
    bool random_v = false; // to enable stochastics
    // SCENARIO
    int scenario_type = SCENARIO_TYPE_DEFAULT;
    float street_width = STREET_WIDTH_DEFAULT;
    float antenna_height = ANTENNA_HEIGHT_DEFAULT;
    float enb_height = ENB_HEIGHT_DEFAULT;
    std::string map_path = getBaseMapPath() + "urban.txt";
    const std::map<int, std::string> SCENARIO_TYPE_MAP = {
        {0, "URBAN_MICROCELL"},
        {1, "URBAN_MACROCELL"},
        {2, "RURAL_MACROCELL"},
        {3, "INDOOR_OPEN_OFFICE"},
        {4, "INDOOR_MIXED_OFFICE"},
        {5, "INDOOR_SHOPPING_MALL"}};
    const std::map<std::string, std::vector<double>> AVAILABLE_FREQUENCIES = {
        {"URBAN_MICROCELL", {2.38, 3.5, 4.9, 26, 28}},
        {"URBAN_MACROCELL", {3.5, 4.9, 26, 28}},
        {"RURAL_MACROCELL", {0.7, 0.8, 3.5}},
        {"INDOOR_OPEN_OFFICE", {3.5, 26, 28}},
        {"INDOOR_MIXED_OFFICE", {3.5, 26, 28}},
        {"INDOOR_SHOPPING_MALL", {3.5, 26, 60}}};

    bool mcs_tables = MCS_TABLES_DEFAULT;
    // MAP
    std::string map_file = MAP_FILE_DEFAULT;
    bool map_file_explicit = false;
    bool allow_nearest_map_fallback = false;
    // MAC LAYER
    int mimo_layers = MIMO_LAYERS_DEFAULT;
    int ofdm_symbols = OFDM_SYMBOLS_DEFAULT;
    int numerology = NUMEROLOGY_DEFAULT;
    int n_ofdm_syms = N_OFDM_DEFAULT;
    int n_re_freq = N_RE_DEFAULT;
    int bandwidth = BANDWIDTH_DEFAULT;
    float frequency = FREQUENCY_DEFAULT;
    int scheduling_mode = SCHEDULING_MODE_DEFAULT;
    int scheduling_type = SCHEDULING_TYPE_DEFAULT;
    int scheduling_config = SCHEDULING_CONFIG_DEFAULT;
    int duplexing_type = DUPLEXING_TYPE_DEFAULT;
    int n_dl_slots = N_DL_SLOTS_DEFAULT;
    int n_ul_slots = N_UL_SLOTS_DEFAULT;
    int transition_c = N_TRANSITION_C_DEFAULT;
    float ratio_DL_UL = RATIO_DL_UL;
    std::string unique_id = "";
    // GENERAL
    int threads = 0;
    bool multithreading = true;
};

#endif
