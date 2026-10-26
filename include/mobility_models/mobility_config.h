/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once


#define STATIC_MODEL 0
#define RANDOM_WALK_MODEL 1
#define RANDOM_WAYPOINT_MODEL 2
#define RANDOM_MANHATTAN_MODEL 3
#define LINEAR_DRIFT_MODEL 4
#define CLOCKWISE_MODEL 5


#include <mobility_models/pos2d.h>
#include <map/map_handler.h>
//--------------------------------------------------------------------------------------------------
// mobility_config(): struct storing all the configuration information related with the mobility
// models. 
// Input: 
//      _type: id of the desired mobility model for the current UE. 
//      _x,_y: initial position only used if _random_init is set to false.
//      _random_init: wether to randomly initialized or not
//      _speed: target speed of the UEs in km/h.
//      _speed_var: size of the white noise to be applied in each timestep to the target speed.
//      _max_distance: max. distance of the UE to the gNB. 
//      _MapRoute: it is the route to the map, to create the object MapHandler. 
//      _time_target: target time used in some of the implemented models, such as random walk model.
//      _time_target_var: size of the white noise to be applied in each timestep to the target time.
//--------------------------------------------------------------------------------------------------
struct mobility_config
{
    mobility_config(){}
    mobility_config(int _type, float _x, float _y, bool _random_init, float _speed, float _speed_var,
                    float _max_distance, double _time_target, 
                    double _time_target_var):
                    init_pos(_x, _y)
                    {
                        type = _type; 
                        random_init = _random_init; 
                        speed = _speed; 
                        speed_var = _speed_var; 
                        max_speed = speed + speed_var; 
                        max_distance = _max_distance; 
                       
                        time_target = _time_target; 
                        time_target_var = _time_target_var; 
                    }
    int type; 
    pos2d init_pos; 
    bool random_init; 
    float speed; 
    float speed_var; 
    float max_speed; 
    float max_distance; 
    double time_target; 
    double time_target_var; 
    bool random_v = false; 
    float get_max_speed(){ return speed + speed_var;}
     
};