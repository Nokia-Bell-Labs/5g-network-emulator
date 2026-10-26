/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <iostream>
#include <mac_layer/mac_config.h>
#include <mac_layer/resource_grid.h>
#include <ue/ue.h>
#include <threading/thread_pool.h>

//--------------------------------------------------------------------------------------------------
// mac_layer(): class which interfaces the resource allocation grids with other modules from the 
// emulator. The step() methods for both UL and DL allocation grids can run concurrently is
// _threading is enabled. 
// Input: 
//      * _threading: enable/disable multithreading.
//      * *ue_list: pointer to the list of connected UEs.
//      * _mimo_layers: number of available MIMO Layers.
//      * _numerology: chosen numerology or subcarrier spacing.
//      * _bandwidth: total available bandwidth.
//      * _scheduling_mode: scheduling mode - 0 - distributed | 1 - grouped
//      * _scheduling_type: scheduling type - 0 - group frequency RBs in RBGs | 1 - no frequency RBs grouping.
//      * _scheduling_config: configuration for scheduling type 0.
//      * _metric_type: selected metric type.
//      * _duplexing_type: TDD or FDD duplexing.
//      * _tdd_c: TDD slot configuration (only used if TDD duplexing is enabled).
//      * _log_c: data logger configuration.
//--------------------------------------------------------------------------------------------------
class mac_layer
{
public: 
    mac_layer(bool _threading, std::vector<ue> *ue_list, mac_config mac_c, tdd_config _tdd_c, log_config log_c, int _verbosity = 0);

public:
    void plot_info();
    void plot_info(int tx);
    int get_n_freq_rb();
    int get_n_freq_rbg();
    int get_n_sc_rbg();
    int get_n_time_rb();
    int get_bandwidth(); 
    int get_rbg_size();
    int get_logical_units();
    void init(std::vector<ue> *ue_list);
    void step(float current_t);

public: 
    void flush_logs(); 

private: 
    void update_ts(float t);

private: 
    grid grid_dl; 
    grid grid_ul;  
    log_handler logger_dl;   
    log_handler logger_ul;  

private: 
    thread_pool tp;
    bool threading; 

private: 
    int bandwidth; 
    bool log;   
    int verbosity = 0;    
    std::vector<ue> *ue_list_v = nullptr;
};
