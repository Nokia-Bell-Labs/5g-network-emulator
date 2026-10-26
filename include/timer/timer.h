/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <iostream>

#include <utils/conversions.h>
#include <utils/terminal_logging.h>

#define S2US 1000000
#define US2S 0.000001

//--------------------------------------------------------------------------------------------------
// timer(): simple ticker module in charge of syncrhonizing the emulator, triggering a signal
// every [period] milliseconds. 
// Input: 
//      _period: of the signal triggering, in ms. If set to <0, simulation mode, the emulator runs
//               as fast as possible.
//      _goal: duration of the simulation
//
// Every timestamp the timer produces is a 64-bit count of microseconds. A 32-bit count wraps
// after 2^32 us = 71.6 minutes, and the emulator takes it as simulated time: the wrap sent
// that time back to zero and a live run stopped forwarding traffic for good. The same goes
// for the products period * (count + 1) and S2US * goal, which overflowed an int after 35
// minutes of real-time run with a finite duration.
//--------------------------------------------------------------------------------------------------
class timer
{
public: 
    using ts_us_t = std::uint64_t;

    timer(double _period, double _goal = -1)
    {
        if(_period > 0)
        {
            period = (std::int64_t)(_period*1000); 
            do_wait = true; 
        }
        else
        {
            do_wait = false; 
        }
        goal = (std::int64_t)((double)S2US * (double)_goal); 
        LOG_INFO_I("timer::ticker") << "Period: " << period << " Goal: " << goal << END(); 
    }

    ~timer()
    {
        stop();
    }

public: 
    // already_elapsed is for tests: the timestamps start there instead of at zero, so that a
    // test can cross 2^32 us without waiting 71 minutes. The emulator always starts at zero.
    void start(std::function<void(ts_us_t)> _func,
               std::chrono::microseconds already_elapsed = std::chrono::microseconds::zero())
    {
        {
            init_t = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                - already_elapsed;
            base_ts = (ts_us_t)already_elapsed.count();
            current_ts = base_ts;
            previous_ts = base_ts;
            func = _func; 
            run = true; 
            if(goal < 0)
            {
                tickthread = std::thread(&timer::ticker_inf, this);
            }
            else
            {
                tickthread = std::thread(&timer::ticker, this);
            }           
        }
    }

    // Safe to call more than once, and after request_stop: what decides whether there is
    // a thread to join is the thread, not the run flag. Joining on the flag left a
    // finished thread unjoined when the loop had ended on its own, and destroying a
    // joinable thread terminates the process.
    void stop()
    {
        run = false;
        if(tickthread.joinable()) tickthread.join();
    }

    // Asks the loop to finish without joining, so that it can be called from inside the
    // ticker thread itself, which is where simulator::step runs.
    void request_stop()
    {
        run = false;
    }

    std::chrono::microseconds * get_init_t()
    {
        return &init_t; 
    }

    void wait_finished()
    {
        if(tickthread.joinable()) tickthread.join();
    }
    
    ts_us_t get_current_ts()
    {
        return (ts_us_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch() - init_t).count(); 
    }

private: 
    void ticker()
    {
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        std::int64_t count = 0; 
        //LOG_INFO_I("timer::ticker") << "FIST Run: " << run << " previous_ts: " << previous_ts << " goal: " << goal << " do_wait: " << do_wait << END(); 
        while(run && ((std::int64_t)previous_ts < goal))
        {
            //LOG_INFO_I("timer::ticker") << "Run: " << run << " previous_ts: " << previous_ts << " goal: " << goal << " do_wait: " << do_wait << END(); 
            if(do_wait)
            {
                current_ts = get_current_ts(); 
                func(current_ts); 
                const std::int64_t next_tick = (std::int64_t)base_ts + period*(count+1);
                const std::int64_t wait = std::max<std::int64_t>(0, next_tick - (std::int64_t)get_current_ts());
                std::this_thread::sleep_for(std::chrono::microseconds(wait));
                previous_ts = current_ts; 
                count++;
            }
            else
            {
                func((ts_us_t)count*TTI_US);
                previous_ts = (ts_us_t)count*TTI_US; 
                count++;               
            }
        }
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - t);
        if(do_wait) LOG_INFO_I("timer::ticker") << "Simulation time: " << goal*US2S << " - actual time: " << time_span.count() << END(); 
        else  LOG_INFO_I("timer::ticker") << "Simulation time: " << count*TTI_S << " - actual time: " << time_span.count() << END(); 
        LOG_INFO_I("timer::ticker")  << "Total iterations: " << count << " mean step time: " << time_span.count()/count << END(); 
   }

    void ticker_inf()
    {
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        std::int64_t count = 0; 
        while(run)
        {
            if(do_wait)
            {
                current_ts = get_current_ts(); 
                func(current_ts); 
            
                const std::int64_t diff = period - (std::int64_t)(current_ts - previous_ts); 
                const std::int64_t sign = (diff > 0) ? 1 : -1 ; 
                const std::int64_t wait = std::max<std::int64_t>(
                    0, period - sign*diff - (std::int64_t)(get_current_ts() - current_ts));
                std::this_thread::sleep_for(std::chrono::microseconds(wait));
                previous_ts = current_ts; 
                count++;
            }
            else
            {
                func((ts_us_t)count*TTI_US);
                previous_ts = (ts_us_t)count*TTI_US; 
                count++;                  
            }
        }
        std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - t);
        if(do_wait)  LOG_INFO_I("timer::ticker") << "Simulation time: " << goal*US2S << " - actual time: " << time_span.count() << END(); 
         LOG_INFO_I("timer::ticker") << "Simulation time: " << count*TTI_S << " - actual time: " << time_span.count() << END(); 
         LOG_INFO_I("timer::ticker") << "Total iterations: " << count << " mean step time: " << time_span.count()/count << END(); 
    }
private: 

    std::int64_t period = 0;
    std::thread tickthread;  
    std::function<void(ts_us_t)> func; 
    std::chrono::microseconds init_t{0};

    ts_us_t base_ts = 0; 
    ts_us_t current_ts = 0; 
    ts_us_t previous_ts = 0; 
    std::atomic<bool> run{false};
    std::int64_t goal = 0; 
    bool do_wait = false; 
};