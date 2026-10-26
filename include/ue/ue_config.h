/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <phy_layer/phy_config.h>
#include <phy_layer/phy_l_definitions.h>
#include <random>
#include <traffic_models/traffic_config.h>//este no estaba antes
#include <pdcp_layer/pdcp_config.h>
#include <mobility_models/mobility_config.h>
#include <common/direction.h>

#define MAX_N_ANTENNAS 4
#define MIN_N_ANTENNAS 1

#define MAX_SINR 35
#define MIN_SINR -10

//--------------------------------------------------------------------------------------------------
// ue_model(): struct storing all the configuration information related with the UE model
// Input: 
//      _type: id of the desired mobility model for the current UE. 
//      _x,_y: initial position only used if _random_init is set to false.
//      _random_init: wether to randomly initialized or not
//      _speed: target speed of the UEs in km/h.
//      _speed_var: size of the white noise to be applied in each timestep to the target speed.
//      _max_distance: max. distance of the UE to the gNB. 
//      _time_target: target time used in some of the implemented models, such as random walk model.
//      _time_target_var: size of the white noise to be applied in each timestep to the target time.
//--------------------------------------------------------------------------------------------------
struct ue_model
{
    ue_model(){}
    ue_model(int _n_antennas, float _nominal_pusch_p0, ul_power_mode _ul_power_mode, float _tx_power_ul, float _alpha_ul, int _scaling_factor, int _cqi_period, 
             int _ri_period, float _ue_h,int _o2i)
             {
                 n_antennas = _n_antennas;
                 cqi_period = _cqi_period; 
                 ri_period = _ri_period; 
                 ue_h = _ue_h; 
                 nominal_pusch_p0=_nominal_pusch_p0;
                 power_mode=_ul_power_mode;
                 tx_power_ul=_tx_power_ul;
                 alpha_ul=_alpha_ul;
                 scaling_factor = _scaling_factor; 
                 o2i=_o2i;
                 penetration_profile =
                     _o2i == IN_BUILDING
                         ? PENETRATION_LEGACY_AUTO
                         : PENETRATION_NONE;
                 vehicle_profile = VEHICLE_STANDARD;
            } 
    int n_antennas; 
    float nominal_pusch_p0; 
    ul_power_mode power_mode = ul_power_mode::fixed;
    float tx_power_ul = 23.0f;
    float alpha_ul; 
    float scaling_factor = 1;
    int o2i;
    int penetration_profile = PENETRATION_LEGACY_AUTO;
    int vehicle_profile = VEHICLE_STANDARD;
    int cqi_period; 
    int ri_period; 
    float ue_h; 
};

