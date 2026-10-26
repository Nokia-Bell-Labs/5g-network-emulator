/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>
#include <simulator/configuration_loader.h>
#include <ue/ue.h>
#include <utils/control/param_registry.h>

using json = nlohmann::json;

namespace
{
const double UNBOUNDED = std::numeric_limits<double>::infinity();  // mac_definitions.h ya usa el nombre INF

bool value_as_double(const param_value &v, double &out)
{
    if (const double *d = std::get_if<double>(&v)) { out = *d; return true; }
    return false;
}

bool value_as_bool(const param_value &v, bool &out)
{
    if (const bool *b = std::get_if<bool>(&v)) { out = *b; return true; }
    return false;
}

param_entry number(const std::string &name, const std::string &unit, double min, double max,
                   const std::string &description,
                   std::function<bool(ue &, double, std::string &)> setter,
                   std::function<double(ue &)> reader)
{
    param_entry e;
    e.name = name;
    e.type = param_type::number;
    e.unit = unit;
    e.min = min;
    e.max = max;
    e.bounded = true;
    e.description = description;
    e.read = reader;
    e.apply = [setter](ue &u, const param_value &v, std::string &reason) {
        double d = 0.0;
        if (!value_as_double(v, d)) { reason = "expected a number"; return false; }
        return setter(u, d, reason);
    };
    return e;
}

param_entry boolean(const std::string &name, const std::string &description,
                    std::function<bool(ue &, bool, std::string &)> setter,
                    std::function<double(ue &)> reader)
{
    param_entry e;
    e.name = name;
    e.type = param_type::boolean;
    e.bounded = false;
    e.description = description;
    e.read = reader;
    e.apply = [setter](ue &u, const param_value &v, std::string &reason) {
        bool b = false;
        if (!value_as_bool(v, b)) { reason = "expected a boolean"; return false; }
        return setter(u, b, reason);
    };
    return e;
}

// Rate cap, SINR offset and traffic target only differ by direction, so they are built
// once per direction instead of written twice.
void add_directional(std::vector<param_entry> &out, int tx_dir)
{
    const std::string p = (tx_dir == TX_DL) ? "dl." : "ul.";

    out.push_back(number(p + "rmax_mbps", "Mbps", 0.0, UNBOUNDED,
        "Rate cap over the air, as a token bucket in the scheduler. 0 disables the cap.",
        [tx_dir](ue &u, double mbps, std::string &) {
            ue_overrides &c = u.overrides();
            const float bps = (float)(mbps * MBIT2BIT);
            const bool was_off = c.rmax_bps[tx_dir] <= 0.0f;
            c.rmax_bps[tx_dir] = bps;
            // A cap that comes into force starts with a full bucket; one TTI worth, which
            // is the depth, so there is no burst credit accumulated while it was off.
            if (was_off && bps > 0.0f) c.rmax_tokens[tx_dir] = bps * 0.001f;
            return true;
        },
        [tx_dir](ue &u) { return (double)u.overrides().rmax_bps[tx_dir] / MBIT2BIT; }));

    // Client driven injection. The only incremental knob of the catalogue: it adds to
    // what is pending instead of replacing it, which is why its read returns the
    // cumulative total, so that a client retrying after a timeout can tell whether its
    // injection arrived.
    {
        param_entry e = number(p + "inject_bytes", "bytes", 0.0, UNBOUNDED,
            "Hands N bytes over to the UE now. Adds to the configured traffic instead of "
            "replacing it, and the pacing is the client's: the emulator packetizes and "
            "queues them on the next step. Incremental; the read returns the total "
            "injected so far. Mind pkt_delay_budget_s: anything that does not make it "
            "out in time is discarded as expired.",
            [tx_dir](ue &u, double bytes, std::string &reason) {
                if (!std::isfinite(bytes)
                    || bytes < 0.0
                    || std::floor(bytes) != bytes
                    || bytes
                        > static_cast<double>(
                              std::numeric_limits<std::uint64_t>::max()
                              / 8))
                {
                    reason =
                        "inject_bytes must be a non-negative integer byte "
                        "count within the uint64 bit range";
                    return false;
                }
                if (!u.inject_bits(
                        tx_dir,
                        static_cast<std::uint64_t>(bytes) * 8,
                        0))
                {
                    reason = "this UE has no simulated traffic source";
                    return false;
                }
                return true;
            },
            [tx_dir](ue &u) { return (double)u.pdcp_state(tx_dir).injected_bits_total() / 8.0; });
        e.incremental = true;
        out.push_back(e);
    }

    out.push_back(number(p + "sinr_offset_db", "dB", -50.0, 50.0,
        "Additive offset on the derived SINR. Moves the reported RSRP by the same amount.",
        [tx_dir](ue &u, double db, std::string &) {
            u.overrides().sinr_offset_db[tx_dir] = (float)db;
            return true;
        },
        [tx_dir](ue &u) { return (double)u.overrides().sinr_offset_db[tx_dir]; }));

    const std::string dir = (tx_dir == TX_DL) ? "dl" : "ul";
    out.push_back(number("traffic." + dir + "_target_mbps", "Mbps", 0.0, UNBOUNDED,
        "Target rate of the simulated traffic generator.",
        [tx_dir](ue &u, double mbps, std::string &reason) {
            if (!u.set_traffic_target(tx_dir, (float)(mbps * MBIT2BIT)))
            {
                reason = "this UE has no simulated traffic generator";
                return false;
            }
            return true;
        },
        [tx_dir](ue &u) {
            float bps = 0.0f;
            u.get_traffic_target(tx_dir, bps);
            return (double)bps / MBIT2BIT;
        }));
}
}

