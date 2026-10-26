/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef LINEAR_DRIFT_MODEL_H
#define LINEAR_DRIFT_MODEL_H

#include <mobility_models/mobility_model_base.h>

//--------------------------------------------------------------------------------------------------
// linear_drift_model(): is an overriding class which implements a mobility model where the UE 
// moves linearly away from the center (0,0). The direction is recalculated at each timestep to
// ensure the UE moves radially outwards, causing the distance from the origin to grow linearly.
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      _init_pos: initial position used to define the starting point.
//      _random_init: whether to initialize the position randomly or not.
//      _speed: target speed of the UEs in km/h.
//      _speed_var: size of the white noise to be applied in each timestep to the target speed.
//      _max_distance: max. distance of the UE to the gNB.
//      _time_target: target time used in some of the implemented models.
//      _time_target_var: size of the white noise to be applied in each timestep to the target time.
//--------------------------------------------------------------------------------------------------
class linear_drift_model : public mobility_model_base
{
public:
    linear_drift_model(int id, pos2d _init_pos, bool _random_init, float _speed, float _speed_var, 
                       float _max_distance, double _time_target, float maxApothem, 
                       double _time_target_var, bool _random_v)
    :mobility_model_base(id, _init_pos, _random_init, _speed, _speed_var, _max_distance, _time_target, maxApothem, _time_target_var, _random_v) {}

public:
    void update_pos(double current_t)
    {
        if (current_t >= t_target)
        {
            float var = new_speed();
            speed += var;
            t_target += time_target + abs(var * time_target_var);
        }

        if (past_t > 0)
        {
            const float t_step = static_cast<float>(current_t - past_t);  // small step: narrow after the double subtraction

            pos2d direction = current_pos / current_pos.get_distance();

            pos2d next_pos = current_pos + direction * (t_step * speed);

            if (next_pos.get_distance() <= max_distance && 
                fabs(next_pos._x()) <= max_apothem && fabs(next_pos._y()) <= max_apothem)
            {
                current_pos = next_pos;
            }
            else
            {
                speed = 0.0f; 
            }
        }

        past_t = current_t;
    }
};

#endif
