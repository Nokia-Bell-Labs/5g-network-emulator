/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/
#pragma once
#ifndef MOBILITY_MODEL_H
#define MOBILITY_MODEL_H

#include <memory>

#include <mobility_models/mobility_config.h>

class mobility_model_base;

#define MIN_DISTANCE 10

//--------------------------------------------------------------------------------------------------
// mobility_model(): class which selects the mobility model which will be used to estimate a new
// position for the associated UE in each time step.
// Input:
//      id: unique id of the UE used to initialize the random generator's seed.
//      type: id of the desired mobility model for the current UE.
//      _init_pos: initial position, only used if _random_init is set to false.
//      _random_init: wether to randomly initialized or not
//      _speed: target speed of the UEs in km/h.
//      _speed_var: size of the white noise to be applied in each timestep to the target speed.
//      _max_distance: max. distance of the UE to the gNB.
//      _time_target: target time used in some of the implemented models, such as random walk model.
//      _time_target_var: size of the white noise to be applied in each timestep to the target time.
//--------------------------------------------------------------------------------------------------
class mobility_model
{
public:
    mobility_model(int id, mobility_config mobility_c, int scenario, float maxApothem);
    ~mobility_model();
    mobility_model(mobility_model&&) noexcept;
    mobility_model& operator=(mobility_model&&) noexcept;
    mobility_model(const mobility_model&) = delete;
    mobility_model& operator=(const mobility_model&) = delete;

public:
    void update_pos(float t);
    void set_initial_position(float _max_apothem);
    float get_distance();

    // Runtime control setters. Position in metres, speed in m/s.
    void set_pos(float x, float y);
    void set_speed(float speed_ms);
    float get_speed();
    float get_apothem();

    void get_pos(float &x, float &y);

    pos2d *get_pos();

    float get_max_speed();

    float x();

    float y();

private:
    void limit_distance(float &distance, int scenario);
    void limit_speed(float &speed, int scenario);

private:
    std::unique_ptr<mobility_model_base> mob_model;
};

#endif
