/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <string>

//--------------------------------------------------------------------------------------------------
// cell_info(): read only description of the run, answered by a get on the cell target.
//
// It exists so that a client can configure itself instead of being told: the dashboard needs the
// apothem to scale its map, and metric_type to warn that priority does nothing under round robin,
// which is the easiest mistake to make with this control plane.
//
// Filled once by simulator, from configuration_loader and mac_layer. Nothing here changes during
// a run.
//--------------------------------------------------------------------------------------------------
struct cell_info
{
    int scenario_type = -1;
    double frequency_hz = 0.0;
    int bandwidth_hz = 0;
    int numerology = 0;
    int n_freq_rbg = 0;
    int metric_type = -1;
    float period_ms = -1.0f;
    float duration_s = 0.0f;
    // Path of the macroscopic fading map of this scenario, resolved from the binary location by
    // configuration_loader. A client can read it to paint the map behind a trajectory.
    std::string map_file;
};
