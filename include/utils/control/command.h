/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// A parameter value as it arrives over the wire. The registry decides what a given knob
// accepts and converts; nothing below this point guesses.
using param_value = std::variant<double, bool, std::string>;

enum class command_op
{
    set,
    get,
    describe,
    ping,
    // Time credit for the barrier mode. Absolute and monotonic: until_tti is the last TTI
    // the emulator is allowed to run. Handled apart from the rest because it is the only
    // command the transport thread applies by itself; see 03-credit-protocol.
    grant,
    // Injection of bytes belonging to one object. Apart from set because it carries a
    // tag, which is not a knob: it does not describe the UE, it labels the bits.
    inject,
    // Releases an object's counters. The emulator cannot know that an object is
    // finished, because it does not know its size; whoever injected it does.
    forget,
    // Incremental object feedback for a transport model. Cell-wide: returns only the
    // counter movements after a client-confirmed cursor, not every live object.
    events
};

//--------------------------------------------------------------------------------------------------
// command(): one unit of work for the control plane, already parsed and validated for
// shape but not yet for content.
//
// target: "ue/<id>", "ue/*" or "cell".
// at_tti: TTI at which it must be applied. Negative means "as soon as it is drained".
//         Canonical identifier for replay: in fast mode the TTI is exactly the ticker
//         counter, which at_t (a double) cannot guarantee.
// id:     correlation id chosen by the client, echoed back in the ack.
//--------------------------------------------------------------------------------------------------
struct command
{
    command_op op = command_op::set;
    std::string target;
    std::vector<std::pair<std::string, param_value>> sets;
    double at_t = -1.0;
    std::int64_t at_tti = -1;
    std::uint64_t id = 0;
    // Internal socket generation, never serialized. Commands and acknowledgements
    // must not cross from a disconnected controller into its successor.
    std::uint64_t connection_generation = 0;

    // Only meaningful for grant.
    std::int64_t until_tti = -1;

    // Only meaningful for inject and forget. 0 is the generator's traffic.
    std::uint32_t tag = 0;
    // Only meaningful for inject: which direction, how many bytes, and what the
    // packets declare in the ECN field. Not-ECT by default, so a client that says
    // nothing gets what it always got.
    int tx_dir = -1;
    double bytes = 0.0;
    std::uint8_t ecn = 0;   // ECN_NOT_ECT

    // Only meaningful for events. `after` is the last cursor the client consumed; the
    // server retains later events so retrying the same request is idempotent.
    std::uint64_t after = 0;
    bool include_state = false;
    bool resync_events = false;
};

struct ack_error
{
    std::string key;
    std::string reason;
};

struct ack
{
    std::uint64_t id = 0;
    std::uint64_t connection_generation = 0; // internal, not serialized
    bool ok = true;
    std::int64_t tti = 0;
    double t = 0.0;
    std::vector<ack_error> errors;
    // Credit in force after applying the message; lets a client notice that it granted
    // into the past without needing an error code.
    std::int64_t credit_until_tti = -1;
    // Free-form payload for get/describe, already serialized as JSON by the manager.
    std::string payload;
};
