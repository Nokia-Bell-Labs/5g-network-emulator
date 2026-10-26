/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>
#include <common/direction.h>
#include <pkts/pkts.h>
#include <utils/control/ndjson.h>

using json = nlohmann::json;

namespace
{
// The names are the ones the ECN field is known by, not the numbers: a client
// writing "ect1" is stating what the traffic is, and the mapping to 0b01 is the
// emulator's business.
bool parse_ecn(const std::string &s, std::uint8_t &out)
{
    if (s == "not-ect") { out = ECN_NOT_ECT; return true; }
    if (s == "ect1" || s == "l4s") { out = ECN_ECT1; return true; }
    if (s == "ect0") { out = ECN_ECT0; return true; }
    if (s == "ce") { out = ECN_CE; return true; }
    return false;
}

const char *ecn_name(std::uint8_t ecn)
{
    switch (ecn)
    {
    case ECN_ECT1: return "ect1";
    case ECN_ECT0: return "ect0";
    case ECN_CE: return "ce";
    default: return "not-ect";
    }
}

command_op parse_op(const std::string &s)
{
    if (s == "get") return command_op::get;
    if (s == "describe") return command_op::describe;
    if (s == "ping") return command_op::ping;
    if (s == "grant") return command_op::grant;
    if (s == "inject") return command_op::inject;
    if (s == "forget") return command_op::forget;
    if (s == "events") return command_op::events;
    return command_op::set;
}
}

