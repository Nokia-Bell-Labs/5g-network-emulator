/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef UE_HANDLER_H
#define UE_HANDLER_H

#include <iostream>
#include <vector>

#include <ue/ue.h>
#include <threading/thread_pool.h>

struct ue_full_config;

//--------------------------------------------------------------------------------------------------
// ue_handler(): this helper class gets the UE-related configuration parameters and create all the
// emulated UEs. 
// Input: 
//      _threads: number of concurrent threads to be created. 
//--------------------------------------------------------------------------------------------------
class ue_handler
{
public: 
    ue_handler(int _threads);

public: 
    void add_ues(std::chrono::microseconds * _init_t, ue_full_config ue_c, phy_enb_config _phy_enb_config, scenario_config _scenario_c,  pdcp_config _pdcp_config_ul, pdcp_config _pdcp_config_dl, harq_config _harq_config, bool _stochastics = true);

    void init();

    void add_ue(std::chrono::microseconds * _init_t, ue_full_config ue_c, phy_enb_config _phy_enb_config, scenario_config _scenario_c, pdcp_config _pdcp_config_ul, pdcp_config _pdcp_config_dl, harq_config _harq_config, bool _stochastics = true);

public: 


    void step(double _current_t);

    void print_traffic();

public: 
    std::vector<ue>* get_ue_list();

    // Recomputes the dense round robin rank over the enabled UEs. Called once at init(),
    // when every UE is enabled and the rank equals the id, and again by
    // control_manager::tick() whenever the enabled set changes.
    static void refresh_enabled_ranks(std::vector<ue> &list);

    // Number of UEs currently attached. A disabled UE does not count anywhere.
    int enabled_count() const;

private: 
    
    void log_ue_creation(int _n_ues, int ue_type, int _n_antennas, int _cqi_period, int _ri_period, float scaling_factor);
private: 
    std::vector<ue> ue_list; 
    thread_pool tp; 
    int threads; 
    int n_ues; 
    int id = 0; 
    bool multithreading; 
    int n_freq_rbg = 0; 
    int bandwidth; 
};
#endif
