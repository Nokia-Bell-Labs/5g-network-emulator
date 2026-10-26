/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <utils/control/command.h>

class ue;

enum class param_type
{
    number,
    boolean
};

//--------------------------------------------------------------------------------------------------
// param_entry(): one knob of the catalogue.
//
// Naming rules (normative, section 4 of the plan):
//   - unit suffix always, unless the knob is dimensionless (priority) or boolean (enabled);
//   - the unit is the one of the .ini, never the internal one: speed_kmh takes km/h even
//     though the state is in m/s, and the applier converts;
//   - directional knobs carry a dl. / ul. prefix and have no unprefixed form;
//   - subsystem prefix (mobility., traffic.) when the parameter belongs to one;
//   - the leaf name is the .ini key plus the unit suffix.
//
// The applier receives the ue and the already validated value. It runs from
// control_manager::tick(), at the quiescent point, never from the hot path: this whole
// table is on the write side.
//--------------------------------------------------------------------------------------------------
struct param_entry
{
    std::string name;
    param_type type = param_type::number;
    std::string unit;          // empty when dimensionless
    double min = 0.0;
    double max = 0.0;
    bool bounded = true;
    // Adds to the current value instead of replacing it. The only one is inject_bytes,
    // and it is flagged here so that describe can warn a client that retrying is not
    // free.
    bool incremental = false;
    std::string description;

    // Returns false with a reason when the value cannot be applied to this particular UE,
    // e.g. a traffic target on a UE whose source is a captured queue.
    std::function<bool(ue &, const param_value &, std::string &)> apply;

    // Reads the current value back for the get operation.
    std::function<double(ue &)> read;
};

class param_registry
{
public:
    static const param_registry &instance();

    const param_entry *find(const std::string &name) const;
    const std::vector<param_entry> &entries() const { return entries_; }

    // Checks type and range without touching anything. reason is filled on failure.
    bool check(const param_entry &e, const param_value &v, std::string &reason) const;

    // The catalogue as JSON, which is what the describe operation returns and what the
    // API uses to validate before forwarding.
    std::string describe_json() const;

private:
    param_registry();
    std::vector<param_entry> entries_;
};
