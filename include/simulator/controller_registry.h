/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <ue/ue.h>
#include <utils/control/cell_info.h>

//--------------------------------------------------------------------------------------------------
// controller registry: a research extension point for per-TTI logic that observes the cell.
//
// A controller is an object that simulator::step() calls once per TTI, on the simulator thread,
// at the quiescent point: after control_manager::tick() and the stop check, and before
// mac_l.step(). Both worker thread pools are parked there, so a controller reads the UE list
// without locks, and what it sees is the complete result of the previous TTI. It runs whether or
// not the control plane is enabled.
//
// A controller observes: it writes its own state and may publish telemetry, but it does not
// write the state of the UEs or of the MAC. Anything that has to reach the UEs goes through the
// control plane.
//
// With nothing registered the list is empty and the step is exactly what it was without this
// file.
//--------------------------------------------------------------------------------------------------

struct tick_context
{
    double sim_t;           // simulated time of this TTI, in seconds
    std::uint64_t tti;      // TTI number, 0 for the first step of the run
    // Read-only view of the UE list. Do not keep a pointer or a reference to an element across
    // calls: ue_handler grows a std::vector<ue>, and a reallocation moves every element. Key
    // any state of your own by UE id and look the UE up again at each tick. The id is the index
    // in this vector (ids are handed out in order); ue::get_id() returns it, and
    // ue::get_control_id() is the same id as text, or the name given in the configuration.
    const std::vector<ue> &ues;
};

class controller
{
public:
    virtual ~controller() = default;

    // Called once per TTI. An exception ends the run: simulator::step() adds the controller name
    // to the message and lets it propagate, like any other fatal runtime error.
    virtual void on_tick(const tick_context &ctx) = 0;
};

// Builds a controller when the simulator is constructed, after the UE list is final. The
// factory gets the path of the configuration file, so that a controller can read a section of
// its own, the description of the cell and the UE list (for the same reallocation reason as
// above, it must not keep pointers into it). Returning nullptr means "not active in this run".
using controller_factory = std::function<std::unique_ptr<controller>(
    const std::string &config_file, const cell_info &cell, const std::vector<ue> &ues)>;

// Appends a factory to the list that every simulator built afterwards instantiates, in
// registration order. Typically called from a file-scope registrar object. Throws
// std::invalid_argument for an empty name, an empty factory or a name already registered.
//
// The list lives in a function-local static, so a registrar in any translation unit can call
// this during static initialisation, whatever the link order.
void register_controller(std::string name, controller_factory factory);

// A controller that has been built, with the name it was registered under.
struct controller_instance
{
    std::string name;
    std::unique_ptr<controller> impl;
};

// Names of the registered controllers, in registration order.
std::vector<std::string> registered_controller_names();

// Used by simulator: runs every registered factory, in registration order, and returns the
// controllers they produced (a factory that returns nullptr is skipped). With an empty registry
// it returns an empty vector. A factory that throws propagates, so a bad configuration stops the
// start-up.
std::vector<controller_instance> instantiate_controllers(
    const std::string &config_file, const cell_info &cell, const std::vector<ue> &ues);

// For tests: forgets every registration, so that a test can start from the default state.
void clear_registered_controllers();
