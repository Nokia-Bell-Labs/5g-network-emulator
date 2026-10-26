/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <utils/control/command.h>

//--------------------------------------------------------------------------------------------------
// control_transport(): how commands reach the emulator. The core knows nothing about
// files or sockets; it only drains whatever the transport has ready at the start of a
// TTI and answers through reply().
//
// poll() is called from the simulation thread at the quiescent point, never concurrently
// with itself. A transport that owns a thread must do its own locking internally.
//--------------------------------------------------------------------------------------------------
class control_transport
{
public:
    virtual ~control_transport() {}

    // Moves everything ready into out. Returns false when the transport is done and will
    // never produce anything again, which lets the manager stop asking.
    virtual bool poll(std::vector<command> &out) = 0;

    virtual void reply(const ack &a) = 0;

    virtual void stop() {}

    // A grant cannot wait in the inbox: in barrier mode the simulation thread is blocked
    // and cannot drain it, so the transport applies it in place through this sink. It is
    // safe because a grant only touches the manager's own counter, never simulation
    // state.
    using grant_sink = std::function<void(const command &, ack &)>;
    void set_grant_sink(grant_sink sink) { grant_sink_ = sink; }

    // The barrier only fails open once a peer that had connected goes away; a transport
    // with no notion of a peer never blocks.
    virtual bool peer_alive() const { return false; }
    virtual bool peer_ever_connected() const { return false; }
    virtual bool command_is_current(const command &) const { return true; }
    virtual std::uint64_t current_generation() const { return 0; }

protected:
    grant_sink grant_sink_;
};
