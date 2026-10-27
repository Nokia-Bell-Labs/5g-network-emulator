/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <common/direction.h>

#define CONSTANT_TRAFFIC_MODEL 0
//--------------------------------------------------------------------------------------------------
// traffic_config(): struct storing all the configuration information related with the traffic
// generator models. 
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      type: id of the traffic generator model which was selected for the current UE.
//      _ul_target/_dl_target: target throughput for UL and DL respectively. 
//      _var_perc: size of the variance of the generated noise. 
//      _pkt_size: virtual IP packet size in bits.
//--------------------------------------------------------------------------------------------------
struct traffic_config
{
    traffic_config(){}
    traffic_config(int _type, float _ul_target, float _dl_target, std::string _ul_traffic_file, std::string _dl_traffic_file, float _var_perc,
                    int _pkt_size, float _bh_d, float _bh_d_var, double _delay = 0)
                    {
                        type = _type; 
                        ul_target = _ul_target; 
                        dl_target = _dl_target; 
                        ul_traffic_file = _ul_traffic_file; 
                        dl_traffic_file = _dl_traffic_file; 
                        var_perc = _var_perc; 
                        pkt_size = _pkt_size; 
                        delay = _delay;
                    }
    int type; 
    float ul_target; 
    float dl_target;
    std::string dl_traffic_file;
    std::string ul_traffic_file;  
    float var_perc; 
    int pkt_size;
    double delay;
    bool random_v=true; 
};
