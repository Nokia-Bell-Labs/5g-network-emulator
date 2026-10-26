/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef MOBILITY_MODEL_BASE_H
#define MOBILITY_MODEL_BASE_H

#include <math.h>
#include <random>
#include <memory>
#include <utils/rng_seed.h>
#include <chrono>
#include <iostream>
#include <functional>
#include <mobility_models/pos2d.h>
#include <mobility_models/mobility_config.h>

//--------------------------------------------------------------------------------------------------
// mobility_model_base(): is a base class that the custom implemented mobility models classes
// override. This base class simply does not update the position: is a static user model. The 
// initial position can be randomized or manually selected by the user using the configuration file.
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      _init_pos: initial position, only used if _random_init is set to false.
//      _random_init: wether to randomly initialized or not
//      _speed: target speed of the UEs in km/h.
//      _speed_var: size of the white noise to be applied in each timestep to the target speed.
//      _max_distance: max. distance of the UE to the gNB. 
//      _time_target: target time used in some of the implemented models, such as random walk model.
//      _time_target_var: size of the white noise to be applied in each timestep to the target time.
//--------------------------------------------------------------------------------------------------

class mobility_model_base
{
public: 
    mobility_model_base(int id, pos2d _init_pos, bool _random_init, float _speed, float _speed_var,
                    float _max_distance, float _time_target, float maxApothem,
                    float _time_target_var, bool _random_v);
                    
    mobility_model_base(int it, mobility_config mobility_c,float maxApothem);
    mobility_model_base(){}
    
protected: 
    std::mt19937 uniform_gen;
    std::uniform_real_distribution<float> uniform_dist{-1,1};

public: 
    virtual void update_pos(float current_t);
    void set_random_initial_position(float max_apothem);
    float get_current_d();
    void get_pos(float &x, float &y);
    pos2d * get_pos();
    float get_max_speed(){ return speed + speed_var; }
    float x();
    float y();

    // Runtime control setters. Position is input state, not derived, so assigning it is
    // legitimate; the models rebuild whatever they cache from it on the next update_pos().
    virtual void set_pos(float x, float y)
    {
        const float cx = std::min(fabs(x), max_apothem) * (x < 0 ? -1 : 1);
        const float cy = std::min(fabs(y), max_apothem) * (y < 0 ? -1 : 1);
        current_pos = pos2d(cx, cy);
        current_distance = current_pos.get_distance();
    }
    // Speed in m/s, the internal unit. The km/h of the .ini and of the control channel is
    // converted by the caller with the same TOMS constant the loader uses.
    void set_speed(float speed_ms) { speed = speed_ms; }
    float get_speed() const { return speed; }
    float get_apothem() const { return max_apothem; }
   

protected: 
    float new_speed()
    {
        return uniform_dist(uniform_gen)*speed_var;
    }
protected: 
    pos2d current_pos; 
    pos2d target_pos; 
    float speed;
    float speed_var; 
    pos2d current_dir; 
    float current_distance; 
    float max_distance; 
    float max_apothem;
    float time_target; 
    float time_target_var; 
    float t_target; 
    float past_t; 
  
};

#endif