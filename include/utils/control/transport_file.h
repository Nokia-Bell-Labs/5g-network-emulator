/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <string>
#include <vector>

#include <utils/control/control_transport.h>

//--------------------------------------------------------------------------------------------------
// transport_file(): reads a NDJSON script from disk at startup and hands it over whole on
// the first poll(). The scheduling by at_tti is the manager's job, so a script and a live
// client go through exactly the same code path.
//
// No network, no thread: this is the transport that makes a scenario reproducible, and
// the one the smoke test uses.
//--------------------------------------------------------------------------------------------------
class transport_file : public control_transport
{
public:
    explicit transport_file(const std::string &path);

    bool poll(std::vector<command> &out) override;
    void reply(const ack &a) override;

    bool ok() const { return ok_; }

private:
    std::vector<command> pending_;
    bool delivered_ = false;
    bool ok_ = false;
};