param_registry::param_registry()
{
    entries_.push_back(number("priority", "", 0.0, UNBOUNDED,
        "Absolute scheduling priority. Replaces the .ini value; it does not multiply it. "
        "Has no effect under round robin, which ignores priority.",
        [](ue &u, double v, std::string &) { u.overrides().priority = (float)v; return true; },
        [](ue &u) { return (double)u.overrides().priority; }));

    entries_.push_back(boolean("enabled",
        "Logical attach. A disabled UE drops its buffers and leaves the simulation: no "
        "traffic, no telemetry, no turn in the round robin rotation.",
        [](ue &u, bool v, std::string &) { u.set_enabled(v); return true; },
        [](ue &u) { return u.is_enabled() ? 1.0 : 0.0; }));

    add_directional(entries_, TX_DL);
    add_directional(entries_, TX_UL);

    entries_.push_back(number("mobility.pos_x_m", "m", -UNBOUNDED, UNBOUNDED,
        "X coordinate. Clamped to the apothem of the scenario map.",
        [](ue &u, double x, std::string &) {
            u.mobility().set_pos((float)x, u.mobility().get_pos()->_y());
            return true;
        },
        [](ue &u) { return (double)u.mobility().x(); }));

    entries_.push_back(number("mobility.pos_y_m", "m", -UNBOUNDED, UNBOUNDED,
        "Y coordinate. Clamped to the apothem of the scenario map.",
        [](ue &u, double y, std::string &) {
            u.mobility().set_pos(u.mobility().get_pos()->_x(), (float)y);
            return true;
        },
        [](ue &u) { return (double)u.mobility().y(); }));

    entries_.push_back(number("pkt_delay_budget_s", "s", 0.001, 60.0,
        "Delay budget of the PDCP buffers, both directions. A packet older than this is "
        "discarded before reaching the air. Raise it to study bulk transfers, where the "
        "default would evaporate most of an injected object.",
        [](ue &u, double s, std::string &) { u.set_pkt_delay_budget((float)s); return true; },
        [](ue &u) { return (double)u.get_pkt_delay_budget(); }));

    entries_.push_back(number("mobility.speed_kmh", "km/h", 0.0, UNBOUNDED,
        "Target speed, in the same unit as the .ini. Converted to m/s with TOMS.",
        [](ue &u, double kmh, std::string &) {
            u.mobility().set_speed((float)(kmh * TOMS));
            return true;
        },
        [](ue &u) { return (double)u.mobility().get_speed() / (TOMS); }));
}

const param_registry &param_registry::instance()
{
    static const param_registry reg;
    return reg;
}

const param_entry *param_registry::find(const std::string &name) const
{
    for (size_t i = 0; i < entries_.size(); i++)
        if (entries_[i].name == name) return &entries_[i];
    return nullptr;
}

bool param_registry::check(const param_entry &e, const param_value &v, std::string &reason) const
{
    if (e.type == param_type::boolean)
    {
        bool b = false;
        if (!value_as_bool(v, b)) { reason = "expected a boolean"; return false; }
        return true;
    }

    double d = 0.0;
    if (!value_as_double(v, d)) { reason = "expected a number"; return false; }
    if (std::isnan(d)) { reason = "not a number"; return false; }
    if (e.bounded && (d < e.min || d > e.max))
    {
        reason = "out of range [" + std::to_string(e.min) + ", " + std::to_string(e.max) + "]";
        return false;
    }
    return true;
}

std::string param_registry::describe_json() const
{
    json out = json::array();
    for (size_t i = 0; i < entries_.size(); i++)
    {
        const param_entry &e = entries_[i];
        json j;
        j["name"] = e.name;
        j["type"] = (e.type == param_type::boolean) ? "boolean" : "number";
        j["unit"] = e.unit;
        j["description"] = e.description;
        if (e.incremental) j["incremental"] = true;
        if (e.type == param_type::number && e.bounded)
        {
            if (e.min == -UNBOUNDED) j["min"] = nullptr; else j["min"] = e.min;
            if (e.max == UNBOUNDED) j["max"] = nullptr; else j["max"] = e.max;
        }
        out.push_back(j);
    }
    return out.dump();
}
