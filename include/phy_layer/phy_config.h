/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/

#ifndef PHY_CONFIG_H
#define PHY_CONFIG_H

#include <random>
#include <string>
#include <map/map_handler.h>
#include <phy_layer/phy_l_definitions.h>
#include <phy_layer/power_model.h>

//--------------------------------------------------------------------------------------------------
// scenario_config(): modeled scenario configuration struct.
// Input:
//              *type: scenario type ID from the ones implemented.
//              *w: width of the beam.
//              *antenna_h: height of the antenna.
//              *eNB_h: height of the eNB/gNB.
//              *mcs_tables: enable/disable custom MCS-BLER-SINR curves for estimating the MCS indexes.
//--------------------------------------------------------------------------------------------------
struct scenario_config
{
    // scenario_config() {}
    scenario_config(int _type, float _w, float _antenna_h, float _eNB_h, std::string _map_file, bool _mcs_tables = true)

    {
        type = _type;
        w = _w;
        antenna_h = _antenna_h;
        eNB_h = _eNB_h;
        map_file = _map_file;
        mcs_tables = _mcs_tables;
    }

    int type;
    float w;
    float antenna_h;
    float eNB_h;
    std::string map_file;
    bool mcs_tables;
};

//--------------------------------------------------------------------------------------------------
// phy_enb_config(): PHY Layer eNB/gNB configuration struct.
// Input:
//              *tx_power: transmission power of the eNB/gNB
//              *modulation_m: 0 - 64 QAM | 1 - 256 QAM
//              *target_ber: used for MCS indexes estimation when the MCS tables are not used.
//              *cqi_mode: 0 - Wideband | 1 - Subband
//              *frequency: base frequency in which the eNB/gNB is operating.
//              *bandwidth: total available bandwidth.
//              *mimo_l: number of available eNB/gNB MIMO layers.
//              *metric_type: ID of the metric type.
//              *n_int_ues: dumber of interfering UEs, not attached to the simulated eNB/gNB
//              *n_int_eNBs: dumber of interfering eNBs, not attached to the simulated eNB/gNB
//              *d_interference: interference distance of nearby UEs
//              *thermal_noise: modeled thermanl noise of the emulated eNB/gNB.
//              *figure_noise: modeled noise figure of the emulated eNB/gNB.
//              *eNB_gain: modeled  gain of the emulated eNB/gNB.
//              *UT_gain: modeled  gain of the emulated UT.
//--------------------------------------------------------------------------------------------------
struct phy_enb_config
{
    phy_enb_config() {}
    phy_enb_config(float _tx_power, int _modulation_m, float _target_ber,
                   int _cqi_mode, float _frequency, int _bandwidth, int _mimo_l,
                   int _metric_type, int _n_int_ues, int _n_int_eNBs, float _d_int, float _interfered_ratio,
                   float _thermal_n, float _figure_n_enb, float _figure_n_ut, float _eNB_gain, float _UT_gain, float _power_boost, int _numerology,
                   float _pf_alpha = 1.0f,
                   float _throughput_time_window_ms = 100.0f,
                   bool _throughput_intra_tti_update = false)
    {
        tx_power = _tx_power;
        modulation_m = _modulation_m;
        target_ber = _target_ber;
        cqi_mode = _cqi_mode;
        frequency = _frequency;
        bandwidth = _bandwidth;
        mimo_l = _mimo_l;
        metric_type = _metric_type;
        n_int_ues = _n_int_ues;
        n_int_eNBs = _n_int_eNBs;
        d_interference = _d_int;
        interfered_ratio = _interfered_ratio;
        thermal_noise = _thermal_n;
        figure_noise_enb = _figure_n_enb;
        figure_noise_ut = _figure_n_ut;
        eNB_gain = _eNB_gain;
        UT_gain = _UT_gain;
        power_boost = _power_boost;
        numerology = _numerology;
        pf_alpha = _pf_alpha;
        throughput_time_window_ms = _throughput_time_window_ms;
        throughput_intra_tti_update = _throughput_intra_tti_update;
    }
    float tx_power;
    int modulation_m;
    float target_ber;
    int cqi_mode;
    float frequency;
    int bandwidth;
    int n_rbgs;
    int n_carrier_prbs = 0;
    int n_sc_rbg;
    int mimo_l;
    int metric_type;
    int n_int_ues;
    int n_int_eNBs;
    float d_interference;
    float interfered_ratio;
    float thermal_noise;
    float figure_noise_enb;
    float figure_noise_ut;
    float eNB_gain;
    float UT_gain;
    float power_boost;
    int numerology;
    float pf_alpha = 1.0f;
    float throughput_time_window_ms = 100.0f;
    bool throughput_intra_tti_update = false;
};

//--------------------------------------------------------------------------------------------------
// phy_ue_config(): UE PHY layer configuration struct.
// Input:
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
//--------------------------------------------------------------------------------------------------
struct phy_ue_config
{
    phy_ue_config() {}
    phy_ue_config(float _alpha_ul, float _nominal_pusch_p0, ul_power_mode _ul_power_mode, float _tx_power_ul, int _cqi_period,
                  int _ri_period, int _n_antennas, float _ue_h, float _priority,
                  float _delay_t, float _delta, float _beta, float _max_speed, float _scaling_factor,
                  int _penetration_profile = PENETRATION_LEGACY_AUTO,
                  int _vehicle_profile = VEHICLE_STANDARD)
    {

        cqi_period = _cqi_period;
        alpha_ul = _alpha_ul;
        nominal_pusch_p0 = _nominal_pusch_p0;
        power_mode=_ul_power_mode;
        tx_power_ul=_tx_power_ul;
        // RI estimation
        ri_period = _ri_period;
        n_antennas = _n_antennas;

        ue_h = _ue_h;
        delay_t = _delay_t;
        delta = _delta;
        beta = _beta;
        max_speed = _max_speed;

        scaling_factor = _scaling_factor;

        priority = _priority;
        penetration_profile = _penetration_profile;
        vehicle_profile = _vehicle_profile;
    }

    float nominal_pusch_p0;
    float alpha_ul;
    ul_power_mode power_mode = ul_power_mode::fixed;
    float tx_power_ul;
    int cqi_period;
    int ri_period;
    int n_antennas;
    float ue_h;
    float delay_t;
    float delta;
    float beta;
    float max_speed;
    float scaling_factor;
    float priority = 1;
    int penetration_profile = PENETRATION_LEGACY_AUTO;
    int vehicle_profile = VEHICLE_STANDARD;
};

#endif
