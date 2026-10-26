/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef UE_H
#define UE_H

#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <phy_layer/phy_layer.h>
#include <phy_shared/phy_shared.h>
#include <mobility_models/mobility_model.h>
#include <pdcp_layer/pdcp_layer.h>
#include <mac_layer/harq_config.h>
#include <map/map_handler.h>

// Logging
#include "utils/logging/mean_handler.h"
#include <ue/ue_config.h>
#include <utils/control/ue_overrides.h>

struct schedule_candidate
{
    bool has_data = false;
    float bits_per_symbol = 0.0f;
    float metric = 0.0f;
    int ue_index = -1;
    int ue_id = -1;
};

//--------------------------------------------------------------------------------------------------
// ue(): this class is in charge of modeling and updating the simulated UE's position, 
// generating/handling packets, estimating the Channel State Indicator and Releasing or Dropping the 
// already processed packets. The emulator's instances of mobility_model(), packet_handler(),
// phy_layer() and pdcp_layer() are all part of the UE module.
// Input: 
//      ue_type/_init_t: select and initialize the packet ingress/release handlers for UL/DL.
//      ue_c: configuration struct. Includes:
//              *ue_m: UE configuration struct which includes: 
//                      *n_antennas: number of physical antennas in the UE.
//                      *tx_power: transmission power for the UE.
//                      *cqi_period: period in ms for the PHY layer to re-estimate the CSI.
//                      *ri_period: period in ms for the PHY layer to estimate the Rank Indicator.
//                      *scaling_factor: used for carrier aggregation throughput calculation. Is signaled by the eNB in a real deployment. It can take the values: 1, 0.8, 0.75, 0.4
//                      *ue_h: height of the UE.
//                      *map_file: route to the map
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
//      _harq_config: HARQ configuration struct which includes: 
//              *mod: 0 - 64 QAM | 1 - 256 QAM
//              *layers: Number of MIMO layers. 
//              *log_units: number of RBs per RBG group. 
//      log_c: Logger configuration struct which includes: 
//              *log_freq: data is logged according to this period in ms.
//              *log_ue: enable/disable UE logging
//              *log_id: UE's unique ID
//              *verbosity: enable/disable verbosity
//--------------------------------------------------------------------------------------------------
class ue
{
public: 
    ue(int _id,
       ue_config ue_c,
       scenario_config _scenario_c,
       phy_enb_config _phy_enb_config,
       pdcp_config _pdcp_config_ul,
       pdcp_config _pdcp_config_dl,
       harq_config _harq_config,
       int ue_type,
       std::chrono::microseconds *init_t,
       bool _stochastics = true);

public: 
    void init(); 
    float get_metric(int tx_dir, int f_index, int n_enabled);
    float get_tp(int tx_dir, int f_index);
    int get_cqi(int tx_dir, int f_index);
    float get_eff(int tx_dir, int f_index);
    int get_mcs(int tx_dir, int f_index);
    float get_sinr(int tx_dir, int f_index);
    float get_linear_sinr(int tx_dir, int f_index);
    float get_mean_sinr(int tx_dir);
    int get_ri(int tx_dir);
    float get_avg_tp(int tx_dir);
    bool get_traffic_verbosity();
    float get_avg_l(int tx_dir);
    float get_consumed_bits(int tx_dir);
    float get_oldest_timestamp(int tx_dir);
    float add_pkts(int tx, std::deque<ip_pkt> &pkts);
    int get_id(); 
    const std::string& get_control_id() const { return control_id; }
    bool has_packets(int tx_dir);
    schedule_candidate get_schedule_candidate(int tx_dir, int f_index, int n_enabled, int ue_index);
    float get_delay_t(); 
    float get_delta(); 
    float handle_pkt(float bits, int tx_dir, int f_index);
    void finalize_ul_allocation(int allocated_prbs);
    void prepare_scheduler_tti(int tx_dir, bool active);
    void record_provisional_scheduler_service(int tx_dir, float scheduled_bits);
    void commit_scheduler_tti();
    float get_ul_tx_power_per_prb_dbm() const { return phy_ul.get_current_tx_power_per_prb_dbm(); }
    int get_ul_scheduling_prbs() const { return phy_ul.get_ul_scheduling_prbs(); }
    float get_throughput_average_bits_per_tti(int tx_dir) const
    {
        return tx_dir == TX_DL
                   ? phy_dl.get_throughput_average_bits_per_tti()
                   : phy_ul.get_throughput_average_bits_per_tti();
    }
    int get_max_service_gap_ttis(int tx_dir) const
    {
        return scheduler_max_service_gap_ttis[tx_dir];
    }
    std::uint64_t get_delivered_bits_total(int tx_dir) const
    {
        return tx_dir == TX_DL
                   ? pdcp_dl.delivered_bits_total()
                   : pdcp_ul.delivered_bits_total();
    }
    void reset_scheduler_statistics()
    {
        scheduler_current_service_gap_ttis[TX_DL] = 0;
        scheduler_current_service_gap_ttis[TX_UL] = 0;
        scheduler_max_service_gap_ttis[TX_DL] = 0;
        scheduler_max_service_gap_ttis[TX_UL] = 0;
    }
    void step();
    void add_current_t(double _current_t){current_t = _current_t; }