namespace ndjson
{
bool parse_line(const std::string &line, std::uint64_t fallback_id,
                std::vector<command> &out, std::string &error,
                std::uint64_t *message_id)
{
    if (message_id != nullptr) *message_id = fallback_id;
    json msg;
    try
    {
        msg = json::parse(line);
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }

    if (!msg.is_object())
    {
        error = "expected a JSON object";
        return false;
    }

    std::int64_t at_tti = -1;
    double at_t = -1.0;
    try
    {
        const std::uint64_t id = msg.contains("id")
            ? msg["id"].get<std::uint64_t>() : fallback_id;
        if (message_id != nullptr) *message_id = id;

        if (msg.contains("at_tti")) at_tti = msg["at_tti"].get<std::int64_t>();
        if (msg.contains("at_t"))
        {
            at_t = msg["at_t"].get<double>();
            // at_tti wins when both are present: it is the instant that replays a journal
            // on the exact same step, which a double cannot guarantee.
            if (at_tti < 0) at_tti = (std::int64_t)llround(at_t * 1000.0);
        }

        std::vector<command> parsed;

        std::vector<json> items;
        if (msg.contains("cmds"))
        {
            if (!msg["cmds"].is_array())
            {
                error = "cmds must be an array";
                return false;
            }
            for (const auto &c : msg["cmds"]) items.push_back(c);
        }
        else
        {
            items.push_back(msg);
        }

        for (size_t k = 0; k < items.size(); k++)
        {
            const json &item = items[k];
            command c;
            c.id = id;
            c.at_t = at_t;
            c.at_tti = at_tti;
            c.op = parse_op(item.contains("op") ? item["op"].get<std::string>() : std::string("set"));
            if (item.contains("target")) c.target = item["target"].get<std::string>();
            if (c.op == command_op::grant)
            {
                if (item.contains("until_tti")) c.until_tti = item["until_tti"].get<std::int64_t>();
                else if (item.contains("until_t")) c.until_tti = (std::int64_t)llround(item["until_t"].get<double>() * 1000.0);
                else
                {
                    error = "grant needs until_tti";
                    return false;
                }
                c.target = "cell";
            }

            if (c.op == command_op::events)
            {
                if (!item.contains("after"))
                {
                    error = "events needs an after cursor";
                    return false;
                }
                c.after = item["after"].get<std::uint64_t>();
                if (item.contains("include_state"))
                    c.include_state = item["include_state"].get<bool>();
                if (item.contains("resync"))
                    c.resync_events = item["resync"].get<bool>();
                c.target = "cell";
            }

            if (c.op == command_op::inject || c.op == command_op::forget)
            {
                if (!item.contains("tag"))
                {
                    error = "inject and forget need a tag";
                    return false;
                }
                const std::int64_t tag = item["tag"].get<std::int64_t>();
                if (tag <= 0 || tag > 0xFFFFFFFFLL)
                {
                    error = "tag must be in 1..2^32-1; 0 is the generator's traffic";
                    return false;
                }
                c.tag = (std::uint32_t)tag;
            }

            if (c.op == command_op::inject)
            {
                if (item.contains("dl.bytes")) { c.tx_dir = TX_DL; c.bytes = item["dl.bytes"].get<double>(); }
                else if (item.contains("ul.bytes")) { c.tx_dir = TX_UL; c.bytes = item["ul.bytes"].get<double>(); }
                else
                {
                    error = "inject needs dl.bytes or ul.bytes";
                    return false;
                }
                if (!std::isfinite(c.bytes)
                    || c.bytes <= 0.0
                    || std::floor(c.bytes) != c.bytes
                    || c.bytes
                        > static_cast<double>(
                              std::numeric_limits<std::uint64_t>::max()
                              / 8))
                {
                    error =
                        "inject needs a positive integer byte count within "
                        "the uint64 bit range";
                    return false;
                }
                if (item.contains("ecn"))
                {
                    if (!item["ecn"].is_string() ||
                        !parse_ecn(item["ecn"].get<std::string>(), c.ecn))
                    {
                        error = "ecn must be one of not-ect, ect0, ect1, ce";
                        return false;
                    }
                }
            }

            if (item.contains("set"))
            {
                if (!item["set"].is_object())
                {
                    error = "set must be an object";
                    return false;
                }
                for (auto it = item["set"].begin(); it != item["set"].end(); ++it)
                {
                    if (it.value().is_boolean())
                        c.sets.push_back(std::make_pair(it.key(), param_value(it.value().get<bool>())));
                    else if (it.value().is_string())
                        c.sets.push_back(std::make_pair(it.key(), param_value(it.value().get<std::string>())));
                    else if (it.value().is_number())
                        c.sets.push_back(std::make_pair(it.key(), param_value(it.value().get<double>())));
                    else
                    {
                        error = "unsupported value type for key " + it.key();
                        return false;
                    }
                }
            }

            if (c.op == command_op::set && c.sets.empty())
            {
                error = "set command without values";
                return false;
            }
            if (c.op != command_op::describe && c.op != command_op::grant && c.target.empty())
            {
                error = "missing target";
                return false;
            }

            parsed.push_back(c);
        }
        out.insert(out.end(), parsed.begin(), parsed.end());
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }

    return true;
}

std::string serialize_journal_entry(const command &c, double sim_t, std::int64_t tti,
                                    std::int64_t wall_ns)
{
    json cmd;
    cmd["target"] = c.target;
    switch (c.op)
    {
    case command_op::get: cmd["op"] = "get"; break;
    case command_op::describe: cmd["op"] = "describe"; break;
    case command_op::ping: cmd["op"] = "ping"; break;
    case command_op::grant: cmd["op"] = "grant"; break;
    case command_op::inject:
        cmd["op"] = "inject";
        cmd["tag"] = c.tag;
        cmd[c.tx_dir == TX_UL ? "ul.bytes" : "dl.bytes"] = c.bytes;
        if (c.ecn != ECN_NOT_ECT) cmd["ecn"] = ecn_name(c.ecn);
        break;
    case command_op::forget:
        cmd["op"] = "forget";
        cmd["tag"] = c.tag;
        break;
    case command_op::events:
        cmd["op"] = "events";
        cmd["after"] = c.after;
        if (c.include_state) cmd["include_state"] = true;
        if (c.resync_events) cmd["resync"] = true;
        break;
    default: cmd["op"] = "set"; break;
    }

    if (!c.sets.empty())
    {
        json sets = json::object();
        for (size_t i = 0; i < c.sets.size(); i++)
        {
            if (const bool *b = std::get_if<bool>(&c.sets[i].second)) sets[c.sets[i].first] = *b;
            else if (const std::string *str = std::get_if<std::string>(&c.sets[i].second)) sets[c.sets[i].first] = *str;
            else sets[c.sets[i].first] = std::get<double>(c.sets[i].second);
        }
        cmd["set"] = sets;
    }

    json j;
    j["id"] = c.id;
    j["at_tti"] = tti;
    j["at_t"] = sim_t;
    j["wall_ns"] = wall_ns;
    j["cmds"] = json::array({cmd});
    return j.dump() + "\n";
}

std::string serialize_ack(const ack &a)
{
    json j;
    j["id"] = a.id;
    j["status"] = a.ok ? "ok" : "error";
    j["tti"] = a.tti;
    j["t"] = a.t;

    if (!a.errors.empty())
    {
        json errs = json::array();
        for (size_t i = 0; i < a.errors.size(); i++)
        {
            json e;
            e["key"] = a.errors[i].key;
            e["reason"] = a.errors[i].reason;
            errs.push_back(e);
        }
        j["errors"] = errs;
    }

    if (a.credit_until_tti >= 0) j["credit_until_tti"] = a.credit_until_tti;

    if (!a.payload.empty())
    {
        try
        {
            j["result"] = json::parse(a.payload);
        }
        catch (const std::exception &)
        {
            j["result"] = a.payload;
        }
    }

    return j.dump() + "\n";
}
}
