/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef TRAFFIC_MODEL_BASE_H
#define TRAFFIC_MODEL_BASE_H

#define CONSTANT_TRAFFIC_MODEL 0

#include <random>
#include <assert.h>
#include <utils/rng_seed.h>
#include <memory>
#include <common/direction.h>
#include <utils/conversions.h>
#include <traffic_models/traffic_config.h>
//--------------------------------------------------------------------------------------------------
// traffic_model_base(): simple traffic generator model in which, in every time step, we generate as
// many UL or DL bits as required to fulfil the target data rate configured by the user. We add some
// white noise around the target throughut in each time step. 
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      _ul_target/_dl_target: target throughput for UL and DL respectively. 
//      _var_perc: size of the variance of the generated noise. 
//      _pkt_size: virtual IP packet size in bits.
//--------------------------------------------------------------------------------------------------
class traffic_model_base
{
public:
    traffic_model_base( int id,
                        float _ul_target, 
                        float _dl_target, 
                        float _var_perc,
                        int _pkt_size, 
                        bool _random_v)
    :uniform_gen(rng_seed(_random_v, RNG_TRAFFIC, (std::uint64_t)id))
    {
        ul_target = _ul_target; 
        dl_target = _dl_target; 
        pkt_size = _pkt_size; 
        var_percent = _var_perc; 
    }
    traffic_model_base(){}
    
public:
    float generate(int tx, double t)
    {
		assert(tx == T_DL || tx == T_UL); 
        if(tx == T_DL) return generate_dl(t);
        else return generate_ul(t); 
    }

    virtual float generate_ul(double t)
    {
        if(t > past_t_ul && past_t_ul != 0)
        {
            double t_diff = t - past_t_ul; 
            past_t_ul = t; 
            float dist = uniform_dist(uniform_gen);
            return ul_target*t_diff*(1 + dist*var_percent); 
        }
        past_t_ul = t;
        return 0.0; 
    }

    virtual float generate_dl(double t)
    {
        if(t > past_t_dl && past_t_dl != 0)
        {
            double t_diff = t - past_t_dl; 
            past_t_dl = t; 
            float dist = uniform_dist(uniform_gen);
            return dl_target*t_diff*(1 + dist*var_percent); 
        }
        past_t_dl = t; 
        return 0.0; 
    }

    int get_pkt_size(int tx)
    {
        return pkt_size;
    }

    // Runtime control setter. Target rate in bits per second, the internal unit; the Mbps
    // of the .ini and of the control channel are converted by the caller.
    virtual void set_target(int tx, float bps)
    {
        assert(tx == T_DL || tx == T_UL);
        if(tx == T_DL) dl_target = bps;
        else ul_target = bps;
    }

    virtual float get_target(int tx) const
    {
        return tx == T_DL ? dl_target : ul_target;
    }
protected: 
    float ul_target; 
    float dl_target; 
    float ul_bits; 
    float dl_bits; 
    int pkt_size;  
    float var_percent; 
    double past_t_ul = 0;
    double past_t_dl = 0;

protected: 
    std::mt19937 uniform_gen;
    std::uniform_real_distribution<float> uniform_dist{-1,1};
 
};
#endif
