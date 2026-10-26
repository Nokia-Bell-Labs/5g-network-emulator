/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

class thread_pool
{
    
public:

    thread_pool() : shutdown_ (false){}

    ~thread_pool ()
    {
        {
            // Unblock any threads and tell them to stop
            std::unique_lock <std::mutex> l (lock_);

            shutdown_ = true;
            condVar_.notify_all();
        }
        // Wait for all threads to stop
        std::cerr << "Joining threads" << std::endl;
        for (auto& thread : threads_)
            thread.join();
    }

    void init(int _num_threads)
    {
        // Create the specified number of threads
        goal = 0; 
        threads_.reserve (_num_threads);
        for (int i = 0; i < _num_threads; ++i)
            threads_.emplace_back (std::bind (&thread_pool::thread_entry, this, i));
        is_init = true; 
    }

    void do_job (std::function <void (void)> func)
    {
        // Place a job on the queu and unblock a thread
        std::unique_lock <std::mutex> l (lock_);
        goal++;
        jobs_.push_back (std::move (func));
        //condVar_.notify_one();
    }

    void do_jobs()
    {
        // Under the lock: the workers read isRunning, index and busy while holding it, and
        // publishing them without it leaves no happens-before edge with whatever the
        // caller wrote before starting the round. That matters beyond these three fields:
        // the runtime control plane mutates UE state right before this call, precisely
        // because the pools are quiescent, and it is the release of this mutex that makes
        // those writes visible to the workers.
        {
            std::unique_lock <std::mutex> l (lock_);
            if(isRunning) return;
            isRunning = true;
            index = 0;
            busy = goal;
        }
        condVar_.notify_all();
    }
    void wait_threads()
    {
        std::unique_lock<std::mutex> lock(lock_);
        condFinished_.wait(lock, [this](){ return !isRunning || jobs_.empty(); });
    }

protected:

    void thread_entry (int i)
    {
        std::function <void (void)> job;
        bool is_job = false; 
        while (1)
        {
            {
                std::unique_lock <std::mutex> l (lock_);

                while (! shutdown_ && (!isRunning || index >= goal ||jobs_.empty()))
                {
                    condVar_.wait (l);
                }

                if ( !isRunning || index >= goal ||jobs_.empty())
                {
                    // No jobs to do and we are shutting down
                    return;
                }
                if(index + 1 <= goal)
                {
                    is_job = true; 
                    job = jobs_[index];
                    index++;
                }
                
            }

            // Do the job without holding any locks
            if(is_job) 
            {
                job ();
                std::unique_lock <std::mutex> l (lock_);
                busy--; 
                if(busy == 0)
                {
                    condFinished_.notify_one(); 
                    isRunning = false;
                }
            }
        }

    }

    // Declaration of thread condition variable
 public:
    int index; 
    int goal; 
    int busy = 0; 
    bool is_init = false; 
    std::mutex lock_;
    std::condition_variable condVar_;
    std::condition_variable condFinished_;
    bool shutdown_;
    std::deque <std::function <void (void)>> jobs_;
    std::vector <std::thread> threads_;
    bool isRunning = false; 
};
#endif