    // Runtime control state. Only control_manager::tick() writes through this accessor.
    ue_overrides& overrides() { return ctl; }
    const ue_overrides& overrides() const { return ctl; }

    // Logical attach/detach. On detach the UE drops every live buffer; see
    // 02-enabled-semantics: it is removed from the simulation for all purposes.
    void set_enabled(bool on);
    bool is_enabled() const { return ctl.enabled; }

    // Handles used by the parameter registry. Mobility and traffic are input state, so
    // they are assigned through setters instead of overridden at the point of reading.
    mobility_model& mobility() { return mobility_m; }
    bool set_traffic_target(int tx_dir, float bps) { return pdcp(tx_dir).set_traffic_target(bps); }
    bool get_traffic_target(int tx_dir, float &bps) { return pdcp(tx_dir).get_traffic_target(bps); }
    bool inject_bits(
        int tx_dir,
        std::uint64_t bits,
        std::uint32_t tag,
        std::uint8_t ecn = ECN_NOT_ECT)
    { return pdcp(tx_dir).inject_bits(bits, tag, ecn); }
    // A tag is unique per UE, so forgetting it is not directional.
    bool forget_object(std::uint32_t tag)
    {
        const bool dl = pdcp_dl.forget_object(tag);
        const bool ul = pdcp_ul.forget_object(tag);
        return dl || ul;
    }
    void enable_object_events()
    {
        pdcp_dl.enable_object_events();
        pdcp_ul.enable_object_events();
    }
    std::unordered_map<std::uint32_t, object_counters> take_object_events(int tx_dir)
    { return pdcp(tx_dir).take_object_events(); }
    void set_pkt_delay_budget(float budget_s)
    {
        pdcp_dl.set_pkt_delay_budget(budget_s);
        pdcp_ul.set_pkt_delay_budget(budget_s);
    }
    float get_pkt_delay_budget() const { return pdcp_dl.get_pkt_delay_budget(); }

    // Read only view for the control channel's get: what the client needs to pace its
    // own injection. Bits and seconds here; the conversion to bytes happens at the edge.
    pdcp_layer& pdcp_state(int tx_dir) { return pdcp(tx_dir); }
    int get_pkt_size(int tx_dir) { return pdcp(tx_dir).get_pkt_size(); }
    float get_apothem() { return mobility_m.get_apothem(); }

private: 
    void init_logger(); 
    void emit_pdcp_monitoring();
    void emit_queue_monitoring();
    void emit_phy_monitoring();
    void emit_l4s_monitoring();
    void emit_mobility_monitoring();
    phy_layer& phy(int tx_dir);
    const phy_layer& phy(int tx_dir) const;
    pdcp_layer& pdcp(int tx_dir);
    const pdcp_layer& pdcp(int tx_dir) const;
    void init_phy_update_rates(float _doppler_f, int _cqi_p, int _ri_p);
    void estimate_channel_state();

private:
    std::mt19937 gen;
    std::uniform_real_distribution<float> distance_cqi_dist{-1,1};
    std::uniform_real_distribution<float> uniform_stochastics{0.0, 1.0};
    std::normal_distribution<float> sinr_stochastics{0.0, 1.0};

protected: 

    void update_pos();
    void update_pdcp(); 
    void add_ts(); 

public:
    void print_traffic();

protected: 
    int id; 
    std::string control_id;
    ue_overrides ctl;

protected: 
    MapHandler map;
    pdcp_layer pdcp_dl;
    pdcp_layer pdcp_ul;
    phy_layer phy_dl;
    phy_layer phy_ul;
    mobility_model mobility_m; 
    phy_shared phy_s;
    float delta_metric; 
    float delay_t_metric; 
    float beta_metric; 
protected:
    bool log_ue; 
    bool log_traffic; 
    bool log_mobility; 
    bool log_quality; 
    int verbosity; 
    bool store_data; 

protected: 
    log_handler ue_log; 
     int n_antennas;
     pos2d prev_pos;
     pos2d pos;
     bool update_cs;
     int o2i;
     float c_dist;
 
protected: 
    double current_t;

private:
    bool phy_init_o2i = false;
    float phy_macro_fading = 0.0f;
    int phy_o2i = OUTDOOR;
    int phy_sinr_period = 1;
    pos2d phy_prev_pos;
    int phy_period_counter = 0;
    float phy_tc = 0.0f;
    int phy_cqi_period = 1;
    int phy_ri_period = 1;
    float phy_corr_distance = 0.0f;
    float phy_c_dist = 0.0f;
    float phy_freq = 0.0f;
    float phy_speed = 0.0f;
    float phy_doppler_f = 0.0f;
    bool phy_update_cs = true;
    dualpi2_stats last_l4s_ul_interval_stats;
    dualpi2_stats last_l4s_dl_interval_stats;
    float scheduler_effective_bits[2] = {0.0f, 0.0f};
    bool scheduler_active[2] = {false, false};
    int scheduler_current_service_gap_ttis[2] = {0, 0};
    int scheduler_max_service_gap_ttis[2] = {0, 0};

};

#endif
