/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <utils/control/command.h>

class ue;

enum class param_type
{
    number,
    boolean,
    // A free-form UTF-8 string, bounded by param_entry::max_length. It exists so that a
    // research extension can take structured input (a small JSON document, say) through
    // the same set operation as any other knob; the built-in catalogue has none.
    text
};

// Default bound of a text value, in bytes. The value goes to the journal verbatim, so
// the bound is what keeps a journal line from growing without limit.
constexpr std::size_t PARAM_TEXT_MAX_LENGTH = 4096;

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
    // Only for text: the longest accepted value, in bytes of UTF-8.
    std::size_t max_length = PARAM_TEXT_MAX_LENGTH;

    // Returns false with a reason when the value cannot be applied to this particular UE,
    // e.g. a traffic target on a UE whose source is a captured queue.
    std::function<bool(ue &, const param_value &, std::string &)> apply;

    // Reads the current value back for the get operation.
    std::function<double(ue &)> read;

    // Same, for a text knob (read is for number and boolean). Optional: a knob without a
    // reader is simply absent from the state that get reports.
    std::function<std::string(ue &)> read_text;
};

class param_registry
{
public:
    static const param_registry &instance();

    // Research extension point: appends a knob after the built-in ones. Meant to be
    // called from file-scope static objects of other translation units, so the pending
    // list is a function-local static and does not depend on the order in which those
    // objects are initialised. The constructor registers the built-in catalogue first,
    // in its fixed order, and then drains the pending list in call order.
    //
    // Throws std::invalid_argument when the entry is malformed (see below) or its name
    // is already taken by a built-in or by an earlier add, and std::logic_error when
    // the registry has already been built: the catalogue is immutable from then on. From
    // a static registrar either one ends the process at start-up, which is intended.
    //
    // Names are lower case letters, digits, '_' and '.', start with a letter and have no
    // empty segment, so that they are valid keys of a set message. The rest of the
    // naming rules above is the registrar's to follow; it cannot be checked here.
    static void add(param_entry entry);

    const param_entry *find(const std::string &name) const;
    const std::vector<param_entry> &entries() const { return entries_; }

    // Checks type and range without touching anything. reason is filled on failure.
    bool check(const param_entry &e, const param_value &v, std::string &reason) const;

    // Current value of a knob as a param_value of the type the knob declares: bool for
    // boolean, std::string for text, double for number. False when the entry has no
    // reader.
    bool read_value(const param_entry &e, ue &u, param_value &out) const;

    // The catalogue as JSON, which is what the describe operation returns and what the
    // API uses to validate before forwarding.
    std::string describe_json() const;

private:
    param_registry();
    std::vector<param_entry> entries_;
};
