/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef MANHATTAN_MODEL_H
#define MANHATTAN_MODEL_H

#include <vector>
#include <mobility_models/mobility_model_base.h>

#define DIR_UP pos2d(0, 1)
#define DIR_DOWN pos2d(0, -1)
#define DIR_RIGHT pos2d(1, 0)
#define DIR_LEFT pos2d(-1, 0)
//--------------------------------------------------------------------------------------------------
// manhattan_model(): is an overriding class which implementes a manhattan mobility model. The model
// is simple, randomly selecting a new direction from the main four cardinal directions every time the 
// elapsed time is greater than the target time.
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
class manhattan_model : public mobility_model_base
{
public: 
    manhattan_model(int id, pos2d _init_pos, bool _random_init, float _speed, float _speed_var,
                    float _max_distance, double _time_target, float _maxApothem,
                    double _time_target_var, bool _random_v)
    :mobility_model_base(id, _init_pos, _random_init, _speed, _speed_var, _max_distance, _time_target, _maxApothem, _time_target_var, _random_v){}
public: 
    void update_pos(double current_t)
    {
       
        if(current_t >= t_target)
        {
            float var = new_speed(); 
            speed += var;
            t_target += time_target + abs(var*time_target_var);
            current_dir = dirs[list_dist(uniform_gen)];
        }
        if(past_t > 0)
        {
            const float t_step = static_cast<float>(current_t - past_t);  // small step: narrow after the double subtraction 
            pos2d next_pos = current_pos + current_dir * ( t_step * speed );

            if(  next_pos.get_distance() <= max_distance && abs(next_pos._x()) <= max_apothem && abs(next_pos._y())<=max_apothem)
            {
                current_pos = next_pos; 
            }
            else
            {
                current_dir = dirs[list_dist(uniform_gen)];
            }
        }
        past_t = current_t;
    }
private: 
    std::uniform_int_distribution<int> list_dist{0, 3};

private: 
    const std::vector<pos2d> dirs { DIR_UP, DIR_DOWN, DIR_RIGHT, DIR_LEFT};
};

#endif