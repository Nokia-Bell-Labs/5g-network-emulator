/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/

#ifndef PHY_LAYER_H
#define PHY_LAYER_H

#include <algorithm>
#include <iostream>
#include <random>
#include <vector>
#include <chrono>
#include <phy_layer/phy_l_definitions.h>
#include <phy_layer/phy_config.h>
#include <phy_shared/phy_shared.h>
#include <mac_layer/metric_handler.h>
#include <mac_layer/throughput_state.h>
#include <mobility_models/pos2d.h>
#include <utils/conversions.h>
#include <utils/terminal_logging.h>

// Logging

#include "utils/logging/log_handler.h"

double linearToDBm(double linear);
double linearToDb(double linear);
double dBmToLinear(double db);
double dBToLinear(double db);

//--------------------------------------------------------------------------------------------------
// phy_layer(): The PHY layer models the Channel Quality Measurements: SINR, RSRP, MCS, CQI,
// HARQ ACK/NACK. These metrics, besides of being logged for later analysis, are used by the MAC layer
// for resource allocation decisions, and to determine what happens with each data block. This info
// is used to also determine the probability of a packet to be retransmitted, according to the HARQ model.
// The implemented models are taken from 3GPP 38.901 specifications.
// Input:
//      _tx: transmission direction of the current instance (DL/UL)
//      _id: User internal ID
//      _scenario_config: scenario configuration struct
//              *type: scenario type ID from the ones implemented.
//              *w: width of the beam.
//              *antenna_h: height of the antenna.
//              *eNB_h: height of the eNB/gNB.
//              *mcs_tables: enable/disable custom MCS-BLER-SINR curves for estimating the MCS indexes.
//      _phy_ue_config: UE PHY layer configuration struct.
//              *alpha_ul: factor controlling the UE uplink power adjustment based on path loss.
//              *nominal_pusch_p0: target received power for the UE in the PUSCH. 
//              *_cqi_period: period in ms between CQI estimations.
//              *_ri_period: period in ms between Rank Indicator estimations.
//              *_max_ri: maximum Rank Indicator value for current UE.
//              *_ue_h: height of UE above ground.
//              *_priority: UE's priority if user prioritization is used.
//              *_delay_t: used in specific metric for resource allocation scheduling.
//              *_delta: used in specific metric for resource allocation scheduling.
//              *_max_speed: maximum UE's speed.
//              *_scaling_factor: defined scaling factor used for throughput calculation.
//      _phy_enb_config: PHY Layer eNB/gNB configuration struct.
//              *tx_power: transmission power of the eNB/gNB
//              *modulation_m: 0 - 64 QAM | 1 - 256 QAM
//              *target_ber: used for MCS indexes estimation when the MCS tables are not used.
//              *cqi_mode: 0 - Wideband | 1 - Subband
//              *frequency: base frequency in which the eNB/gNB is operating.
//              *bandwidth: total available bandwidth.
//              *mimo_l: number of available eNB/gNB MIMO layers.
//              *metric_type: ID of the metric type.
//              *n_int_ues: dumber of interfering UEs, not attached to the simulated eNB/gNB
//              *d_interference: interference distance of nearby UEs
//              *thermal_noise: modeled thermanl noise of the emulated eNB/gNB.
//              *figure_noise: modeled noise figure of the emulated eNB/gNB.
//              *eNB_gain: modeled  gain of the emulated eNB.
//              *UT_gain: modeled  gain of the emulated UT.
//      verbosity: Enable/disable verbosity
//--------------------------------------------------------------------------------------------------
class phy_layer
{

public:
    phy_layer(int _tx, int _id, scenario_config _scenario_config, phy_ue_config _phy_ue_config, phy_enb_config _phy_enb_config, bool _stochastics = true, int _verbosity = 0);

public:
    void init(
        int _n_rbs,
        int _bandwidth,
        int _carrier_prbs);
    int get_cqi(int f_index);
    float get_eff(int f_index);
    int get_ri();
    int get_mcs(int f_index);
    float get_sinr(int f_index);
    float get_linear_sinr(int f_index);
    float get_mean_sinr();
    float get_mean_rsrp();
    float get_mean_cqi();
    float get_mean_mcs();
    float get_mean_eff();
    float get_mean_scheduler_metric() const;
    bool uses_throughput_history_scheduler() const
    {
        return metric_h.uses_throughput_history();
    }
    void set_logger(log_handler *_logger);
    // priority and rr_rank come by value from ue_overrides, n_enabled from the scheduler.
    // priority is applied here and not inside metric_handler: metric_v[] is only rewritten
    // on a CQI refresh, so folding it into the cached value would delay a priority change
    // by up to cqi_period.
    float get_metric(int f, int n_enabled, int rr_rank, float priority);
    float get_tp(int f);
    float assignRFBandwidth(float bandwidth);
    void finalize_ul_allocation(int allocated_prbs);
    void prepare_scheduler_tti(bool active);
    void record_provisional_service(float scheduled_bits);
    void update_scheduler_state(float effective_bits, bool active);
    void reset_scheduler_state();
    float get_current_tx_power_per_prb_dbm() const { return current_tx_power_per_prb_dbm; }
    int get_ul_scheduling_prbs() const { return ul_scheduling_prbs; }
    float get_throughput_average_bits_per_tti() const
    {
        return throughput_state.average_throughput();
    }

public:
    // sinr_offset_db comes by value from ue_overrides and is added in sinr_power_model,
    // which also shifts the reported RSRP: an offset standing for power or losses moves
    // both. Stored in a member for the duration of the call instead of threaded through
    // estimate_sinr; only this UE's own thread is inside estimate_channel_state.
    void estimate_channel_state(float distance, phy_shared &phy_s, float macro_fading, int o2i, int _tx_dir, const pos2d &pos, float oldest_t, float avg_tp, float _current_t, float sinr_offset_db);

private:
    void init_metric(int _index);
    void init_scenario(int _type, float _eNB_h, float _w, float _antenna_h, float _ue_h, float _ue_speed); 
    void init_phy(float _freq, float _ue_speed, float _target_ber, float _ue_h, int _tx, float _scaling_factor, int _mimo_l, int _numerology, int _n_sc_rbg, bool _mcs_tables, float _gain_tx, float _gain_rx, float _tx_power,float _alpha_ul,float _nominal_pusch_p0,ul_power_mode _ul_power_mode, float _tx_power_ul, float _power_boost,float _thermal_noise_density,float _NF_UT,float _NF_eNB,int  _num_interf_ues,int  _num_interf_eNBs,float _d_interference, float _interfered_ratio);
    void estimate_noise_interference(float _tx_power, int _n_ues, float _d_interference, float _interfered_ratio, float _figure, float _gain_tx, float _gain_rx);
    void init_amc(int _modulation_m, int _cqi_m, int _cqi_p);
    void init_rank(int _period, int _n_antennas,int _mimo_layers);
    void init_update_rates(float _doppler_f, int _cqi_p, int _ri_p);

private:
    int estimate_cqi_from_distance(float distance);
    float estimate_eff_from_sinr(float sinr);
    float estimate_eff_from_mcs(int mcs);
    float estimate_cqi_from_se(float se);
    float estimate_mcs_from_se(float se, float &eff);
    void estimate_channel_q(int f);
    void channel_q_tables(int f);
    int get_mcs_index(float sinr);
    void channel_q_efficiency(int f);
    void estimate_ri();
    float sinr_power_model(float _tx_power,float distance,float pathloss, float macro_fading);
    float ul_power_per_prb_dbm(float distance, int allocated_prbs);
    void estimate_sinr(float distance, int f, float macro_fading);
    void estimate_attenuation(float distance, const pos2d &pos, phy_shared &phy_s);
    void average_cqi();
    void average_sinr();
    void reset_cqi();
    float compute_losses(float distance);//this method includes the oxygen absortion, the penetration and in_building losses
    float compute_penetration_losses(
        float building_sample,
        float vehicle_sample);
    float compute_pathloss_ABG(float d, bool LOS);
    float calculateOxygenLoss();
    void check_boundaries();
    void compute_coherence_bw();
    float compute_rayleigh();
    void estimate_metric(int f);
    void estimate_tp(int f);
    void prepare_metrics(float oldest_t, float avg_tp);
    float standalone_rate_bits_per_tti() const;
    float throughput_metric_average() const;
    int get_modulation_index();
    int get_n_layers();

private:
    std::mt19937 fading_gen;
    std::mt19937 interference_gen;
    std::mt19937 cqi_distance_gen;
    std::uniform_real_distribution<float> distance_cqi_dist{-1, 1};
    std::uniform_real_distribution<float> uniform_stochastics{0.0, 1.0};
    std::normal_distribution<float> sinr_stochastics{0.0, 1.0};

private:
    int modulation_m;

private:
    float target_ber ;

private:
    int tx;
    std::string tx_s;

private:
    int cqi_mode;
    int cqi_period;
    int sinr_period;

private:
    int ri_period;
    int max_ri;

private:
    float noise_interf;
    float thermal_noise_density;
    float antenna_gain_tx;
    float antenna_gain_rx;
    float noise_figure;
    float linear_interference_transmitted;
    float linear_noise;
    float NF_UT;
    float NF_eNB;
    int num_interf_ues;
    int num_interf_eNBs;
    float d_interference;
    float interfered_ratio;

private:
    int period_counter = 0;

private:
    int verbosity = 0;

private:
    bool stochastics = true;

private:
    float tx_power_eNb;
    float alpha_ul;
    float nominal_pusch_p0;
    ul_power_mode power_mode = ul_power_mode::fixed;
    float tx_power_ul;
    int ul_scheduling_prbs = 1;
    float current_tx_power_per_prb_dbm = 0.0f;
    float last_distance = 0.0f;
    float power_boost;

private:
    bool update_cs = true;

private:
    float pathloss;
    float macro_fading;
    float sinr_offset_db = 0.0f;
    float c_dist;

private:
    float doppler_f;
    float tc;
    float ds;
    float bc;

private:
    int n_rbs;
    int n_sc_rb;
    int n_rb_rbg;
    int n_tot_RB;
    int rbg_lookup_index;
    int layers;
    float overhead;
    float scaling_factor;
    int mimo_l;
    int sinr_rbs;
    int sinr_step;
    int bandwidth;
    int numerology;

private:
    bool mcs_tables;
    float mcs_lookup[MCS_INDEX_MAX];

private:
    bool is_init = false;

private:
    bool phy_log = false;
    log_handler *logger;

private:
    metric_handler metric_h;
    metric_info metric_i;
    throughput_history_state throughput_state;
    bool throughput_intra_tti_update = false;
    float provisional_service_bits = 0.0f;

private:
    std::vector<int> cqi_v;
    std::vector<int> mcs_v;
    std::vector<float> eff_v;
    std::vector<float> metric_v;
    std::vector<float> tp_v;
    std::vector<float> rsrp_v;
    std::vector<float> db_sinr_v;
    std::vector<float> l_sinr_v;

private:
    float max_speed;

private:
    int cqi_s;
    int mcs_s;
    float eff_s;
    float rsrp_s;
    float db_sinr_s;
    float l_sinr_s;

private:
    int scenario;
    bool d_in_computed = false;
    bool init_o2i = false;
    float eNB_h;
    float ue_h;
    float antenna_h;
    float antenna_h_172;
    float w;
    int o2i;
    int penetration_profile = PENETRATION_LEGACY_AUTO;
    int vehicle_profile = VEHICLE_STANDARD;
    int tx_dir = TX_DL;
private:
    pos2d prev_pos;
    float a_oxygen;
    // compute_losses() adds pltw + plin, and estimate_noise_interference() calls it during
    // init(), before estimate_channel_state() has had a chance to compute them. Without an
    // initializer the noise floor of the very first run depended on whatever was left in
    // that memory, which made runs irreproducible and occasionally produced an infinite
    // noise_interf, and with it SINR = -inf for the whole UE.
    float d_in = 0.0f;
    float plin = 0.0f;
    float pltw = 0.0f;

private:
    float freq;
    float freq_ghz;

private:
    int current_ri;

private:
    float current_t;

private:
    int id;
};
#endif
