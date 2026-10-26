/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <utils/control/command.h>

// Wire format of the control plane, shared by the file and the socket transports so that
// a script and a live client cannot drift apart.
//
// One JSON object per line:
//   {"id":42,"at":"now","cmds":[
//     {"target":"ue/3","set":{"priority":4.0,"dl.rmax_mbps":25.0}},
//     {"target":"ue/7","set":{"mobility.pos_x_m":120.0}}]}
//
// at_tti (integer) is the canonical instant; at_t (seconds) is accepted and converted.
// Without either, the message is applied on the next tick.
namespace ndjson
{
// Returns false and fills error when the line is not valid JSON or not shaped like a
// message. Parsing is atomic: on failure `out` is unchanged. The envelope id is
// returned separately when available so a socket can correlate a shape error.
bool parse_line(const std::string &line, std::uint64_t fallback_id,
                std::vector<command> &out, std::string &error,
                std::uint64_t *message_id = nullptr);

// Serializes an ack as one line, newline included.
std::string serialize_ack(const ack &a);

// One journal line, newline included. at_tti is the canonical instant, so a journal is
// itself a valid script and an interactive session becomes a repeatable experiment
// without any conversion.
std::string serialize_journal_entry(const command &c, double sim_t, std::int64_t tti,
                                    std::int64_t wall_ns);
}