// ue_config(): struct storing all the configuration information related with the UE
// Input: 
//              *ue_m: UE configuration struct which includes: 
//                      *n_antennas: number of physical antennas in the UE.
//                      *alpha_ul: factor controlling the UE uplink power adjustment based on path loss.
//                      *nominal_pusch_p0: target received power for the UE in the PUSCH. 
//                      *cqi_period: period in ms for the PHY layer to re-estimate the CSI.
//                      *ri_period: period in ms for the PHY layer to estimate the Rank Indicator.
//                      *scaling_factor: used for carrier aggregation throughput calculation. Is signaled by the eNB in a real deployment. It can take the values: 1, 0.8, 0.75, 0.4
//                      *ue_h: height of the UE.
//              *traffic_c: traffic generator configuration struct which includes: 
//                      *type: selected traffic generator models for this type of UEs.
//                      *ul_target/_dl_target: target throughput for UL and DL respectively.
//                      *var_perc: size of the variance of the generated noise
//                      *pkt_size: virtual IP packet size in bits.
//              *mobility_c: mobility model configuration struct which includes: 
//                     _type: id of the desired mobility model for the current UE. 
//                     _x,_y: initial position only used if _random_init is set to false.
//                     _random_init: wether to randomly initialized or not
//                     _speed: target speed of the UEs in km/h.
//                     _speed_var: size of the white noise to be applied in each timestep to the target speed.
//                     _max_distance: max. distance of the UE to the gNB. 
//                     _time_target: target time used in some of the implemented models, such as random walk model.
//                     _time_target_var: size of the white noise to be applied in each timestep to the target time.
//              *delta_metric: metric-specific parameter.
//              *delay_t_metric: metric-specific parameter.
//              *beta_metric: metric-specific parameters. 
//              *priority: used by the user priotitization algorithm implemented in the MAC Layer. 
//      _scenario_c: scenario configuration struct which includes: 
//              *type: scenario type ID from the ones implemented.
//              *w: width of the beam.
//              *antenna_h: height of the antenna.
//              *eNB_h: height of the eNB/gNB.
//              *mcs_tables: enable/disable custom MCS-BLER-SINR curves for estimating the MCS indexes. 
//      _phy_enb_config: eNB configuration struct which includes: 
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
//              *eNB_gain: modeled  gain of the emulated eNB/gNB.
//              *UT_gain: modeled  gain of the emulated UT.
//      _pdcp_config_ul/_pdcp_config_dl: UL/DL PDCP configuration struct which includes:
//              *max_rtx_ul/dl: max number of retransmissions for UL/DL HARQ packets.
//              *air_delay_var: added variance to the delay comming from the air propagation.
//              *rtx_period_ul/dl: ack/nack delay in seconds for UL/DL HARQ packets.
//              *rtx_period_ul/dl_var: white noise spread around the ack/nack delay in seconds for UL/DL HARQ packets.
//              *rtx_proc_delay_ul/dl: ack/nack processing delay in seconds for UL/DL HARQ packets.
//              *rtx_proc_delay_ul/dl_var: white noise spread around the ack/nack processing delay in seconds for UL/DL HARQ packets.
//              *backhaul_d: backhauling delay, use for Release buffer.
//              *backhaul_d_var: white noise spread around the backhauling delay.
//              *order_pkts: enable/disable RLC reordering.
//--------------------------------------------------------------------------------------------------
struct ue_config
{
    ue_config(){}
    ue_config(ue_model _ue_model, 
              traffic_config _traffic_c, mobility_config _mobility_c, int _priority = 1,
              float _delta_metric = 1.0, float _delay_t_metric = 0.1, float _beta_metric = 0.5, float _pkt_delay_budget = 0.35)
            {
                ue_m = _ue_model; 
                traffic_c = _traffic_c; 
                mobility_c = _mobility_c; 
                delta_metric = _delta_metric; 
                delay_t_metric = _delay_t_metric; 
                beta_metric = _beta_metric; 
                priority = _priority; 
                pkt_delay_budget = _pkt_delay_budget;
            }
    int ul_queue_n = -1; 
    int dl_queue_n = -1;
    std::string control_id = "";
    ue_model ue_m; 
    traffic_config traffic_c; 
    mobility_config mobility_c; 
    float delta_metric; 
    float delay_t_metric; 
    float beta_metric;
    bool beta_metric_configured = false;
    float pkt_delay_budget = 0.35f;
    int log_freq = 0; 
    bool log_ue = false; 
    bool log_mobility = false; 
    bool log_traffic = false; 
    bool log_quality = false;
    std::string log_id = "";   
    float priority = 1; 
    dualpi2_config l4s_c;
    phy_ue_config get_phy_config()
    {
      return phy_ue_config(ue_m.alpha_ul,ue_m.nominal_pusch_p0,ue_m.power_mode,ue_m.tx_power_ul, ue_m.cqi_period, ue_m.ri_period,
                                  ue_m.n_antennas,  ue_m.ue_h, priority,
                                  delay_t_metric, delta_metric, beta_metric, mobility_c.get_max_speed(), ue_m.scaling_factor,
                                  ue_m.penetration_profile, ue_m.vehicle_profile);
    }
};
