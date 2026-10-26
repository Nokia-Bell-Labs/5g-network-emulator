/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef TRAFFIC_MODEL_H
#define TRAFFIC_MODEL_H

#include <traffic_models/traffic_model_base.h>
#include <traffic_models/traffic_generator.h>
//--------------------------------------------------------------------------------------------------
// traffic_model(): class which selects the traffic generator model which will be used to generate
// virtual bits for simulated users. 
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      type: id of the traffic generator model which was selected for the current UE.
//      _ul_target/_dl_target: target throughput for UL and DL respectively. 
//      _var_perc: size of the variance of the generated noise. 
//      _pkt_size: virtual IP packet size in bits.
//--------------------------------------------------------------------------------------------------
class traffic_model
{
public:
    traffic_model(int id, traffic_config traffic_c);

public:
    float generate(int tx, double t);

    int get_pkt_size(int tx);

    // Target rate in bits per second.
    void set_target(int tx, float bps);
    float get_target(int tx) const;

private: 
    std::unique_ptr<traffic_model_base> traff_model; 
};

#endif
