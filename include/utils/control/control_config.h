/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <string>

// Mirrors monitoring_config: the [Control] section of the .ini, parsed by
// configuration_loader and handed to control_manager::init().
struct control_config
{
    bool enabled = false;

    // unix | tcp | file | none. "file" replays a NDJSON script from disk and needs no
    // network; "none" builds the manager but leaves it inert.
    std::string transport = "none";

    // Unix socket path, or bind address for tcp.
    std::string address = "/tmp/fikore-control.sock";
    int port = 0;

    // async: never wait. barrier: advance only up to the granted TTI. barrier is refused
    // when period > 0 and degrades to async with a warning.
    std::string sync_mode = "async";
    int credit_timeout_ms = 30000;
    // Only consulted in barrier mode. Defaults to abort for the same reason on_peer_loss
    // does: a TTI run without its credit is not the synchronised experiment that was
    // asked for, and carrying on records that as if it were data. Set it to continue if
    // you would rather have an unsynchronised result than none.
    std::string on_timeout = "abort"; // abort | continue

    // What to do in barrier mode when a controller that had connected goes away.
    // abort ends the run; continue carries on free running for the rest of it. An
    // unattended run that keeps going unsynchronised writes out simulation nobody asked
    // for, which is why the default is to stop.
    std::string on_peer_loss = "abort"; // abort | continue

    // NDJSON script applied by simulated time, and journal of everything applied.
    std::string timeline_file = "none";
    std::string journal_file = "none";

    int max_cmds_per_tick = 256;

    // Replayable object events retained for one control peer. A barrier run aborts if
    // this fills, because losing transport feedback invalidates it. An async client is
    // instead required to resynchronise from a full snapshot.
    int max_object_events = 65536;
};
