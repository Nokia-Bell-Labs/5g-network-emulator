/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <stdexcept>
#include <string>

// Stable process statuses, following the traditional BSD sysexits meanings without
// depending on <sysexits.h> (which is common on Unix but not part of C++ or POSIX).
enum class run_exit_code : int
{
    ok = 0,
    usage = 64,          // EX_USAGE
    no_input = 66,       // EX_NOINPUT
    software = 70,       // EX_SOFTWARE
    io_error = 74,       // EX_IOERR
    temporary_failure = 75, // EX_TEMPFAIL
    protocol = 76,       // EX_PROTOCOL
    config = 78          // EX_CONFIG
};

enum class run_stop_reason
{
    none,
    completed,
    terminated,
    credit_timeout,
    control_peer_lost,
    object_event_backlog,
    runtime_error
};

inline int exit_status(run_exit_code code)
{
    return static_cast<int>(code);
}

inline const char *run_stop_reason_name(run_stop_reason reason)
{
    switch (reason)
    {
    case run_stop_reason::completed: return "completed";
    case run_stop_reason::terminated: return "terminated";
    case run_stop_reason::credit_timeout: return "credit_timeout";
    case run_stop_reason::control_peer_lost: return "control_peer_lost";
    case run_stop_reason::object_event_backlog: return "object_event_backlog";
    case run_stop_reason::runtime_error: return "runtime_error";
    default: return "none";
    }
}

inline run_exit_code run_stop_exit_code(run_stop_reason reason)
{
    switch (reason)
    {
    case run_stop_reason::completed: return run_exit_code::ok;
    case run_stop_reason::credit_timeout: return run_exit_code::temporary_failure;
    case run_stop_reason::control_peer_lost: return run_exit_code::io_error;
    case run_stop_reason::object_event_backlog: return run_exit_code::protocol;
    case run_stop_reason::terminated:
    case run_stop_reason::runtime_error:
    case run_stop_reason::none:
    default: return run_exit_code::software;
    }
}

class run_failure : public std::runtime_error
{
public:
    run_failure(run_exit_code code, const std::string &message)
        : std::runtime_error(message), code_(code) {}

    run_exit_code code() const { return code_; }

private:
    run_exit_code code_;
};
