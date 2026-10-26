/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef RESOURCE_BLOCK_H
#define RESOURCE_BLOCK_H

#include <string>
#include <algorithm>

#include <mac_layer/metric_handler.h>
#include <ue/ue.h>

// Logging

#include "utils/logging/log_handler.h"

//--------------------------------------------------------------------------------------------------
// rb(): class which is in charge of assigning the best UE to the current RB/RBG. This class gets the
// estimated metric for each UE assigning the one with highest metric. It also estimates the number 
// of bits granted to the assigned users according to the selected duplexing type.
// Input: 
//      * _t: time index of the current RB/RBG within the resource allocation grid.
//      * _f: frequency index of the current RB/RBG within the resource allocation grid.
//      * _tx: transmission direction of current RB/RBG.
//      * _mimo_layers: number of available MIMO Layers.
//      * _n_sc: total number of subcarriers.
//      * *_ue_list: pointer to the list of connected UEs.
//      * _verbosity: enable/disable verbosity
//      * _logger: pointer to instance of the logger class, used for data logging.
//--------------------------------------------------------------------------------------------------
class rb
{
public: 
    rb(int _t, int _f, int _tx, int _mimo_layers,  int _n_sc, std::vector<ue> *_ue_list, int _verbosity = 0, log_handler * _logger = nullptr); 
private: 
    bool store_hist; 
    int mimo_layers; 
    int t; 
    int f; 
    int tx; 
    int n_sc; 
    max_metric_handler max_metric;    
    std::vector<ue> *ue_list; 
    float current_t;
    bool log; 

private: 
    void reset_metric();
    bool check_ue_assignment();
public: 
    // n_enabled: how many UEs are enabled this TTI. The round robin wraps on it, so it
    // has to be the same number for every RBG and every UE of the step.
    void estimate_params(int syms, float current_t, int n_enabled);
    // Handle Packets
    float handle_packet(int syms);
    bool was_scheduled() const { return last_scheduled; }
    int planned_ue_index() const
    {
        return max_metric.is_assigned() && max_metric.has_data()
                   ? max_metric.get_index()
                   : -1;
    }
    float planned_scheduled_bits(int syms) const
    {
        return planned_ue_index() >= 0
                   ? max_metric.get_tp() * syms
                   : 0.0f;
    }
    int last_ue_index() const { return last_ue_index_v; }
    float last_scheduled_bits() const { return last_scheduled_bits_v; }
    float last_effective_bits() const { return last_effective_bits_v; }
    log_handler * logger;
private: 
    int verbosity = 0; 
    bool last_scheduled = false;
    int last_ue_index_v = -1;
    float last_scheduled_bits_v = 0.0f;
    float last_effective_bits_v = 0.0f;
    int tie_cursor = 0;
};

#endif
