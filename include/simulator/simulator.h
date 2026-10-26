/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <thread>
#include <iostream>
#include <cstdint>
#include <functional>

#include <simulator/configuration_loader.h>
#include <simulator/controller_registry.h>
#include <ue/ue_handler.h>
#include <mac_layer/mac_layer.h>
#include <timer/timer.h>
#include <utils/control/control_manager.h>
#include <utils/run_status.h>
#include <utils/terminal_logging.h>

class simulator
{
public:
    simulator(std::string config_file);

public: 

    void override_duration(double _duration);

    void start();

    void join();
    
    void terminate();

    void print_traffic();

    int exit_code() const { return exit_status(exit_code_); }
    run_stop_reason stop_reason() const { return stop_reason_; }

    // One simulation step. Public so that tests can drive the loop without the
    // timer thread; the emulator itself still gets here through timer::start().
    // _ts is simulated time in microseconds, 64-bit: see the note in timer.h.
    void step(std::uint64_t _ts);

    // Runs n steps back to back in the calling thread, with the same simulated
    // time base as the fast mode of the ticker (1 ms per step).
    void run_steps(unsigned int n);

    // For tests: the UE list is final after the constructor returns.
    std::vector<ue>* ue_list() { return ue_h.get_ue_list(); }
    control_manager& control_plane() { return control; }
    // For tests: number of calls to step() so far.
    std::uint64_t steps_done() const { return total_steps; }

private: 

    void handle_time_log(double sim_time_s);
    void log_runtime_start();
    void log_runtime_stop(const std::string &reason);
    void maybe_log_progress(double sim_time_s);

private: 
    configuration_loader config_loader; 
    ue_handler ue_h; 
    mac_layer mac_l;
    control_manager control;
    // Built at the end of the constructor from the controller registry, called from step().
    std::vector<controller_instance> controllers;
private: 
    int freq = 1000; 
    int step_c = 0; 
    std::uint64_t total_steps = 0;
    std::chrono::duration<double> time_mac = std::chrono::duration<double>().zero(); 
    std::chrono::duration<double> time_ue = std::chrono::duration<double>().zero(); 
    std::uint64_t progress_steps = 0;
    std::chrono::duration<double> progress_time_mac = std::chrono::duration<double>().zero();
    std::chrono::duration<double> progress_time_ue = std::chrono::duration<double>().zero();
    float total_time_mac; 
    float total_time_ue; 
private: 
    timer ticker; 
private: 
    double duration;
private: 
    int verbosity = 0; 
    bool runtime_started = false;
    bool runtime_stopped = false;
    run_exit_code exit_code_ = run_exit_code::ok;
    run_stop_reason stop_reason_ = run_stop_reason::none;
    std::chrono::steady_clock::time_point runtime_start_tp;
    double progress_log_period_s = 0.0;
    double next_progress_log_sim_time_s = 0.0;

};
#endif
