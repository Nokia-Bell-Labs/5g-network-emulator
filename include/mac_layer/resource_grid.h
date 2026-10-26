/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <math.h>

#include <mac_layer/resource_block.h>
#include <mac_layer/tdd_handler.h>
#include <mac_layer/mac_definitions.h>
#include <phy_layer/phy_l_definitions.h>
#include <utils/conversions.h>

struct grid_step_metrics
{
    int available_rbg_count = 0;
    int structural_unavailable_rbg_count = 0;
    int scheduled_rbg_count = 0;
    int effective_rbg_count = 0;
    int zero_effective_rbg_count = 0;
    int empty_rbg_count = 0;
    int scheduled_ue_count = 0;
    int active_ues_with_data = 0;
    float scheduled_bits = 0.0f;
    float effective_bits = 0.0f;
    float grid_capacity_bits = 0.0f;
    float utilization_ratio = 0.0f;
    float scheduling_efficiency_ratio = 0.0f;
};

//--------------------------------------------------------------------------------------------------
// grid(): class is in charge of building the Resource Allocation grid for the assigned transmission
// direction according to the selected configuration parameters. This is done following the
// respective 3GPP specifications. The main operations happen in the step() method in which the UEs
// are allocated to different RGB according to their estimated CSIs and the selected schedulling 
// algorithm
// Input: 
//      * _tx: transmission direction.
//      * _mimo_layers: number of available MIMO Layers.
//      * _numerology: chosen numerology or subcarrier spacing.
//      * _bandwidth: total available bandwidth.
//      * _scheduling_mode: scheduling mode - 0 - distributed | 1 - grouped
//      * _scheduling_type: scheduling type - 0 - group frequency RBs in RBGs | 1 - no frequency RBs grouping.
//      * _scheduling_config: configuration for scheduling type 0.
//      * _metric_type: selected metric type.
//      * _duplexing_type: TDD or FDD duplexing.
//      * _tdd_c: TDD slot configuration (only used if TDD duplexing is enabled).
//      * _verbosity: enable/disable verbosity
//--------------------------------------------------------------------------------------------------
class grid
{
    
public:
    grid(int _tx, int _mimo_layers, int _numerology,  int _n_re_f, int _n_re_t,  int _metric_t, int _bandwidth, int _scheduling_mode,
            int _scheduling_type, int _scheduling_config,
            int _duplexing_type, float _ratio_DL_UL, tdd_config _tdd_c, int _verbosity = 0);
 
private:
    int check_duplexing_type(int d_type);
    int check_sch_config(int config);
    int check_sch_mode(int mode);
    int check_numerology(int n);
    void check_frequency_division(int n);

public:
    void plot_info();
    int get_n_freq_rb();
    int get_n_freq_rbg();
    int get_n_time_rb();
    int get_n_sc_rbg(); 
    int get_rbg_size(); 
    int get_logical_units();
    void add_current_ts(float t);
    void init(std::vector<ue> * ue_list);
    float assignRFBandwidth(float bandwidth); 
    void step();
    const grid_step_metrics &get_last_step_metrics() const { return last_step_metrics; }
    void set_logger(log_handler * _logger)
    {
        log = true; 
        logger = _logger;
    }

private: 
    // Definitions
    // -----------
    int tx; 
    int mimo_layers; // Number of MIMO layers
    int numerology; // Numerology which determines the subcarrier spacing
    int bandwidth; // Available total bandwidth for the current grid
    int n_sc_rb; // Subcarriers per resource block
    int n_sc_rbg; // Subcarriers per resource block group
    int n_sym_rbg; // OFDM symbols per resource block group
    int n_rb_max; // Max number of resource blocks. Is defined by the numerology
    int sc_spacing; // Subcarrier spacing in Hz
    int n_freq_rb; // Number of resource blocks along the frequency axis 
    int n_freq_rbg; // Number of resource block groups along the frequency axis 
    int n_freq_rb_rbg; // Number of resource blocks per resource block group along the frequency axis
    int n_logical_units; // Number of resource blocks per resource block group along the frequency axis
    int n_time_rb; // Number of resource blocks along the time axis
    int n_time_rbg; // Number of resource block groups along the time axis
    int n_rb_total; // Total number of resource blocks in the grid
    int n_re_total; // Total number of resource elements in the grid
    int n_ofdm_symbols; // Number of OFDM symbols per slot.
    std::vector<std::vector<rb>> rb_grid; // Grid where the actual rbs are stored
    int scheduling_mode; // Localized or distributed modes
    uint8_t scheduling_type; // Configuration of the resource grid shape
    uint8_t scheduling_config; // Also determines the grid's shape
    int metric_t; // Chosen metric type
    int duplexing_t; // Chosen duplexing mode
    tdd_handler tdd_h; // TDD scheme
    int log_freq; 
    bool log = false; 
    std::string log_id; 
    log_handler * logger; 
    int verbosity = 0; 
    float current_t = 0; 
    std::vector<ue> *ue_list = nullptr;
    grid_step_metrics last_step_metrics;
};
           
    
