/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

//
// MAC CONFIG
//
struct mac_config
{
    mac_config(int _mimo_layers, int _numerology, int _n_re_freq, int _n_ofdm_syms,int _bandwidth, int _scheduling_mode,
            int _scheduling_type, int _scheduling_config, int _metric_type, 
            int _duplexing_type, float _ratio_DL_UL,
            float _pf_alpha = 1.0f,
            float _throughput_time_window_ms = 100.0f,
            bool _throughput_intra_tti_update = false)
    {
        mimo_layers = _mimo_layers; 
        numerology = _numerology; 
        n_re_freq = _n_re_freq; 
        n_ofdm_syms = _n_ofdm_syms; 
        bandwidth = _bandwidth; 
        scheduling_config = _scheduling_config; 
        scheduling_mode = _scheduling_mode;
        scheduling_type = _scheduling_type; 
        metric_type = _metric_type; 
        duplexing_type = _duplexing_type;
        ratio_DL_UL=_ratio_DL_UL;
        pf_alpha = _pf_alpha;
        throughput_time_window_ms = _throughput_time_window_ms;
        throughput_intra_tti_update = _throughput_intra_tti_update;
    }
    int mimo_layers; 
    int numerology; 
    int n_re_freq;
    int n_ofdm_syms;
    int bandwidth; 
    int scheduling_mode; 
    int scheduling_type; 
    int scheduling_config; 
    int metric_type; 
    int duplexing_type; 
    float ratio_DL_UL;
    float pf_alpha;
    float throughput_time_window_ms;
    bool throughput_intra_tti_update;
};